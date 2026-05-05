#include "Operations/MapUtilsBakeToInstanceMeshOps.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsComponentSettings.h"
#include "Operations/MapUtilsIsmBakedTag.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsBakeToInstanceMeshOps"

namespace
{
    AActor* SpawnIsmActorForSource(UWorld* World, ULevel* Level, AStaticMeshActor* Source, int32 LabelIndex, UMaterialInterface* OverrideMaterial)
    {
        UStaticMeshComponent* Src = Source->GetStaticMeshComponent();
        if (!Src || !Src->GetStaticMesh())
        {
            return nullptr;
        }

        const FTransform ActorXf = Source->GetActorTransform();

        FActorSpawnParameters Params;
        Params.OverrideLevel = Level;

        AActor* Out = World->SpawnActor<AActor>(AActor::StaticClass(), ActorXf, Params);
        if (!Out)
        {
            return nullptr;
        }
        Out->Modify();

        UInstancedStaticMeshComponent* Ismc = NewObject<UInstancedStaticMeshComponent>(Out, NAME_None, RF_Transactional);
        Ismc->Modify();
        Ismc->bHasPerInstanceHitProxies = true;
        Ismc->SetMobility(EComponentMobility::Static);
        Ismc->SetStaticMesh(Src->GetStaticMesh());

        // Full settings migration before root assignment so register picks up the right state.
        MapUtilsComponentSettings::Copy(Src, Ismc);

        // ToolSetup override: stamp the chosen material onto every slot, after Copy so it wins.
        if (OverrideMaterial)
        {
            const int32 SlotCount = Ismc->GetNumMaterials();
            for (int32 SlotIdx = 0; SlotIdx < SlotCount; ++SlotIdx)
            {
                Ismc->SetMaterial(SlotIdx, OverrideMaterial);
            }
        }

        // Either an explicit source flag or a mirrored actor (det<0) inverts winding; OR keeps both.
        const bool bSrcReverse = Src->bReverseCulling;
        const bool bDeterminantReverse = ActorXf.ToMatrixWithScale().Determinant() < 0.0;
        Ismc->SetReverseCulling(bSrcReverse || bDeterminantReverse);

        Out->SetRootComponent(Ismc);
        Out->RemoveOwnedComponent(Ismc);
        Ismc->CreationMethod = EComponentCreationMethod::Instance;
        Out->AddOwnedComponent(Ismc);
        Ismc->RegisterComponent();

        // SetRootComponent on a fresh ISMC (identity transform) snaps the actor to (0,0,0);
        // restore the source transform so the new actor sits where the old SMA stood.
        Out->SetActorTransform(ActorXf);

        Ismc->AddInstance(FTransform::Identity, /*bWorldSpace*/ false);

        MapUtilsIsmBaked::TagAndLabelWithIndex(Out, LabelIndex);

        Out->PostEditChange();
        return Out;
    }
}

FMapUtilsBakeInstanceResult FMapUtilsBakeToInstanceMeshOps::BakeToInstanceMesh(const TArray<AStaticMeshActor*>& Actors, UMaterialInterface* OverrideMaterial)
{
    FMapUtilsBakeInstanceResult Result;

    TArray<AStaticMeshActor*> Valid;
    TSet<ULevel*> UniqueLevels;

    for (AStaticMeshActor* Actor : Actors)
    {
        if (!IsValid(Actor))
        {
            continue;
        }
        UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
        if (!MeshComp || !MeshComp->GetStaticMesh())
        {
            UE_LOG(LogMapUtils, Warning, TEXT("BakeToInstanceMesh: skipping %s (no mesh)"), *Actor->GetName());
            continue;
        }
        Valid.Add(Actor);
        UniqueLevels.Add(Actor->GetLevel());
    }

    if (Valid.IsEmpty())
    {
        Result.ErrorText = LOCTEXT("NoValid", "No valid StaticMeshActor with mesh in selection.");
        return Result;
    }

    if (UniqueLevels.Num() != 1)
    {
        Result.ErrorText = LOCTEXT("CrossLevel", "Selected actors must all live in the same level.");
        return Result;
    }

    UWorld* World = Valid[0]->GetWorld();
    ULevel* Level = *UniqueLevels.CreateConstIterator();
    if (!World || !Level || !World->IsEditorWorld())
    {
        Result.ErrorText = LOCTEXT("BadWorld", "Invalid editor world.");
        return Result;
    }

    Result.SourceActorCount = Valid.Num();

    // Single world scan up-front; increment locally per spawn.
    int32 NextLabelIdx = MapUtilsIsmBaked::PeekNextLabelIndex(World);

    {
        FScopedTransaction Transaction(LOCTEXT("BakeToInstanceMesh", "Bake Selected to Instance Mesh"));
        Level->Modify();

        // Deselect sources while still valid. SelectNone after EditorDestroyActor would emit
        // "invalid flags" warnings (Garbage internal flag) per destroyed source.
        if (GEditor)
        {
            GEditor->SelectNone(true, true);
        }

        for (AStaticMeshActor* Source : Valid)
        {
            AActor* New = SpawnIsmActorForSource(World, Level, Source, NextLabelIdx, OverrideMaterial);
            if (!New)
            {
                Result.FailedSourceNames.Add(Source->GetName());
                UE_LOG(LogMapUtils, Warning, TEXT("BakeToInstanceMesh: spawn failed for %s"), *Source->GetName());
                continue;
            }
            ++NextLabelIdx;
            ++Result.CreatedActorCount;

            Source->Modify();
            World->EditorDestroyActor(Source, true);
        }
    }

    Result.bSuccess = Result.CreatedActorCount > 0;

    UE_LOG(LogMapUtils, Log, TEXT("BakeToInstanceMesh (1:1): %d source(s) -> %d ISM actor(s), %d failed"), Result.SourceActorCount, Result.CreatedActorCount, Result.FailedSourceNames.Num());

    return Result;
}

#undef LOCTEXT_NAMESPACE
