#include "Operations/MapUtilsBakeToMergedInstanceMeshOps.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsBakePreflight.h"
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

#define LOCTEXT_NAMESPACE "MapUtilsBakeToMergedInstanceMeshOps"

namespace
{
    struct FInstanceEntry
    {
        UStaticMeshComponent* SourceSmc = nullptr;  // representative for settings copy / equality
        FTransform WorldXf;
        bool bReverseCulling = false;
    };

    struct FGroup
    {
        UStaticMeshComponent* TemplateSmc = nullptr;
        bool bReverseCulling = false;
        TArray<FTransform> WorldXfs;
    };

    bool IsAcceptableSource(AActor* Actor)
    {
        if (!IsValid(Actor))
        {
            return false;
        }
        if (Actor->IsA<AStaticMeshActor>())
        {
            return true;
        }
        // Tag required even for AActor + ISMC; otherwise third-party ISM hosts get absorbed.
        return Actor->ActorHasTag(MapUtilsIsmBaked::Tag);
    }

    void HarvestFromComponent(AActor* OwningActor, UStaticMeshComponent* SMC, TArray<FInstanceEntry>& OutEntries, TArray<FMapUtilsBakeProfileSample>& OutSamples)
    {
        if (!SMC || !SMC->GetStaticMesh())
        {
            return;
        }

        FMapUtilsBakeProfileSample Sample;
        Sample.ProfileName = SMC->GetCollisionProfileName();
        Sample.CollisionEnabled = SMC->GetCollisionEnabled();
        Sample.DisplayName = OwningActor->GetName();
        OutSamples.Add(Sample);

        if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
        {
            const int32 Count = ISMC->GetInstanceCount();
            for (int32 i = 0; i < Count; ++i)
            {
                FTransform InstanceWorldXf;
                if (!ISMC->GetInstanceTransform(i, InstanceWorldXf, /*bWorldSpace*/ true))
                {
                    continue;
                }

                FInstanceEntry Entry;
                Entry.SourceSmc = SMC;
                Entry.WorldXf = InstanceWorldXf;
                Entry.bReverseCulling = InstanceWorldXf.ToMatrixWithScale().Determinant() < 0.0;
                OutEntries.Add(MoveTemp(Entry));
            }
        }
        else
        {
            FInstanceEntry Entry;
            Entry.SourceSmc = SMC;
            Entry.WorldXf = SMC->GetComponentTransform();
            Entry.bReverseCulling = Entry.WorldXf.ToMatrixWithScale().Determinant() < 0.0;
            OutEntries.Add(MoveTemp(Entry));
        }
    }
}

