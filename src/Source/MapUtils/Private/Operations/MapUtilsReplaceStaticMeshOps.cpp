#include "Operations/MapUtilsReplaceStaticMeshOps.h"

#include "MapUtilsModule.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsReplaceStaticMeshOps"

FMapUtilsReplaceResult FMapUtilsReplaceStaticMeshOps::ReplaceStaticMesh(
    const TArray<AStaticMeshActor*>& Actors,
    UStaticMesh* NewMesh)
{
    FMapUtilsReplaceResult Result;

    if (!NewMesh)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ReplaceStaticMesh: NewMesh null."));
        return Result;
    }

    FScopedTransaction Transaction(LOCTEXT("ReplaceStaticMesh", "Replace StaticMesh"));

    for (AStaticMeshActor* Actor : Actors)
    {
        if (!IsValid(Actor))
        {
            continue;
        }

        UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
        if (!MeshComp)
        {
            continue;
        }

        Actor->Modify();
        MeshComp->Modify();
        MeshComp->SetStaticMesh(NewMesh);
        Actor->PostEditChange();

        Result.UpdatedActorNames.Add(Actor->GetName());
        Result.UpdatedCount++;
    }

    Result.bSuccess = Result.UpdatedCount > 0;

    UE_LOG(LogMapUtils, Log,
        TEXT("ReplaceStaticMesh: updated %d actor(s) with %s"),
        Result.UpdatedCount, *NewMesh->GetPathName());

    return Result;
}

#undef LOCTEXT_NAMESPACE
