#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * Shared "ISM_Baked" actor labelling. Bake ops and any external pipeline that
 * opts in tag their outputs identically so the merge filter accepts all of them.
 *
 * Also centralises outliner-folder routing: every baked actor lands in the outliner's
 * "Make Current Folder" if set, otherwise in the caller-provided fallback (typically
 * the builder's or source actor's folder), otherwise stays at root.
 */
namespace IsmBaked
{
    /** Tag applied to baked ISM-hosting actors. Used to whitelist them as merge inputs. */
    extern const FName Tag;

    /** Returns the next free "ISM_Baked_N" index by scanning existing actor labels in the world. */
    int32 PeekNextLabelIndex(UWorld* World);

    /**
     * Apply Tag, a fresh "ISM_Baked_N" label, and folder routing.
     * FallbackFolderPath is used when the outliner's "Make Current Folder" is unset; pass
     * NAME_None to leave the actor at root in that case.
     */
    void TagAndLabel(AActor* Actor, FName FallbackFolderPath = NAME_None);

    /**
     * Apply Tag, "ISM_Baked_<Index>" label, and folder routing; returns the next index.
     * Cheap path for callers that bake N actors in a row: scan world once, increment locally.
     */
    int32 TagAndLabelWithIndex(AActor* Actor, int32 Index, FName FallbackFolderPath = NAME_None);
}
