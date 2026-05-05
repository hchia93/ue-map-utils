#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UStaticMesh;

struct FMapUtilsReplaceResult
{
    int32 UpdatedCount = 0;
    TArray<FString> UpdatedActorNames;
    bool bSuccess = false;
};

class FMapUtilsReplaceStaticMeshOps
{
public:
    static FMapUtilsReplaceResult ReplaceStaticMesh(const TArray<AStaticMeshActor*>& Actors, UStaticMesh* NewMesh);
};
