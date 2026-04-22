#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class ABlockingVolume;

struct FMapUtilsBlockingVolumeConvertResult
{
    TArray<ABlockingVolume*> CreatedVolumes;
    TArray<FString> DestroyedActorNames;
    int32 ClusterCount = 0;
    bool bSuccess = false;
};

class FMapUtilsBlockingVolumeOps
{
public:
    static constexpr float DefaultToleranceUnits = 10.0f;

    static FMapUtilsBlockingVolumeConvertResult ConvertActorsToBlockingVolumes(
        const TArray<AStaticMeshActor*>& Actors,
        float ToleranceUnits = DefaultToleranceUnits);
};
