#include "Operations/MapUtilsBlockingVolumeOps.h"

#include "MapUtilsModule.h"

#include "Builders/CubeBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/BlockingVolume.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Model.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsBlockingVolumeOps"

namespace
{
    struct FCandidate
    {
        AStaticMeshActor* Actor = nullptr;
        FBox Bounds;
        int32 ClusterID = -1;
    };

    int32 FindRoot(TArray<int32>& Parents, int32 Index)
    {
        while (Parents[Index] != Index)
        {
            Parents[Index] = Parents[Parents[Index]];
            Index = Parents[Index];
        }
        return Index;
    }

    TArray<FCandidate> BuildCandidates(const TArray<AStaticMeshActor*>& Actors)
    {
        TArray<FCandidate> Candidates;
        Candidates.Reserve(Actors.Num());

        for (AStaticMeshActor* Actor : Actors)
        {
            if (!IsValid(Actor))
            {
                continue;
            }

            UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
            if (!MeshComp)
            {
                continue;
            }

            UStaticMesh* Mesh = MeshComp->GetStaticMesh();
            if (!Mesh)
            {
                UE_LOG(LogMapUtils, Warning,
                    TEXT("BuildCandidates: skipping %s (no StaticMesh)"), *Actor->GetName());
                continue;
            }

            FBox LocalBox = Mesh->GetBoundingBox();
            if (!LocalBox.IsValid)
            {
                continue;
            }

            // Editor world: component transform cache is valid (tick has run).
            FBox WorldBox = LocalBox.TransformBy(MeshComp->GetComponentTransform());

            FCandidate Candidate;
            Candidate.Actor = Actor;
            Candidate.Bounds = WorldBox;
            Candidates.Add(Candidate);
        }

        return Candidates;
    }

    void ClusterByProximity(TArray<FCandidate>& Candidates, float Tolerance)
    {
        const int32 Count = Candidates.Num();
        if (Count <= 1)
        {
            for (int32 i = 0; i < Count; i++)
            {
                Candidates[i].ClusterID = i;
            }
            return;
        }

        TArray<int32> Parents;
        TArray<int32> Ranks;
        Parents.SetNum(Count);
        Ranks.SetNum(Count);

        for (int32 i = 0; i < Count; i++)
        {
            Parents[i] = i;
            Ranks[i] = 0;
        }

        for (int32 i = 0; i < Count; i++)
        {
            const FBox Expanded = Candidates[i].Bounds.ExpandBy(Tolerance);

            for (int32 j = i + 1; j < Count; j++)
            {
                const FBox& Other = Candidates[j].Bounds;

                const bool bOverlap =
                    Expanded.Min.X <= Other.Max.X && Expanded.Max.X >= Other.Min.X &&
                    Expanded.Min.Y <= Other.Max.Y && Expanded.Max.Y >= Other.Min.Y &&
                    Expanded.Min.Z <= Other.Max.Z && Expanded.Max.Z >= Other.Min.Z;

                if (bOverlap)
                {
                    const int32 RootI = FindRoot(Parents, i);
                    const int32 RootJ = FindRoot(Parents, j);
                    if (RootI != RootJ)
                    {
                        if (Ranks[RootI] < Ranks[RootJ])
                        {
                            Parents[RootI] = RootJ;
                        }
                        else if (Ranks[RootI] > Ranks[RootJ])
                        {
                            Parents[RootJ] = RootI;
                        }
                        else
                        {
                            Parents[RootJ] = RootI;
                            Ranks[RootI]++;
                        }
                    }
                }
            }
        }

        for (int32 i = 0; i < Count; i++)
        {
            Candidates[i].ClusterID = FindRoot(Parents, i);
        }
    }

