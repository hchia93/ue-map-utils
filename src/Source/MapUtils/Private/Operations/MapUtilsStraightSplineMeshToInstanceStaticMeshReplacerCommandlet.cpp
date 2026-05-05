#include "Operations/MapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet.h"

// Editor-only by design: drives UEditorEngine + package save. Trap any Runtime-type drift early.
static_assert(WITH_EDITOR, "MapUtils commandlets are editor-only; keep MapUtils.uplugin Module Type=Editor.");

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#include "MapUtilsModule.h"

namespace
{
    static FVector ForwardAxisVector(ESplineMeshAxis::Type Axis)
    {
        switch (Axis)
        {
        case ESplineMeshAxis::X: return FVector(1, 0, 0);
        case ESplineMeshAxis::Y: return FVector(0, 1, 0);
        case ESplineMeshAxis::Z: return FVector(0, 0, 1);
        }
        return FVector(1, 0, 0);
    }

    static int32 ForwardAxisIndex(ESplineMeshAxis::Type Axis)
    {
        switch (Axis)
        {
        case ESplineMeshAxis::X: return 0;
        case ESplineMeshAxis::Y: return 1;
        case ESplineMeshAxis::Z: return 2;
        }
        return 0;
    }

    static FString ForwardAxisString(ESplineMeshAxis::Type Axis)
    {
        switch (Axis)
        {
        case ESplineMeshAxis::X: return TEXT("X");
        case ESplineMeshAxis::Y: return TEXT("Y");
        case ESplineMeshAxis::Z: return TEXT("Z");
        }
        return TEXT("X");
    }

    /** Mesh extent along Axis in mesh local space, taken from rendering bounds.
     *  Returns 0 if mesh or bounds are invalid (caller treats as failure). */
    static double MeshForwardExtent(const UStaticMesh* Mesh, ESplineMeshAxis::Type Axis)
    {
        if (!Mesh)
        {
            return 0.0;
        }
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const int32 Idx = ForwardAxisIndex(Axis);
        return 2.0 * Bounds.BoxExtent[Idx];
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
}

UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::Main(const FString& Params)
{
    UE_LOG(LogMapUtils, Display, TEXT("StraightSplineMeshToInstanceStaticMeshReplacer commandlet starting..."));

    FOptions Options;
    if (!ParseOptions(Params, Options))
    {
        return 1;
    }
    UE_LOG(LogMapUtils, Display, TEXT("StraightSplineMeshToInstanceStaticMeshReplacer: candidates=%d levels=%d dryRun=%s tangentDot=%.4f chordDot=%.4f manifest=%s"), Options.CandidateAssetPaths.Num(), Options.LevelPaths.Num(), Options.bDryRun ? TEXT("true") : TEXT("false"), Options.TangentParallelDot, Options.TangentChordDot, *Options.ManifestPath);

    TArray<UClass*> CandidateClasses;
    if (!ResolveCandidateClasses(Options.CandidateAssetPaths, CandidateClasses))
    {
        return 1;
    }

    TArray<FLevelResult> LevelResults;
    LevelResults.Reserve(Options.LevelPaths.Num());

    for (const FString& LevelPath : Options.LevelPaths)
    {
        FLevelResult LevelResult;
        LevelResult.LevelPath = LevelPath;

        const bool bOK = ProcessLevel(LevelPath, CandidateClasses, Options, LevelResult);
        if (!bOK && LevelResult.FailReason.IsEmpty())
        {
            LevelResult.FailReason = TEXT("unknown failure");
        }
        LevelResults.Add(LevelResult);

        UE_LOG(LogMapUtils, Display, TEXT("Level %s -> scanned=%d eligible=%d replaced=%d segs(s/c)=%d/%d saved=%s reason=%s"), *LevelPath, LevelResult.ActorsScanned, LevelResult.ActorsEligible, LevelResult.ActorsReplaced, LevelResult.SegmentsStraight, LevelResult.SegmentsCurved, LevelResult.bSaved ? TEXT("true") : TEXT("false"), *LevelResult.FailReason);

        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    if (!WriteManifest(Options.ManifestPath, LevelResults, Options))
    {
        return 1;
    }

    int32 TotalScanned = 0, TotalEligible = 0, TotalReplaced = 0, LevelsRed = 0;
    int32 TotalSegStraight = 0, TotalSegCurved = 0;
    for (const FLevelResult& R : LevelResults)
    {
        TotalScanned    += R.ActorsScanned;
        TotalEligible   += R.ActorsEligible;
        TotalReplaced   += R.ActorsReplaced;
        TotalSegStraight += R.SegmentsStraight;
        TotalSegCurved   += R.SegmentsCurved;
        if (!R.FailReason.IsEmpty())
        {
            ++LevelsRed;
        }
    }

    UE_LOG(LogMapUtils, Display, TEXT("Complete. levels=%d red=%d scanned=%d eligible=%d replaced=%d segs(s/c)=%d/%d (dryRun=%s)"), LevelResults.Num(), LevelsRed, TotalScanned, TotalEligible, TotalReplaced, TotalSegStraight, TotalSegCurved, Options.bDryRun ? TEXT("true") : TEXT("false"));

    return LevelsRed > 0 ? 2 : 0;
}

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::ParseOptions(const FString& Params, FOptions& OutOptions) const
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
        UE_LOG(LogMapUtils, Error, TEXT("Required: -candidates=<bp asset paths>"));
        return false;
    }
    SplitCommaList(CandidatesValue, OutOptions.CandidateAssetPaths);

