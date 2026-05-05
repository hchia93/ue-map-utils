#include "Operations/MapUtilsMoveActorsToLevelOps.h"

#include "MapUtilsModule.h"

#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsMoveActorsToLevelOps"

FMapUtilsMoveResult FMapUtilsMoveActorsToLevelOps::MoveActorsToLevel(const TArray<AActor*>& Actors, ULevel* DestLevel)
{
    FMapUtilsMoveResult Result;

    if (!DestLevel)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MoveActorsToLevel: DestLevel null."));
        return Result;
    }

    if (Actors.IsEmpty())
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MoveActorsToLevel: empty actor list."));
        return Result;
    }

    Result.DestLevelName = DestLevel->GetOutermost()->GetName();

    FScopedTransaction Transaction(LOCTEXT("MoveActorsToLevel", "Move Actors to Level"));

    TArray<AActor*> OutMoved;
    const int32 Moved = UEditorLevelUtils::MoveActorsToLevel(Actors, DestLevel, /*bWarnAboutReferences=*/ true, /*bWarnAboutRenaming=*/ true, /*bMoveAllOrFail=*/ false, &OutMoved);

    Result.MovedCount = Moved;
    Result.bSuccess = Moved > 0;

    UE_LOG(LogMapUtils, Log, TEXT("MoveActorsToLevel: moved %d actor(s) to %s"), Moved, *Result.DestLevelName);

    return Result;
}

#undef LOCTEXT_NAMESPACE
