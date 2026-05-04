#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * Shared "ISM_Baked" actor labelling. Bake ops and any external pipeline that
 * opts in tag their outputs identically so the merge filter accepts all of them.
 */
namespace MapUtilsIsmBaked
{
    /** Tag applied to baked ISM-hosting actors. Used to whitelist them as merge inputs. */
    extern const FName Tag;

    /** Returns the next free "ISM_Baked_N" index by scanning existing actor labels in the world. */
    int32 PeekNextLabelIndex(UWorld* World);

    /** Apply Tag and a fresh "ISM_Baked_N" label using one world scan. */
    void TagAndLabel(AActor* Actor);

    /**
     * Apply Tag and a "ISM_Baked_<StartIndex>" label, returning the next index to use.
     * Cheap path for callers that bake N actors in a row: scan world once, increment locally.
     */
    int32 TagAndLabelWithIndex(AActor* Actor, int32 Index);
}
