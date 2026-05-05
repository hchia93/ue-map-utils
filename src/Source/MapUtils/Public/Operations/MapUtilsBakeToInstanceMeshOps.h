#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UMaterialInterface;

struct FMapUtilsBakeInstanceResult
{
    int32 SourceActorCount = 0;
    int32 CreatedActorCount = 0;
    TArray<FString> FailedSourceNames;
    bool bSuccess = false;
    FText ErrorText;
};

/**
 * Replace each selected StaticMeshActor with its own ISMC-hosting actor (1 instance each).
 * No grouping, no merging: N selected -> N output actors. Source actors destroyed.
 * Full component-settings migration via MapUtilsComponentSettings.
 */
class FMapUtilsBakeToInstanceMeshOps
{
public:
    /**
     * @param OverrideMaterial  When non-null, every ISMC slot on each spawned actor is forced to this
     *                          material after the source-component settings have been migrated.
     */
    static FMapUtilsBakeInstanceResult BakeToInstanceMesh(const TArray<AStaticMeshActor*>& Actors, UMaterialInterface* OverrideMaterial = nullptr);
};
