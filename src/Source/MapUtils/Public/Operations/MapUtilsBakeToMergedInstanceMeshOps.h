#pragma once

#include "CoreMinimal.h"

class AActor;

struct FMapUtilsBakeMergedInstanceResult
{
    int32 SourceActorCount = 0;
    int32 InstanceCount = 0;
    int32 GroupCount = 0;
    int32 SkippedActorCount = 0;
    bool bSuccess = false;
    bool bUserCancelled = false;
    FText ErrorText;
};

/**
 * Merge selected sources (StaticMeshActors + previously-baked ISMActors) into a single
 * ISM-hosting actor at the centroid of all instances. Different (mesh, materials, collision)
 * tuples become separate ISMC inside one actor. Pivot stays at centroid so the merged
 * actor is reasonable to manipulate. Source actors destroyed.
 *
 * Acceptable inputs:
 *  - AStaticMeshActor (1 instance per actor)
 *  - AActor whose root is UInstancedStaticMeshComponent (N instances per actor)
 *
 * Anything else (Blueprint actors with logic, etc.) is skipped to avoid clobbering scripted
 * actors. Designed to replace UE's Group Actor workflow for static decoration.
 */
class FMapUtilsBakeToMergedInstanceMeshOps
{
public:
    static FMapUtilsBakeMergedInstanceResult BakeToMergedInstanceMesh(
        const TArray<AActor*>& Actors);
};
