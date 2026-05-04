#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

struct FMapUtilsBakeProfileSample
{
    FName ProfileName = NAME_None;
    ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
    FString DisplayName;
};

/**
 * Pre-bake validation shared by Bake-to-Instance and Bake-to-Merged operations.
 * Surfaces divergence to the user instead of silently collapsing it during bake.
 */
class FMapUtilsBakePreflight
{
public:
    /**
     * Diffs CollisionProfileName + CollisionEnabled across the supplied per-component samples.
     * Returns true silently when uniform; on divergence shows a modal listing each group and
     * returns the user's choice (OK -> true, Cancel -> false).
     */
    static bool ConfirmBodyInstanceProfileUniformity(const TArray<FMapUtilsBakeProfileSample>& Samples);
};
