#include "Operations/MapUtilsBlueprintToStaticMeshReplacerCommandlet.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StaticMeshComponentLODInfo.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#include "MapUtilsModule.h"

namespace
{
    static FString MobilityToString(EComponentMobility::Type Mobility)
    {
        switch (Mobility)
        {
        case EComponentMobility::Static:    return TEXT("Static");
        case EComponentMobility::Stationary: return TEXT("Stationary");
        case EComponentMobility::Movable:   return TEXT("Movable");
        }
        return TEXT("Unknown");
    }

    /** Find the (singular) non-editor StaticMeshComponent on the actor, if any.
     *  Returns nullptr if there is zero or more-than-one such component. */
    static UStaticMeshComponent* FindSingularSMC(AActor* Actor)
    {
        if (!Actor)
        {
            return nullptr;
        }
        UStaticMeshComponent* Found = nullptr;
        TArray<UStaticMeshComponent*> All;
        Actor->GetComponents<UStaticMeshComponent>(All);
        for (UStaticMeshComponent* Comp : All)
        {
            if (!Comp || Comp->IsEditorOnly())
            {
                continue;
            }
            if (Found)
            {
                return nullptr; // multiple SMCs -> not a candidate shape
            }
            Found = Comp;
        }
        return Found;
    }

    /** Resolve "/Game/Path/BP_X" to its UClass (BlueprintGeneratedClass). */
    static UClass* LoadBPGeneratedClass(const FString& AssetPath)
    {
        UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!BP || !BP->GeneratedClass)
        {
            return nullptr;
        }
        return BP->GeneratedClass;
    }

    /** Trim OverrideMaterials to mesh slot count. Returns false if any trimmed
     *  entry was non-null (data loss → caller should abort this instance). */
    static bool TrimOverrideMaterials(TArray<UMaterialInterface*>& Mats, int32 SlotCount)
    {
        if (Mats.Num() <= SlotCount)
        {
            return true;
        }
        for (int32 i = SlotCount; i < Mats.Num(); ++i)
        {
            if (Mats[i] != nullptr)
            {
                return false;
            }
        }
        Mats.SetNum(SlotCount);
        return true;
    }
}