    FString LevelsValue;
    if (!FParse::Value(*Params, TEXT("-levels="), LevelsValue, false) || LevelsValue.IsEmpty())
    {
        UE_LOG(LogMapUtils, Error, TEXT("Required: -levels=<level paths>"));
        return false;
    }
    SplitCommaList(LevelsValue, OutOptions.LevelPaths);

    if (!FParse::Value(*Params, TEXT("-manifest="), OutOptions.ManifestPath, false))
    {
        const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutOptions.ManifestPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("StraightSplineMeshToInstanceStaticMesh"), TEXT("Manifest"), FString::Printf(TEXT("%s.json"), *Stamp));
    }
    OutOptions.ManifestPath.TrimQuotesInline();

    OutOptions.bDryRun = !FParse::Param(*Params, TEXT("execute"));

    float ParsedFloat;
    if (FParse::Value(*Params, TEXT("-tangentdot="), ParsedFloat))
    {
        OutOptions.TangentParallelDot = ParsedFloat;
    }
    if (FParse::Value(*Params, TEXT("-chorddot="), ParsedFloat))
    {
        OutOptions.TangentChordDot = ParsedFloat;
    }

    return true;
}

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::ResolveCandidateClasses(const TArray<FString>& CandidateAssetPaths, TArray<UClass*>& OutClasses) const
{
    OutClasses.Reset();
    for (const FString& AssetPath : CandidateAssetPaths)
    {
        UClass* Cls = LoadBPGeneratedClass(AssetPath);
        if (!Cls)
        {
            UE_LOG(LogMapUtils, Error, TEXT("Failed to resolve candidate BP class: %s"), *AssetPath);
            return false;
        }
        OutClasses.Add(Cls);
    }
    return OutClasses.Num() > 0;
}

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::ProcessLevel(const FString& LevelPath, const TArray<UClass*>& CandidateClasses, const FOptions& Opts, FLevelResult& OutResult) const
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

    // Snapshot candidates first, then mutate.
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
            if (ActorClass == Candidate || ActorClass->IsChildOf(Candidate))
            {
                Matches.Add({Actor, Candidate});
                break;
            }
        }
    }

    OutResult.ActorsScanned = Matches.Num();

    int32 ReplacedThisLevel = 0;
    bool bAnyMutation = false;

    for (const FActorMatch& M : Matches)
    {
        FActorRecord Rec;
        Rec.OldActorName  = M.Actor->GetName();
        Rec.OldActorLabel = M.Actor->GetActorLabel();
        Rec.OldFolder     = M.Actor->GetFolderPath().ToString();
        Rec.OldBPClassPath = M.MatchedClass ? M.MatchedClass->GetPathName() : FString();
        Rec.OldActorWorldXform = M.Actor->GetActorTransform();

        AnalyzeActor(M.Actor, Opts, Rec);

        OutResult.SegmentsTotal    += Rec.Segments.Num();
        for (const FSegmentRecord& S : Rec.Segments)
        {
            (S.bStraight ? OutResult.SegmentsStraight : OutResult.SegmentsCurved) += 1;
        }

        if (Rec.bAllStraight)
        {
            ++OutResult.ActorsEligible;
            if (!Opts.bDryRun)
            {
                if (ReplaceActor(M.Actor, World, Rec))
                {
                    Rec.bReplaced = true;
                    ++ReplacedThisLevel;
                    bAnyMutation = true;
                }
            }
        }
        OutResult.Records.Add(Rec);
    }

    OutResult.ActorsReplaced = ReplacedThisLevel;

    if (Opts.bDryRun || !bAnyMutation)
    {
        OutResult.bSaved = false;
        return true;
    }

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetMapPackageExtension());

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

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::AnalyzeActor(AActor* OldActor, const FOptions& Opts, FActorRecord& OutRecord) const
{
    TArray<USplineMeshComponent*> SplineComps;
    OldActor->GetComponents<USplineMeshComponent>(SplineComps);

    if (SplineComps.IsEmpty())
    {
        OutRecord.FailReason = TEXT("no SplineMeshComponent on actor");
        return false;
    }

    const FTransform ActorWorld = OutRecord.OldActorWorldXform;
    bool bAllStraight = true;

    for (USplineMeshComponent* SMC : SplineComps)
    {
        if (!SMC || !IsValid(SMC) || SMC->IsEditorOnly())
        {
            continue;
        }

        UStaticMesh* Mesh = SMC->GetStaticMesh();
        const FSplineMeshParams& SP = SMC->SplineParams;

        FSegmentRecord Seg;
        Seg.MeshPath          = Mesh ? Mesh->GetPathName() : FString();
        Seg.LocalStartPos     = SP.StartPos;
        Seg.LocalStartTangent = SP.StartTangent;
        Seg.LocalEndPos       = SP.EndPos;
        Seg.LocalEndTangent   = SP.EndTangent;

        const FVector StartTanN = SP.StartTangent.GetSafeNormal();
        const FVector EndTanN   = SP.EndTangent.GetSafeNormal();
        const FVector Chord     = SP.EndPos - SP.StartPos;
        const double  ChordLen  = Chord.Length();
        const FVector ChordN    = Chord.GetSafeNormal();

        const bool bDegenerate = ChordLen < KINDA_SMALL_NUMBER
            || StartTanN.IsNearlyZero() || EndTanN.IsNearlyZero();
        const bool bParallel = !bDegenerate && (FVector::DotProduct(StartTanN, EndTanN) > Opts.TangentParallelDot);
        const bool bAligned  = !bDegenerate && (FVector::DotProduct(StartTanN, ChordN)  > Opts.TangentChordDot);

        Seg.bStraight = (bParallel && bAligned);

        if (Seg.bStraight && Mesh)
        {
            const ESplineMeshAxis::Type FwdAxis = SMC->ForwardAxis;
            const FVector MeshFwdLocal = ForwardAxisVector(FwdAxis);
            const double MeshFwdLen = MeshForwardExtent(Mesh, FwdAxis);

            if (MeshFwdLen > KINDA_SMALL_NUMBER)
            {
                // Align mesh forward axis to chord direction (minimal-arc rotation).
                // Up axis is unconstrained; matches SplineMC's convention for cylindrically
                // symmetric meshes.
                const FQuat LocalRot = FQuat::FindBetweenNormals(MeshFwdLocal, ChordN);

                // Forward axis scaled by chord_length / mesh_forward_length; other axes = 1.
                FVector LocalScale(1, 1, 1);
                LocalScale[ForwardAxisIndex(FwdAxis)] = ChordLen / MeshFwdLen;

                Seg.BakedActorLocal = FTransform(LocalRot, SP.StartPos, LocalScale);
                Seg.BakedWorld      = Seg.BakedActorLocal * ActorWorld;
            }
            else
            {
                // Mesh has zero forward extent on the chosen axis -> can't bake; treat as curved.
                Seg.bStraight = false;
            }
        }

        if (!Seg.bStraight)
        {
            bAllStraight = false;
        }

        OutRecord.Segments.Add(Seg);
    }

    OutRecord.bAllStraight = bAllStraight && OutRecord.Segments.Num() > 0;
    return OutRecord.bAllStraight;
}

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::ReplaceActor(AActor* OldActor, UWorld* World, FActorRecord& InOutRecord) const
{
    // Spawn at identity and FinishSpawning at the source actor's transform to avoid the
    // deferred-template double-rotation gotcha (same as in BlueprintToStaticMeshReplacer).
    FActorSpawnParameters SpawnParams;
    SpawnParams.OverrideLevel = OldActor->GetLevel();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bDeferConstruction = true;

    AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!NewActor)
    {
        InOutRecord.FailReason = TEXT("SpawnActor<AActor> returned null");
        return false;
    }

    USceneComponent* Root = NewObject<USceneComponent>(NewActor, USceneComponent::StaticClass(), TEXT("DefaultSceneRoot"));
    Root->SetMobility(EComponentMobility::Static);
    Root->CreationMethod = EComponentCreationMethod::Instance;
    NewActor->SetRootComponent(Root);
    Root->RegisterComponent();
    NewActor->AddInstanceComponent(Root);

    // Group baked segments by mesh asset to produce one ISMC per unique mesh.
    TMap<UStaticMesh*, UInstancedStaticMeshComponent*> MeshToISM;
    int32 ComponentIdx = 0;
    for (const FSegmentRecord& Seg : InOutRecord.Segments)
    {
        if (!Seg.bStraight || Seg.MeshPath.IsEmpty())
        {
            continue;
        }
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Seg.MeshPath);
        if (!Mesh)
        {
            continue;
        }

        UInstancedStaticMeshComponent** Existing = MeshToISM.Find(Mesh);
        UInstancedStaticMeshComponent* ISMC = nullptr;
        if (Existing)
        {
            ISMC = *Existing;
        }
        else
        {
            const FName CompName(*FString::Printf(TEXT("ISMC_%d"), ComponentIdx++));
            ISMC = NewObject<UInstancedStaticMeshComponent>(NewActor, UInstancedStaticMeshComponent::StaticClass(), CompName);
            ISMC->SetMobility(EComponentMobility::Static);
            ISMC->CreationMethod = EComponentCreationMethod::Instance;
            ISMC->SetStaticMesh(Mesh);
            ISMC->SetCollisionProfileName(TEXT("NoCollision"));
            ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            ISMC->SetGenerateOverlapEvents(false);
            ISMC->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            ISMC->RegisterComponent();
            NewActor->AddInstanceComponent(ISMC);
            MeshToISM.Add(Mesh, ISMC);
        }

        // BakedActorLocal is in source-actor-local space. The ISMC is attached to the new
        // actor's root with identity relative transform, and FinishSpawning will set the
        // root to the source actor's world transform — so component-local == source-actor-local.
        ISMC->AddInstance(Seg.BakedActorLocal);
    }

    NewActor->SetFolderPath(FName(*InOutRecord.OldFolder));
    const FString NewLabel = InOutRecord.OldActorLabel + TEXT("_ISM");
    NewActor->SetActorLabel(NewLabel, /*bMarkDirty=*/false);

    NewActor->FinishSpawning(InOutRecord.OldActorWorldXform);

    InOutRecord.NewActorName = NewActor->GetName();

    World->DestroyActor(OldActor);
    return true;
}