FMapUtilsBakeMergedInstanceResult FMapUtilsBakeToMergedInstanceMeshOps::BakeToMergedInstanceMesh(const TArray<AActor*>& Actors, UMaterialInterface* OverrideMaterial)
{
    FMapUtilsBakeMergedInstanceResult Result;
    const bool bHasOverrideMaterial = (OverrideMaterial != nullptr);

    TArray<AActor*> AcceptedSources;
    TArray<FInstanceEntry> Entries;
    TArray<FMapUtilsBakeProfileSample> Samples;
    TSet<ULevel*> UniqueLevels;

    for (AActor* Actor : Actors)
    {
        if (!IsAcceptableSource(Actor))
        {
            if (IsValid(Actor))
            {
                ++Result.SkippedActorCount;
                UE_LOG(LogMapUtils, Warning, TEXT("BakeToMergedInstanceMesh: skipping %s (class %s, not SMA or ISMActor)"), *Actor->GetName(), *Actor->GetClass()->GetName());
            }
            continue;
        }

        TArray<UStaticMeshComponent*> SMCs;
        Actor->GetComponents<UStaticMeshComponent>(SMCs);

        const int32 BeforeEntries = Entries.Num();
        for (UStaticMeshComponent* SMC : SMCs)
        {
            HarvestFromComponent(Actor, SMC, Entries, Samples);
        }

        if (Entries.Num() > BeforeEntries)
        {
            AcceptedSources.Add(Actor);
            UniqueLevels.Add(Actor->GetLevel());
        }
    }

    if (Entries.IsEmpty())
    {
        Result.ErrorText = LOCTEXT("NoValid", "No valid mesh source in selection. Select StaticMeshActors or previously-baked ISMActors.");
        return Result;
    }

    // Single already-baked ISM actor -> no-op. Output would be structurally identical to input;
    // warn instead of silently re-creating the same actor.
    if (AcceptedSources.Num() == 1 && !AcceptedSources[0]->IsA<AStaticMeshActor>())
    {
        Result.ErrorText = LOCTEXT("AlreadyMerged", "Selection is a single already-baked ISM actor. Nothing to merge. Add more StaticMeshActors / ISMActors to the selection, or use Bake to Instance Mesh on a fresh SMA instead.");
        return Result;
    }

    if (UniqueLevels.Num() != 1)
    {
        Result.ErrorText = LOCTEXT("CrossLevel", "Selected sources must all live in the same level.");
        return Result;
    }

    UWorld* World = AcceptedSources[0]->GetWorld();
    ULevel* Level = *UniqueLevels.CreateConstIterator();
    if (!World || !Level || !World->IsEditorWorld())
    {
        Result.ErrorText = LOCTEXT("BadWorld", "Invalid editor world.");
        return Result;
    }

    // Profile divergence is logged only; the modal preflight was too intrusive in regular use.
    // Per-source profile is still preserved by ISMC group splitting via
    // MapUtilsComponentSettings::AreGroupableSettingsEqual + Copy.
    {
        TMap<FName, int32> ProfileCounts;
        for (const FMapUtilsBakeProfileSample& S : Samples)
        {
            ProfileCounts.FindOrAdd(S.ProfileName)++;
        }
        if (ProfileCounts.Num() > 1)
        {
            TArray<FString> Parts;
            Parts.Reserve(ProfileCounts.Num());
            for (const TPair<FName, int32>& Pair : ProfileCounts)
            {
                Parts.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key.ToString(), Pair.Value));
            }
            UE_LOG(LogMapUtils, Warning, TEXT("BakeToMergedInstanceMesh: %d distinct collision profile(s) in selection [%s]; per-source profile preserved by ISMC group splitting."), ProfileCounts.Num(), *FString::Join(Parts, TEXT(", ")));
        }
    }

    // AABB-center pivot: union of all instance world-space mesh bounds.
    // Density-independent (a clustered subset will not drag the pivot off-center).
    FBox WorldBounds(ForceInit);
    for (const FInstanceEntry& Entry : Entries)
    {
        UStaticMesh* Mesh = Entry.SourceSmc ? Entry.SourceSmc->GetStaticMesh() : nullptr;
        const FBox LocalBounds = Mesh ? Mesh->GetBoundingBox() : FBox(Entry.WorldXf.GetLocation(), Entry.WorldXf.GetLocation());
        WorldBounds += LocalBounds.TransformBy(Entry.WorldXf);
    }
    const FTransform PivotXf(FRotator::ZeroRotator, WorldBounds.GetCenter());

    // Group instances by (groupable-settings equality, reverse culling).
    // Linear search; group counts stay small because most placements share settings.
    TArray<FGroup> Groups;
    for (const FInstanceEntry& Entry : Entries)
    {
        const int32 Idx = Groups.IndexOfByPredicate([&](const FGroup& G)
        {
            return G.bReverseCulling == Entry.bReverseCulling
                && MapUtilsComponentSettings::AreGroupableSettingsEqual(G.TemplateSmc, Entry.SourceSmc, bHasOverrideMaterial);
        });
        if (Idx == INDEX_NONE)
        {
            FGroup G;
            G.TemplateSmc = Entry.SourceSmc;
            G.bReverseCulling = Entry.bReverseCulling;
            G.WorldXfs.Add(Entry.WorldXf);
            Groups.Add(MoveTemp(G));
        }
        else
        {
            Groups[Idx].WorldXfs.Add(Entry.WorldXf);
        }
    }

    Result.SourceActorCount = AcceptedSources.Num();
    Result.InstanceCount = Entries.Num();
    Result.GroupCount = Groups.Num();

    {
        FScopedTransaction Transaction(LOCTEXT("BakeToMergedInstanceMesh", "Bake Selected to Merged Instance Mesh"));
        Level->Modify();

        FActorSpawnParameters Params;
        Params.OverrideLevel = Level;

        AActor* MergedActor = World->SpawnActor<AActor>(AActor::StaticClass(), PivotXf, Params);
        if (!MergedActor)
        {
            Result.ErrorText = LOCTEXT("SpawnFailed", "Failed to spawn merged ISM actor.");
            return Result;
        }
        MergedActor->Modify();

        USceneComponent* Root = nullptr;

        for (const FGroup& Group : Groups)
        {
            UInstancedStaticMeshComponent* Ismc = NewObject<UInstancedStaticMeshComponent>(MergedActor, NAME_None, RF_Transactional);
            Ismc->Modify();
            Ismc->bHasPerInstanceHitProxies = true;
            Ismc->SetMobility(EComponentMobility::Static);
            Ismc->SetStaticMesh(Group.TemplateSmc->GetStaticMesh());

            // Full settings migration from the representative source component.
            MapUtilsComponentSettings::Copy(Group.TemplateSmc, Ismc);

            // ToolSetup override: stamp the chosen material onto every slot, after Copy so it wins.
            if (bHasOverrideMaterial)
            {
                const int32 SlotCount = Ismc->GetNumMaterials();
                for (int32 SlotIdx = 0; SlotIdx < SlotCount; ++SlotIdx)
                {
                    Ismc->SetMaterial(SlotIdx, OverrideMaterial);
                }
            }

            Ismc->SetReverseCulling(Group.bReverseCulling);

            if (Root == nullptr)
            {
                MergedActor->SetRootComponent(Ismc);
                Root = Ismc;
            }
            else
            {
                Ismc->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
            }

            MergedActor->RemoveOwnedComponent(Ismc);
            Ismc->CreationMethod = EComponentCreationMethod::Instance;
            MergedActor->AddOwnedComponent(Ismc);
            Ismc->RegisterComponent();

            // SetRootComponent with a fresh (identity) ISMC snaps the actor to (0,0,0);
            // restore the centroid pivot so instance-relative transforms resolve correctly.
            if (Root == Ismc)
            {
                MergedActor->SetActorTransform(PivotXf);
            }

            for (const FTransform& InstanceWorldXf : Group.WorldXfs)
            {
                const FTransform InstanceLocalXf = InstanceWorldXf.GetRelativeTransform(PivotXf);
                Ismc->AddInstance(InstanceLocalXf, /*bWorldSpace*/ false);
            }
        }

        MapUtilsIsmBaked::TagAndLabel(MergedActor);
        MergedActor->PostEditChange();

        // Deselect sources while still valid. SelectNone after EditorDestroyActor would
        // iterate Garbage-flagged actors and emit "invalid flags" warnings per source.
        if (GEditor)
        {
            GEditor->SelectNone(true, true);
        }

        for (AActor* Source : AcceptedSources)
        {
            Source->Modify();
            World->EditorDestroyActor(Source, true);
        }

        if (GEditor)
        {
            GEditor->SelectActor(MergedActor, true, true);
        }
    }

    Result.bSuccess = true;

    const FString OverrideMatName = bHasOverrideMaterial ? OverrideMaterial->GetName() : FString(TEXT("<none>"));
    UE_LOG(LogMapUtils, Log, TEXT("BakeToMergedInstanceMesh: %d source(s) -> 1 actor with %d instances across %d ISMC group(s) (skipped %d, overrideMat=%s)"), Result.SourceActorCount, Result.InstanceCount, Result.GroupCount, Result.SkippedActorCount, *OverrideMatName);

    // Per-group diagnostic. When the user expects 1 group but sees N, this surfaces the splitting reason
    // (mesh / collision profile / reverse culling). Material divergence is omitted when overridden.
    for (int32 GIdx = 0; GIdx < Groups.Num(); ++GIdx)
    {
        const FGroup& G = Groups[GIdx];
        UStaticMesh* Mesh = G.TemplateSmc ? G.TemplateSmc->GetStaticMesh() : nullptr;
        const FString MeshName = Mesh ? Mesh->GetName() : FString(TEXT("None"));
        const FString ProfileName = G.TemplateSmc ? G.TemplateSmc->GetCollisionProfileName().ToString() : FString(TEXT("None"));
        const int32 ReverseCullingFlag = G.bReverseCulling ? 1 : 0;
        UE_LOG(LogMapUtils, Log, TEXT("  Group[%d]: mesh=%s instances=%d profile=%s reverseCulling=%d"), GIdx, *MeshName, G.WorldXfs.Num(), *ProfileName, ReverseCullingFlag);
    }

    return Result;
}

#undef LOCTEXT_NAMESPACE