UMapUtilsBlueprintToStaticMeshReplacerCommandlet::UMapUtilsBlueprintToStaticMeshReplacerCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMapUtilsBlueprintToStaticMeshReplacerCommandlet::Main(const FString& Params)
{
    UE_LOG(LogMapUtils, Display, TEXT("BlueprintToStaticMeshReplacer commandlet starting..."));

    FOptions Options;
    if (!ParseOptions(Params, Options))
    {
        return 1;
    }
    UE_LOG(LogMapUtils, Display, TEXT("BlueprintToStaticMeshReplacer: candidates=%d levels=%d dryRun=%s manifest=%s"),
        Options.CandidateAssetPaths.Num(),
        Options.LevelPaths.Num(),
        Options.bDryRun ? TEXT("true") : TEXT("false"),
        *Options.ManifestPath);

    TArray<UClass*> CandidateClasses;
    if (!ResolveCandidateClasses(Options.CandidateAssetPaths, CandidateClasses))
    {
        return 1;
    }
    UE_LOG(LogMapUtils, Display, TEXT("BlueprintToStaticMeshReplacer: resolved %d candidate classes"), CandidateClasses.Num());

    TArray<FLevelResult> LevelResults;
    LevelResults.Reserve(Options.LevelPaths.Num());

    for (const FString& LevelPath : Options.LevelPaths)
    {
        FLevelResult LevelResult;
        LevelResult.LevelPath = LevelPath;

        const bool bOK = ProcessLevel(LevelPath, CandidateClasses, Options.bDryRun, LevelResult);
        if (!bOK && LevelResult.FailReason.IsEmpty())
        {
            LevelResult.FailReason = TEXT("unknown failure");
        }
        LevelResults.Add(LevelResult);

        UE_LOG(LogMapUtils, Display,
            TEXT("BlueprintToStaticMeshReplacer: %s -> found=%d replaced=%d saved=%s reason=%s"),
            *LevelPath,
            LevelResult.InstancesFound,
            LevelResult.InstancesReplaced,
            LevelResult.bSaved ? TEXT("true") : TEXT("false"),
            *LevelResult.FailReason);

        // GC between levels so package memory is reclaimed before the next load.
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    if (!WriteManifest(Options.ManifestPath, LevelResults, Options.bDryRun))
    {
        return 1;
    }

    int32 TotalFound = 0;
    int32 TotalReplaced = 0;
    int32 LevelsRed = 0;
    for (const FLevelResult& R : LevelResults)
    {
        TotalFound += R.InstancesFound;
        TotalReplaced += R.InstancesReplaced;
        if (!R.FailReason.IsEmpty())
        {
            ++LevelsRed;
        }
    }

    UE_LOG(LogMapUtils, Display,
        TEXT("BlueprintToStaticMeshReplacer: complete. levels=%d red=%d instances_found=%d instances_replaced=%d (dryRun=%s)"),
        LevelResults.Num(), LevelsRed, TotalFound, TotalReplaced,
        Options.bDryRun ? TEXT("true") : TEXT("false"));

    return LevelsRed > 0 ? 2 : 0;
}

bool UMapUtilsBlueprintToStaticMeshReplacerCommandlet::ParseOptions(const FString& Params, FOptions& OutOptions) const
{
    auto SplitCommaList = [](const FString& Value, TArray<FString>& OutList)
    {
        FString Trimmed = Value;
        Trimmed.TrimQuotesInline();
        Trimmed.ParseIntoArray(OutList, TEXT(","), true);
        for (FString& S : OutList)
        {
            S.TrimStartAndEndInline();
        }
    };

    FString CandidatesValue;
    if (!FParse::Value(*Params, TEXT("-candidates="), CandidatesValue, false) || CandidatesValue.IsEmpty())
    {
        UE_LOG(LogMapUtils, Error, TEXT("BlueprintToStaticMeshReplacer: -candidates=<paths> is required"));
        return false;
    }
    SplitCommaList(CandidatesValue, OutOptions.CandidateAssetPaths);

    FString LevelsValue;
    if (!FParse::Value(*Params, TEXT("-levels="), LevelsValue, false) || LevelsValue.IsEmpty())
    {
        UE_LOG(LogMapUtils, Error, TEXT("BlueprintToStaticMeshReplacer: -levels=<paths> is required"));
        return false;
    }
    SplitCommaList(LevelsValue, OutOptions.LevelPaths);

    if (!FParse::Value(*Params, TEXT("-manifest="), OutOptions.ManifestPath, false))
    {
        // Default: Intermediate/BlueprintToStaticMeshReplacer/Manifest/<timestamp>.json
        const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutOptions.ManifestPath = FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Intermediate"), TEXT("BlueprintToStaticMeshReplacer"), TEXT("Manifest"),
            FString::Printf(TEXT("%s.json"), *Stamp));
    }
    OutOptions.ManifestPath.TrimQuotesInline();

    OutOptions.bDryRun = FParse::Param(*Params, TEXT("dryrun"));

    return true;
}

bool UMapUtilsBlueprintToStaticMeshReplacerCommandlet::ResolveCandidateClasses(
    const TArray<FString>& CandidateAssetPaths,
    TArray<UClass*>& OutClasses) const
{
    OutClasses.Reset();
    OutClasses.Reserve(CandidateAssetPaths.Num());

    for (const FString& AssetPath : CandidateAssetPaths)
    {
        UClass* Cls = LoadBPGeneratedClass(AssetPath);
        if (!Cls)
        {
            UE_LOG(LogMapUtils, Error, TEXT("BlueprintToStaticMeshReplacer: failed to resolve candidate class: %s"), *AssetPath);
            return false;
        }
        OutClasses.Add(Cls);
    }
    return OutClasses.Num() > 0;
}

