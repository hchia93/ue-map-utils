#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;

struct FBakeInstanceResult
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
 * Full component-settings migration via MigrateStaticMeshComponentSettingsOps.
 */
class FBakeToInstanceMeshOps
{
public:
    static FBakeInstanceResult BakeToInstanceMesh(const TArray<AStaticMeshActor*>& Actors);
};
