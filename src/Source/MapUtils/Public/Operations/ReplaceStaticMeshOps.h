#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UStaticMesh;

struct FReplaceResult
{
    int32 UpdatedCount = 0;
    TArray<FString> UpdatedActorNames;
    bool bSuccess = false;
};

namespace ReplaceStaticMeshOps
{
    FReplaceResult ReplaceStaticMesh(const TArray<AStaticMeshActor*>& Actors, UStaticMesh* NewMesh);
}