bool UMapUtilsBlueprintToStaticMeshReplacerCommandlet::ProcessLevel(
    const FString& LevelPath,
    const TArray<UClass*>& CandidateClasses,
    bool bDryRun,
    FLevelResult& OutResult) const
{
    UPackage* Package = LoadPackage(nullptr, *LevelPath, LOAD_None);
    if (!Package)
    {
        OutResult.FailReason = TEXT("LoadPackage failed");
        return false;
    }

    UWorld* World = UWorld::FindWorldInPackage(Package);
    if (!World)
    {
        OutResult.FailReason = TEXT("FindWorldInPackage returned null");
        return false;
    }

    World->PersistentLevel->OnLevelLoaded();
    World->PersistentLevel->UpdateLevelComponents(/*bRerunConstructionScripts=*/ false);

    // Collect candidate actors first (snapshot), then mutate. Don't iterate-and-destroy.
    // Use the level's Actors array directly since FActorIterator requires an
    // initialized game world (not what LoadPackage produces for an editor commandlet).
    struct FActorMatch
    {
        AActor* Actor;
        UClass* MatchedClass;
    };
    TArray<FActorMatch> Matches;

    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor)
        {
            continue;
        }
        UClass* ActorClass = Actor->GetClass();
        for (UClass* Candidate : CandidateClasses)
        {
            if (ActorClass == Candidate)
            {
                Matches.Add({Actor, Candidate});
                break;
            }
        }
    }

    OutResult.InstancesFound = Matches.Num();

    if (Matches.Num() == 0)
    {
        // Not an error; level had no candidate instances. Don't save.
        OutResult.bSaved = false;
        return true;
    }

    int32 ReplacedThisLevel = 0;

    for (const FActorMatch& M : Matches)
    {
        FReplaceRecord Rec;
        Rec.LevelPath = LevelPath;
        const bool bRecOK = ReplaceInstance(M.Actor, World, M.MatchedClass, bDryRun, Rec);
        if (bRecOK)
        {
            Rec.bReplaced = true;
            ++ReplacedThisLevel;
        }
        OutResult.Records.Add(Rec);
    }

    OutResult.InstancesReplaced = ReplacedThisLevel;

    if (bDryRun || ReplacedThisLevel == 0)
    {
        OutResult.bSaved = false;
        return true;
    }

    // Save the package
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetMapPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    SaveArgs.SaveFlags = SAVE_None;
    SaveArgs.bForceByteSwapping = false;
    SaveArgs.bWarnOfLongFilename = true;

    const bool bSaved = UPackage::SavePackage(Package, World, *PackageFilename, SaveArgs);
    OutResult.bSaved = bSaved;
    if (!bSaved)
    {
        OutResult.FailReason = TEXT("SavePackage failed");
        return false;
    }

    return true;
}

