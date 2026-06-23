#pragma once

#include "CoreMinimal.h"

class AActor;
class ABlockingVolume;

struct FBlockingVolumeWrapResult
{
    ABlockingVolume* CreatedVolume = nullptr;
    int32 SourceActorCount = 0;
    int32 SkippedActorCount = 0;
    bool bSuccess = false;
    FText ErrorText;
};

class FCreateBlockingVolumeOps
{
public:
    /**
     * Spawn a single BlockingVolume sized to the combined world-space bounds of the selected actors.
     * Bounds are taken from each actor's primitive components (StaticMesh, Skeletal, Brush, etc.) so
     * BP actors with composite layouts work too. Source actors are preserved (not destroyed). Volume
     * spawns into the level of the first acceptable actor. Existing BlockingVolumes in the selection
     * are skipped to avoid recursive wrap-of-self.
     */
    static FBlockingVolumeWrapResult CreateBlockingVolumeForActors(const TArray<AActor*>& Actors);
};