bool UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet::WriteManifest(const FString& ManifestPath, const TArray<FLevelResult>& LevelResults, const FOptions& Opts) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("phase"), Opts.bDryRun ? TEXT("dryrun") : TEXT("execute"));
    Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
    Root->SetNumberField(TEXT("tangent_parallel_dot"), Opts.TangentParallelDot);
    Root->SetNumberField(TEXT("tangent_chord_dot"), Opts.TangentChordDot);

    TArray<TSharedPtr<FJsonValue>> LevelsJson;
    int32 TotalScanned = 0, TotalEligible = 0, TotalReplaced = 0;
    int32 TotalSegStraight = 0, TotalSegCurved = 0;
    int32 LevelsRed = 0;

    for (const FLevelResult& R : LevelResults)
    {
        TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetStringField(TEXT("level"), R.LevelPath);
        L->SetNumberField(TEXT("actors_scanned"), R.ActorsScanned);
        L->SetNumberField(TEXT("actors_eligible"), R.ActorsEligible);
        L->SetNumberField(TEXT("actors_replaced"), R.ActorsReplaced);
        L->SetNumberField(TEXT("segments_total"), R.SegmentsTotal);
        L->SetNumberField(TEXT("segments_straight"), R.SegmentsStraight);
        L->SetNumberField(TEXT("segments_curved"), R.SegmentsCurved);
        L->SetBoolField(TEXT("saved"), R.bSaved);
        L->SetStringField(TEXT("fail_reason"), R.FailReason);

        TArray<TSharedPtr<FJsonValue>> RecordsJson;
        for (const FActorRecord& Rec : R.Records)
        {
            TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("old_actor_name"), Rec.OldActorName);
            J->SetStringField(TEXT("old_actor_label"), Rec.OldActorLabel);
            J->SetStringField(TEXT("old_folder"), Rec.OldFolder);
            J->SetStringField(TEXT("old_bp_class"), Rec.OldBPClassPath);
            J->SetStringField(TEXT("new_actor_name"), Rec.NewActorName);
            J->SetStringField(TEXT("old_actor_world_loc"), Rec.OldActorWorldXform.GetLocation().ToString());
            J->SetStringField(TEXT("old_actor_world_rot"), Rec.OldActorWorldXform.Rotator().ToString());
            J->SetStringField(TEXT("old_actor_world_scale"), Rec.OldActorWorldXform.GetScale3D().ToString());
            J->SetBoolField(TEXT("all_straight"), Rec.bAllStraight);
            J->SetBoolField(TEXT("replaced"), Rec.bReplaced);
            J->SetStringField(TEXT("fail_reason"), Rec.FailReason);

            // Aggregate ISM grouping preview: how many instances per unique mesh
            TMap<FString, int32> MeshCount;
            for (const FSegmentRecord& Seg : Rec.Segments)
            {
                if (!Seg.bStraight || Seg.MeshPath.IsEmpty())
                {
                    continue;
                }
                MeshCount.FindOrAdd(Seg.MeshPath) += 1;
            }
            TArray<TSharedPtr<FJsonValue>> GroupsJson;
            for (const auto& Pair : MeshCount)
            {
                TSharedRef<FJsonObject> G = MakeShared<FJsonObject>();
                G->SetStringField(TEXT("mesh"), Pair.Key);
                G->SetNumberField(TEXT("instance_count"), Pair.Value);
                GroupsJson.Add(MakeShared<FJsonValueObject>(G));
            }
            J->SetArrayField(TEXT("ism_groups"), GroupsJson);

            // Per-segment baked transform preview (only if eligible)
            if (Rec.bAllStraight)
            {
                TArray<TSharedPtr<FJsonValue>> SegsJson;
                for (const FSegmentRecord& Seg : Rec.Segments)
                {
                    TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
                    S->SetStringField(TEXT("mesh"), Seg.MeshPath);
                    S->SetStringField(TEXT("local_start_pos"), Seg.LocalStartPos.ToString());
                    S->SetStringField(TEXT("local_end_pos"), Seg.LocalEndPos.ToString());
                    S->SetStringField(TEXT("baked_world_loc"), Seg.BakedWorld.GetLocation().ToString());
                    S->SetStringField(TEXT("baked_world_rot"), Seg.BakedWorld.Rotator().ToString());
                    S->SetStringField(TEXT("baked_world_scale"), Seg.BakedWorld.GetScale3D().ToString());
                    SegsJson.Add(MakeShared<FJsonValueObject>(S));
                }
                J->SetArrayField(TEXT("segments"), SegsJson);
            }

            RecordsJson.Add(MakeShared<FJsonValueObject>(J));
        }
        L->SetArrayField(TEXT("records"), RecordsJson);
        LevelsJson.Add(MakeShared<FJsonValueObject>(L));

        TotalScanned    += R.ActorsScanned;
        TotalEligible   += R.ActorsEligible;
        TotalReplaced   += R.ActorsReplaced;
        TotalSegStraight += R.SegmentsStraight;
        TotalSegCurved   += R.SegmentsCurved;
        if (!R.FailReason.IsEmpty())
        {
            ++LevelsRed;
        }
    }

    Root->SetArrayField(TEXT("levels"), LevelsJson);
    Root->SetNumberField(TEXT("total_actors_scanned"), TotalScanned);
    Root->SetNumberField(TEXT("total_actors_eligible"), TotalEligible);
    Root->SetNumberField(TEXT("total_actors_replaced"), TotalReplaced);
    Root->SetNumberField(TEXT("total_segments_straight"), TotalSegStraight);
    Root->SetNumberField(TEXT("total_segments_curved"), TotalSegCurved);
    Root->SetNumberField(TEXT("levels_red"), LevelsRed);

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        UE_LOG(LogMapUtils, Error, TEXT("Failed to serialize manifest JSON"));
        return false;
    }

    const FString ManifestDir = FPaths::GetPath(ManifestPath);
    if (!IFileManager::Get().DirectoryExists(*ManifestDir))
    {
        IFileManager::Get().MakeDirectory(*ManifestDir, /*Tree=*/true);
    }

    if (!FFileHelper::SaveStringToFile(OutString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMapUtils, Error, TEXT("Failed to write manifest: %s"), *ManifestPath);
        return false;
    }

    UE_LOG(LogMapUtils, Display, TEXT("Manifest written: %s"), *ManifestPath);
    return true;
}
