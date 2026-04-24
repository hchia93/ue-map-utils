#include "Exports/MapUtilsContextExporter.h"

#include "MapUtilsModule.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "MapUtilsContextExporter"

namespace
{
    const TCHAR* ExporterVersion = TEXT("MapUtils/1.0");

    TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("X"), V.X);
        Obj->SetNumberField(TEXT("Y"), V.Y);
        Obj->SetNumberField(TEXT("Z"), V.Z);
        return Obj;
    }

    TSharedPtr<FJsonObject> RotatorToJson(const FRotator& R)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetNumberField(TEXT("Pitch"), R.Pitch);
        Obj->SetNumberField(TEXT("Yaw"), R.Yaw);
        Obj->SetNumberField(TEXT("Roll"), R.Roll);
        return Obj;
    }

    TSharedPtr<FJsonObject> BoundsToJson(const FBox& Box)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetObjectField(TEXT("Min"), VectorToJson(Box.Min));
        Obj->SetObjectField(TEXT("Max"), VectorToJson(Box.Max));
        return Obj;
    }

    FString CollisionEnabledToString(ECollisionEnabled::Type Value)
    {
        switch (Value)
        {
        case ECollisionEnabled::NoCollision:       return TEXT("NoCollision");
        case ECollisionEnabled::QueryOnly:         return TEXT("QueryOnly");
        case ECollisionEnabled::PhysicsOnly:       return TEXT("PhysicsOnly");
        case ECollisionEnabled::QueryAndPhysics:   return TEXT("QueryAndPhysics");
        case ECollisionEnabled::ProbeOnly:         return TEXT("ProbeOnly");
        case ECollisionEnabled::QueryAndProbe:     return TEXT("QueryAndProbe");
        default:                                   return TEXT("Unknown");
        }
    }

    bool WriteJsonToFile(const TSharedRef<FJsonObject>& Root, const FString& FilePath)
    {
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        if (!FJsonSerializer::Serialize(Root, Writer))
        {
            return false;
        }

        IFileManager& FileManager = IFileManager::Get();
        const FString Dir = FPaths::GetPath(FilePath);
        if (!FileManager.DirectoryExists(*Dir))
        {
            FileManager.MakeDirectory(*Dir, /*Tree=*/ true);
        }

        return FFileHelper::SaveStringToFile(Output, *FilePath);
    }
}

FString FMapUtilsContextExporter::MakeOutputPath(const FString& Topic, UWorld* World)
{
    const FString LevelName = World ? World->GetMapName() : TEXT("unknown");
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    const FString FileName = FString::Printf(TEXT("%s-%s-%s.json"), *Topic, *LevelName, *Timestamp);
    return FPaths::ProjectIntermediateDir() / TEXT("MapUtilsContext") / FileName;
}

