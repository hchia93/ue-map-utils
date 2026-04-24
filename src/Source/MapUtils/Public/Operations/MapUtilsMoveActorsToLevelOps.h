#pragma once

#include "CoreMinimal.h"

class AActor;
class ULevel;

struct FMapUtilsMoveResult
{
    int32 MovedCount = 0;
    FString DestLevelName;
    bool bSuccess = false;
};

class FMapUtilsMoveActorsToLevelOps
{
public:
    static FMapUtilsMoveResult MoveActorsToLevel(
        const TArray<AActor*>& Actors,
        ULevel* DestLevel);
};
