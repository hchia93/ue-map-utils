#include "Operations/MapUtilsFixBakedIsmRotationOps.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsIsmBakedTag.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsFixBakedIsmRotationOps"

namespace
{
    constexpr float QuatTol = 0.001f;

    struct FRotationBucket
    {
        FQuat Rep = FQuat::Identity;
        int32 Count = 0;
    };

    // Pool all instance rotations across every ISMC on the actor, bucket by tolerance,
    // return the most frequent rotation. Reject ties (no strict plurality) and Identity
    // (nothing to extract) so the caller skips those actors with an explicit reason.
    bool FindDominantInstanceRotation(const TArray<UInstancedStaticMeshComponent*>& Components, FQuat& OutRotation, FString& OutSkipReason)
    {
        TArray<FRotationBucket> Buckets;
        int32 TotalInstances = 0;

        for (UInstancedStaticMeshComponent* ISMC : Components)
        {
            if (!ISMC)
            {
                continue;
            }
            const int32 Count = ISMC->GetInstanceCount();
            for (int32 i = 0; i < Count; ++i)
            {
                FTransform Local;
                ISMC->GetInstanceTransform(i, Local, /*bWorldSpace*/ false);
                const FQuat Q = Local.GetRotation();

                int32 FoundIdx = INDEX_NONE;
                for (int32 b = 0; b < Buckets.Num(); ++b)
                {
                    if (Buckets[b].Rep.Equals(Q, QuatTol))
                    {
                        FoundIdx = b;
                        break;
                    }
                }
                if (FoundIdx != INDEX_NONE)
                {
                    Buckets[FoundIdx].Count++;
                }
                else
                {
                    Buckets.Add({ Q, 1 });
                }
                ++TotalInstances;
            }
        }

        if (TotalInstances == 0)
        {
            OutSkipReason = TEXT("no instances on any ISMC");
            return false;
        }

        int32 MaxIdx = INDEX_NONE;
        int32 MaxCount = 0;
        int32 SecondCount = 0;
        for (int32 b = 0; b < Buckets.Num(); ++b)
        {
            if (Buckets[b].Count > MaxCount)
            {
                SecondCount = MaxCount;
                MaxCount = Buckets[b].Count;
                MaxIdx = b;
            }
            else if (Buckets[b].Count > SecondCount)
            {
                SecondCount = Buckets[b].Count;
            }
        }

        if (MaxIdx == INDEX_NONE)
        {
            OutSkipReason = TEXT("rotation bucketing produced no result");
            return false;
        }
        if (MaxCount == SecondCount)
        {
            OutSkipReason = FString::Printf(TEXT("ambiguous rotation distribution (no strict plurality across %d instances)"), TotalInstances);
            return false;
        }

        const FQuat R = Buckets[MaxIdx].Rep;
        if (R.Equals(FQuat::Identity, QuatTol))
        {
            OutSkipReason = TEXT("dominant instance rotation already Identity; nothing to extract");
            return false;
        }

        OutRotation = R;
        return true;
    }
}

FMapUtilsFixBakedIsmRotationResult FMapUtilsFixBakedIsmRotationOps::Fix(const TArray<AActor*>& Actors)
{
    FMapUtilsFixBakedIsmRotationResult Result;
    Result.SelectedActorCount = Actors.Num();

    if (Actors.IsEmpty())
    {
        Result.ErrorText = LOCTEXT("NoSelection", "No actor selected.");
        return Result;
    }

    FScopedTransaction Transaction(LOCTEXT("FixBakedIsmRotation", "Fix Baked ISM Rotation"));

    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor))
        {
            ++Result.SkippedActorCount;
            continue;
        }

        const FString ActorName = Actor->GetActorLabel();

        if (!Actor->Tags.Contains(MapUtilsIsmBaked::Tag))
        {
            Result.SkipReasons.Add(FString::Printf(TEXT("%s: not tagged ISM_Baked"), *ActorName));
            ++Result.SkippedActorCount;
            continue;
        }

        const FQuat ActorQuat = Actor->GetActorQuat();
        if (!ActorQuat.Equals(FQuat::Identity, QuatTol))
        {
            Result.SkipReasons.Add(FString::Printf(TEXT("%s: actor rotation already non-Identity (probably already fixed)"), *ActorName));
            ++Result.SkippedActorCount;
            continue;
        }

        TArray<UInstancedStaticMeshComponent*> ISMCs;
        Actor->GetComponents<UInstancedStaticMeshComponent>(ISMCs);
        if (ISMCs.IsEmpty())
        {
            Result.SkipReasons.Add(FString::Printf(TEXT("%s: no ISMC component found"), *ActorName));
            ++Result.SkippedActorCount;
            continue;
        }

        FQuat R;
        FString SkipReason;
        if (!FindDominantInstanceRotation(ISMCs, R, SkipReason))
        {
            Result.SkipReasons.Add(FString::Printf(TEXT("%s: %s"), *ActorName, *SkipReason));
            ++Result.SkippedActorCount;
            continue;
        }

        // Each instance's world transform must stay unchanged. UE FTransform composition is
        // InstanceWorld = InstanceLocal * ComponentToWorld (left-of-right, i.e. apply local then parent).
        // Shifting ComponentToWorld rotation from Identity to R demands NewLocal = OldLocal * R^-1
        // so that NewLocal * (R, P) reproduces the original OldLocal * (Identity, P). The R^-1 must
        // sit on the RIGHT to also de-rotate the local translation; putting it on the left only
        // touches rotation and visibly orbits every instance around the actor pivot by R.
        const FTransform RInv(R.Inverse());

        Actor->Modify();

        FTransform NewActorXf = Actor->GetActorTransform();
        NewActorXf.SetRotation(R);
        Actor->SetActorTransform(NewActorXf);

        for (UInstancedStaticMeshComponent* ISMC : ISMCs)
        {
            ISMC->Modify();
            const int32 Count = ISMC->GetInstanceCount();
            for (int32 i = 0; i < Count; ++i)
            {
                FTransform OldLocal;
                ISMC->GetInstanceTransform(i, OldLocal, /*bWorldSpace*/ false);
                const FTransform NewLocal = OldLocal * RInv;
                ISMC->UpdateInstanceTransform(i, NewLocal, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
            }
            ISMC->MarkRenderStateDirty();
        }

        Actor->PostEditChange();

        ++Result.FixedActorCount;
    }

    Result.bSuccess = Result.FixedActorCount > 0 || Result.SkippedActorCount == Result.SelectedActorCount;

    UE_LOG(LogMapUtils, Log, TEXT("FixBakedIsmRotation: %d selected, %d fixed, %d skipped"),
        Result.SelectedActorCount, Result.FixedActorCount, Result.SkippedActorCount);

    return Result;
}

#undef LOCTEXT_NAMESPACE