    ABlockingVolume* CreateBlockingVolume(UWorld* World, ULevel* TargetLevel, const FBox& Bounds)
    {
        const FVector Center = Bounds.GetCenter();
        const FVector Size = Bounds.GetSize();

        FActorSpawnParameters SpawnParams;
        SpawnParams.OverrideLevel = TargetLevel;

        ABlockingVolume* Volume = World->SpawnActor<ABlockingVolume>(
            ABlockingVolume::StaticClass(), FTransform(Center), SpawnParams);
        if (!Volume)
        {
            UE_LOG(LogMapUtils, Error, TEXT("SpawnActor<ABlockingVolume> failed."));
            return nullptr;
        }

        Volume->Modify();

        UModel* BrushModel = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
        BrushModel->Initialize(Volume, true);
        Volume->Brush = BrushModel;

        UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
        Builder->X = Size.X;
        Builder->Y = Size.Y;
        Builder->Z = Size.Z;
        Volume->BrushBuilder = Builder;
        Builder->Build(World, Volume);

        Volume->SetActorLocation(Center);
        Volume->PostEditChange();

        return Volume;
    }
}

FMapUtilsBlockingVolumeConvertResult FMapUtilsBlockingVolumeOps::ConvertActorsToBlockingVolumes(
    const TArray<AStaticMeshActor*>& Actors,
    float ToleranceUnits)
{
    FMapUtilsBlockingVolumeConvertResult Result;

    if (!GEditor)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ConvertActorsToBlockingVolumes: GEditor null."));
        return Result;
    }

    TArray<FCandidate> Candidates = BuildCandidates(Actors);
    if (Candidates.IsEmpty())
    {
        UE_LOG(LogMapUtils, Warning,
            TEXT("ConvertActorsToBlockingVolumes: no valid AStaticMeshActor with mesh in input."));
        return Result;
    }

    UWorld* World = Candidates[0].Actor->GetWorld();
    ULevel* TargetLevel = Candidates[0].Actor->GetLevel();
    if (!World || !TargetLevel)
    {
        UE_LOG(LogMapUtils, Warning,
            TEXT("ConvertActorsToBlockingVolumes: first actor has no world or level."));
        return Result;
    }

    if (!World->IsEditorWorld())
    {
        UE_LOG(LogMapUtils, Warning,
            TEXT("ConvertActorsToBlockingVolumes: not editor world, aborting."));
        return Result;
    }

    ClusterByProximity(Candidates, ToleranceUnits);

    TMap<int32, TArray<int32>> Clusters;
    for (int32 i = 0; i < Candidates.Num(); i++)
    {
        Clusters.FindOrAdd(Candidates[i].ClusterID).Add(i);
    }

    Result.ClusterCount = Clusters.Num();

    // Atomic undo: single Ctrl+Z reverts spawn + destroy for all clusters.
    FScopedTransaction Transaction(LOCTEXT("ConvertToBlockingVolumes", "Convert to Blocking Volumes"));

    TargetLevel->Modify();

    for (const TPair<int32, TArray<int32>>& Pair : Clusters)
    {
        FBox ClusterBounds(ForceInit);
        for (int32 Idx : Pair.Value)
        {
            ClusterBounds += Candidates[Idx].Bounds;
        }

        ABlockingVolume* Volume = CreateBlockingVolume(World, TargetLevel, ClusterBounds);
        if (!Volume)
        {
            continue;
        }

        Result.CreatedVolumes.Add(Volume);

        for (int32 Idx : Pair.Value)
        {
            AStaticMeshActor* SMA = Candidates[Idx].Actor;
            if (!IsValid(SMA))
            {
                continue;
            }

            SMA->Modify();
            Result.DestroyedActorNames.Add(SMA->GetName());
            World->EditorDestroyActor(SMA, true);
        }
    }

    Result.bSuccess = !Result.CreatedVolumes.IsEmpty();

    UE_LOG(LogMapUtils, Log,
        TEXT("ConvertActorsToBlockingVolumes: %d actors -> %d volumes across %d clusters (level: %s)"),
        Result.DestroyedActorNames.Num(), Result.CreatedVolumes.Num(), Result.ClusterCount,
        *TargetLevel->GetOutermost()->GetName());

    return Result;
}

#undef LOCTEXT_NAMESPACE
