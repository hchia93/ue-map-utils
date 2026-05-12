#pragma once

#include "CoreMinimal.h"

class AActor;

struct FMapUtilsFixBakedIsmRotationResult
{
    int32 SelectedActorCount = 0;
    int32 FixedActorCount = 0;
    int32 SkippedActorCount = 0;
    TArray<FString> SkipReasons;
    bool bSuccess = false;
    FText ErrorText;
};

/**
 * Repair retroactive bake bugs where the builder rotation got stored on instance
 * local transforms instead of on the actor root. For each ISM_Baked actor with
 * Identity world rotation, find the dominant rotation across all instances of
 * all its ISMCs, set that as the actor's rotation, and bake the inverse into
 * every instance's local transform. Visual position is preserved; the actor's
 * Local gizmo gains a meaningful basis matching the original builder orientation.
 *
 * Idempotent: re-running on a fixed actor sees non-Identity actor rotation and skips.
 */
class FMapUtilsFixBakedIsmRotationOps
{
public:
    static FMapUtilsFixBakedIsmRotationResult Fix(const TArray<AActor*>& Actors);
};