bool UMapUtilsBlueprintToStaticMeshReplacerCommandlet::ReplaceInstance(
    AActor* OldActor,
    UWorld* World,
    UClass* MatchedClass,
    bool bDryRun,
    FReplaceRecord& OutRecord) const
{
    OutRecord.OldActorName  = OldActor->GetName();
    OutRecord.OldActorLabel = OldActor->GetActorLabel();
    OutRecord.OldFolder     = OldActor->GetFolderPath().ToString();
    OutRecord.OldBPClassPath = MatchedClass ? MatchedClass->GetPathName() : FString();

    UStaticMeshComponent* OldSMC = FindSingularSMC(OldActor);
    if (!OldSMC)
    {
        OutRecord.FailReason = TEXT("expected exactly 1 non-editor SMC, found 0 or >1");
        return false;
    }

    UStaticMesh* Mesh = OldSMC->GetStaticMesh();
    if (!Mesh)
    {
        OutRecord.FailReason = TEXT("OldSMC->GetStaticMesh() is null");
        return false;
    }

    const FTransform WorldXform = OldSMC->GetComponentTransform();
    OutRecord.WorldLoc = WorldXform.GetLocation();
    OutRecord.WorldRot = WorldXform.Rotator();
    OutRecord.WorldScale = WorldXform.GetScale3D();
    OutRecord.MeshPath = Mesh->GetPathName();
    OutRecord.CollisionProfileName = OldSMC->GetCollisionProfileName().ToString();
    OutRecord.MobilityStr = MobilityToString(OldSMC->Mobility);

    // Capture material array, trimmed to slot count
    TArray<UMaterialInterface*> Mats = OldSMC->OverrideMaterials;
    const int32 SlotCount = Mesh->GetStaticMaterials().Num();
    if (!TrimOverrideMaterials(Mats, SlotCount))
    {
        OutRecord.FailReason = FString::Printf(
            TEXT("OverrideMaterials beyond slot %d are non-null (data loss risk)"), SlotCount);
        return false;
    }
    for (UMaterialInterface* M : Mats)
    {
        OutRecord.MaterialPaths.Add(M ? M->GetPathName() : FString());
    }

    if (bDryRun)
    {
        return true;
    }

    // Spawn new SMA at IDENTITY; final transform applied via FinishSpawning(WorldXform).
    // Calling SpawnActor with the real transform AND FinishSpawning with the real transform
    // triggers AActor::FinishSpawning's deferred-transform recompute (Actor.cpp ~L4373):
    //   TemplateTransform = RootComponent.GetComponentTransform() * OriginalSpawnTransform.Inverse();
    //   FinalRootTransform = TemplateTransform * UserTransform;
    // which composes the spawn-time rotation with the FinishSpawning rotation, doubling Yaw.
    // Spawning at identity makes TemplateTransform == Identity, so FinalRootTransform == UserTransform.
    FActorSpawnParameters SpawnParams;
    SpawnParams.OverrideLevel = OldActor->GetLevel();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bDeferConstruction = true; // we want to set props before BeginPlay
    AStaticMeshActor* NewSMA = World->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams);
    if (!NewSMA)
    {
        OutRecord.FailReason = TEXT("SpawnActor<AStaticMeshActor> returned null");
        return false;
    }

    UStaticMeshComponent* NewSMC = NewSMA->GetStaticMeshComponent();
    if (!NewSMC)
    {
        OutRecord.FailReason = TEXT("NewSMA has no StaticMeshComponent");
        World->DestroyActor(NewSMA);
        return false;
    }

    // Mobility must be set BEFORE SetStaticMesh on Static-vs-Movable to avoid warnings;
    // also matches the source SMC's mobility regardless of BP root mobility.
    NewSMC->SetMobility(OldSMC->Mobility);
    NewSMC->SetStaticMesh(Mesh);

    // OverrideMaterials: per-slot copy (preserves slot semantics; nulls fall through to mesh defaults)
    for (int32 i = 0; i < Mats.Num(); ++i)
    {
        if (Mats[i] != nullptr)
        {
            NewSMC->SetMaterial(i, Mats[i]);
        }
    }

    // BodyInstance: field-copy preserves nested fields (responses, physMat, flags),
    // then re-apply profile + collision-enabled via setters so the property system
    // marks them as overridden vs SMA archetype (raw struct assignment alone is not
    // recognised as archetype-delta and silently reverts to AStaticMeshActor CDO on save).
    NewSMC->BodyInstance = OldSMC->BodyInstance;
    NewSMC->SetCollisionProfileName(OldSMC->GetCollisionProfileName());
    NewSMC->SetCollisionEnabled(OldSMC->GetCollisionEnabled());
    NewSMC->BodyInstance.UpdatePhysicsFilterData();

    // Vertex paint: OverrideVertexColors lives on FStaticMeshComponentLODInfo and is a raw
    // FColorVertexBuffer*, NOT a UProperty. Archetype-delta serialization sees nothing, so
    // a fresh SMA spawned from CDO has empty LODData; we must deep-copy each LOD's color
    // buffer + the PaintedVertices source-of-truth array used by the editor for re-paint.
    const int32 NumLOD = OldSMC->LODData.Num();
    if (NumLOD > 0)
    {
        NewSMC->SetLODDataCount(NumLOD, NumLOD);
        for (int32 LODIdx = 0; LODIdx < NumLOD; ++LODIdx)
        {
            const FStaticMeshComponentLODInfo& OldLOD = OldSMC->LODData[LODIdx];
            FStaticMeshComponentLODInfo& NewLOD = NewSMC->LODData[LODIdx];

            NewLOD.PaintedVertices = OldLOD.PaintedVertices;
            // OverrideMapBuildData (per-instance baked lightmap) is TUniquePtr-owned and not
            // trivially copyable. Skipped: the level needs Build Lighting after flatten anyway,
            // which regenerates this from the lightmass run.

            FColorVertexBuffer* OldCVB = OldLOD.OverrideVertexColors;
            if (OldCVB && OldCVB->GetNumVertices() > 0)
            {
                const int32 NumVerts = OldCVB->GetNumVertices();
                TArray<FColor> Colors;
                Colors.SetNumUninitialized(NumVerts);
                for (int32 v = 0; v < NumVerts; ++v)
                {
                    Colors[v] = OldCVB->VertexColor(v);
                }
                NewLOD.OverrideVertexColors = new FColorVertexBuffer();
                NewLOD.OverrideVertexColors->InitFromColorArray(Colors);
                BeginInitResource(NewLOD.OverrideVertexColors, nullptr);
            }
        }
        NewSMC->MarkRenderStateDirty();
    }

    // Custom Primitive Data: per-instance material parameter overrides. UE 5.x has TWO
    // fields: CustomPrimitiveData (UPROPERTY EditAnywhere - persisted) and
    // CustomPrimitiveDataInternal (UPROPERTY Transient - runtime buffer). The plain
    // SetCustomPrimitiveDataFloat() setter writes only Internal which gets reset from
    // persisted on load, so SavePackage drops the values. Use SetDefaultCustomPrimitiveDataFloat
    // which writes the persisted field.
    const TArray<float>& OldCPD = OldSMC->GetCustomPrimitiveData().Data;
    for (int32 i = 0; i < OldCPD.Num(); ++i)
    {
        NewSMC->SetDefaultCustomPrimitiveDataFloat(i, OldCPD[i]);
    }

    // Outliner organization
    NewSMA->SetFolderPath(OldActor->GetFolderPath());
    NewSMA->SetActorLabel(OldActor->GetActorLabel(), /*bMarkDirty=*/false);

    // Finish deferred construction
    NewSMA->FinishSpawning(WorldXform);

    OutRecord.NewActorName = NewSMA->GetName();

    World->DestroyActor(OldActor);
    return true;
}