FMapUtilsContextExportResult FMapUtilsContextExporter::ExportStaticMeshContext(UWorld* World)
{
    FMapUtilsContextExportResult Result;

    if (!World)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ExportStaticMeshContext: World null."));
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Actors;

    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("Name"), Actor->GetName());
        Item->SetStringField(TEXT("Path"), Actor->GetPathName());

        if (ULevel* Level = Actor->GetLevel())
        {
            Item->SetStringField(TEXT("Level"), Level->GetOutermost()->GetName());
        }

        Item->SetObjectField(TEXT("Location"), VectorToJson(Actor->GetActorLocation()));
        Item->SetObjectField(TEXT("Rotation"), RotatorToJson(Actor->GetActorRotation()));
        Item->SetObjectField(TEXT("Scale"), VectorToJson(Actor->GetActorScale3D()));

        UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
        if (MeshComp)
        {
            UStaticMesh* Mesh = MeshComp->GetStaticMesh();
            Item->SetStringField(TEXT("MeshPath"), Mesh ? Mesh->GetPathName() : FString());

            TArray<TSharedPtr<FJsonValue>> Materials;
            const int32 NumMaterials = MeshComp->GetNumMaterials();
            for (int32 i = 0; i < NumMaterials; ++i)
            {
                UMaterialInterface* Mat = MeshComp->GetMaterial(i);
                Materials.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : FString()));
            }
            Item->SetArrayField(TEXT("Materials"), Materials);

            if (Mesh)
            {
                FBox LocalBox = Mesh->GetBoundingBox();
                if (LocalBox.IsValid)
                {
                    FBox WorldBox = LocalBox.TransformBy(MeshComp->GetComponentTransform());
                    Item->SetObjectField(TEXT("WorldBounds"), BoundsToJson(WorldBox));
                }
            }
        }
        else
        {
            Item->SetStringField(TEXT("MeshPath"), FString());
        }

        Actors.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("ExporterVersion"), ExporterVersion);
    Root->SetStringField(TEXT("Topic"), TEXT("StaticMeshRefs"));
    Root->SetStringField(TEXT("Level"), World->GetMapName());
    Root->SetStringField(TEXT("Timestamp"), FDateTime::Now().ToIso8601());
    Root->SetArrayField(TEXT("Actors"), Actors);

    const FString OutputPath = MakeOutputPath(TEXT("staticmesh"), World);
    if (WriteJsonToFile(Root, OutputPath))
    {
        Result.bSuccess = true;
        Result.OutputPath = OutputPath;
        Result.ItemCount = Actors.Num();

        UE_LOG(LogMapUtils, Log, TEXT("ExportStaticMeshContext: wrote %d actors to %s"), Result.ItemCount, *OutputPath);
    }
    else
    {
        UE_LOG(LogMapUtils, Error, TEXT("ExportStaticMeshContext: failed to write %s"), *OutputPath);
    }

    return Result;
}

FMapUtilsContextExportResult FMapUtilsContextExporter::ExportCollisionContext(UWorld* World)
{
    FMapUtilsContextExportResult Result;

    if (!World)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ExportCollisionContext: World null."));
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Actors;

    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
        if (!MeshComp)
        {
            continue;
        }

        const ECollisionEnabled::Type CollisionEnabled = MeshComp->GetCollisionEnabled();
        if (CollisionEnabled == ECollisionEnabled::NoCollision)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("Name"), Actor->GetName());
        Item->SetStringField(TEXT("Path"), Actor->GetPathName());

        if (ULevel* Level = Actor->GetLevel())
        {
            Item->SetStringField(TEXT("Level"), Level->GetOutermost()->GetName());
        }

        Item->SetObjectField(TEXT("Location"), VectorToJson(Actor->GetActorLocation()));
        Item->SetBoolField(TEXT("bHidden"), Actor->IsHidden());
        Item->SetStringField(TEXT("CollisionProfile"), MeshComp->GetCollisionProfileName().ToString());
        Item->SetStringField(TEXT("CollisionEnabled"), CollisionEnabledToString(CollisionEnabled));

        UStaticMesh* Mesh = MeshComp->GetStaticMesh();
        Item->SetStringField(TEXT("MeshPath"), Mesh ? Mesh->GetPathName() : FString());

        if (Mesh)
        {
            FBox LocalBox = Mesh->GetBoundingBox();
            if (LocalBox.IsValid)
            {
                FBox WorldBox = LocalBox.TransformBy(MeshComp->GetComponentTransform());
                Item->SetObjectField(TEXT("WorldBounds"), BoundsToJson(WorldBox));
            }
        }

        Actors.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("ExporterVersion"), ExporterVersion);
    Root->SetStringField(TEXT("Topic"), TEXT("CollisionCandidates"));
    Root->SetStringField(TEXT("Level"), World->GetMapName());
    Root->SetStringField(TEXT("Timestamp"), FDateTime::Now().ToIso8601());
    Root->SetArrayField(TEXT("Actors"), Actors);

    const FString OutputPath = MakeOutputPath(TEXT("collision"), World);
    if (WriteJsonToFile(Root, OutputPath))
    {
        Result.bSuccess = true;
        Result.OutputPath = OutputPath;
        Result.ItemCount = Actors.Num();

        UE_LOG(LogMapUtils, Log, TEXT("ExportCollisionContext: wrote %d actors to %s"), Result.ItemCount, *OutputPath);
    }
    else
    {
        UE_LOG(LogMapUtils, Error, TEXT("ExportCollisionContext: failed to write %s"), *OutputPath);
    }

    return Result;
}

#undef LOCTEXT_NAMESPACE
