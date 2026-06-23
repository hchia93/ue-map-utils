#pragma once

#include "CoreMinimal.h"

class AActor;
class ULevel;

struct FMoveResult
{
    int32 MovedCount = 0;
    FString DestLevelName;
    bool bSuccess = false;
};

class FMoveActorsToLevelOps
{
public:
    static FMoveResult MoveActorsToLevel(const TArray<AActor*>& Actors, ULevel* DestLevel);
};