bool UMapUtilsBlueprintToStaticMeshReplacerCommandlet::WriteManifest(
    const FString& ManifestPath,
    const TArray<FLevelResult>& LevelResults,
    bool bDryRun) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("phase"), bDryRun ? TEXT("modify-dryrun") : TEXT("modify"));
    Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());

    TArray<TSharedPtr<FJsonValue>> LevelsJson;
    int32 TotalFound = 0;
    int32 TotalReplaced = 0;
    int32 LevelsRed = 0;

    for (const FLevelResult& R : LevelResults)
    {
        TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetStringField(TEXT("level"), R.LevelPath);
        L->SetNumberField(TEXT("instances_found"), R.InstancesFound);
        L->SetNumberField(TEXT("instances_replaced"), R.InstancesReplaced);
        L->SetBoolField(TEXT("saved"), R.bSaved);
        L->SetStringField(TEXT("fail_reason"), R.FailReason);

        TArray<TSharedPtr<FJsonValue>> RecordsJson;
        for (const FReplaceRecord& Rec : R.Records)
        {
            TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("old_actor_name"), Rec.OldActorName);
            J->SetStringField(TEXT("old_actor_label"), Rec.OldActorLabel);
            J->SetStringField(TEXT("old_folder"), Rec.OldFolder);
            J->SetStringField(TEXT("old_bp_class"), Rec.OldBPClassPath);
            J->SetStringField(TEXT("new_actor_name"), Rec.NewActorName);
            J->SetStringField(TEXT("mesh"), Rec.MeshPath);
            J->SetStringField(TEXT("collision_profile"), Rec.CollisionProfileName);
            J->SetStringField(TEXT("mobility"), Rec.MobilityStr);
            J->SetStringField(TEXT("loc"), Rec.WorldLoc.ToString());
            J->SetStringField(TEXT("rot"), Rec.WorldRot.ToString());
            J->SetStringField(TEXT("scale"), Rec.WorldScale.ToString());
            J->SetBoolField(TEXT("replaced"), Rec.bReplaced);
            J->SetStringField(TEXT("fail_reason"), Rec.FailReason);

            TArray<TSharedPtr<FJsonValue>> MatsJson;
            for (const FString& M : Rec.MaterialPaths)
            {
                MatsJson.Add(MakeShared<FJsonValueString>(M));
            }
            J->SetArrayField(TEXT("materials"), MatsJson);

            RecordsJson.Add(MakeShared<FJsonValueObject>(J));
        }
        L->SetArrayField(TEXT("records"), RecordsJson);

        LevelsJson.Add(MakeShared<FJsonValueObject>(L));

        TotalFound += R.InstancesFound;
        TotalReplaced += R.InstancesReplaced;
        if (!R.FailReason.IsEmpty())
        {
            ++LevelsRed;
        }
    }

    Root->SetArrayField(TEXT("levels"), LevelsJson);
    Root->SetNumberField(TEXT("total_found"), TotalFound);
    Root->SetNumberField(TEXT("total_replaced"), TotalReplaced);
    Root->SetNumberField(TEXT("levels_red"), LevelsRed);

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        UE_LOG(LogMapUtils, Error, TEXT("BlueprintToStaticMeshReplacer: failed to serialize manifest JSON"));
        return false;
    }

    const FString ManifestDir = FPaths::GetPath(ManifestPath);
    if (!IFileManager::Get().DirectoryExists(*ManifestDir))
    {
        IFileManager::Get().MakeDirectory(*ManifestDir, /*Tree=*/true);
    }

    if (!FFileHelper::SaveStringToFile(OutString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMapUtils, Error, TEXT("BlueprintToStaticMeshReplacer: failed to write manifest: %s"), *ManifestPath);
        return false;
    }

    UE_LOG(LogMapUtils, Display, TEXT("BlueprintToStaticMeshReplacer: manifest written: %s"), *ManifestPath);
    return true;
}
