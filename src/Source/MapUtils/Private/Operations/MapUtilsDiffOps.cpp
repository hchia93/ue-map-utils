#include "Operations/MapUtilsDiffOps.h"

#include "MapUtilsActions.h"
#include "MapUtilsModule.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/ITransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
    // A transaction with more direct AActor refs than this is treated as
    // level-wide noise (e.g. ULevel::Actors array serialized wholesale).
    constexpr int32 BulkActorThreshold = 10;
}

TArray<FMapUtilsDiffEntry> FMapUtilsDiffOps::ScanModifiedActors(UWorld* World)
{
    TArray<FMapUtilsDiffEntry> Result;

    if (!World || !GEditor || !GEditor->Trans)
    {
        return Result;
    }

    UTransactor* Transactor = GEditor->Trans;
    const int32 QueueLength = Transactor->GetQueueLength();
    const int32 UndoCount = Transactor->GetUndoCount();
    const int32 ActiveCount = FMath::Max(0, QueueLength - UndoCount);

    TSet<AActor*> SeenActors;

    for (int32 QueueIndex = 0; QueueIndex < ActiveCount; ++QueueIndex)
    {
        const FTransaction* Transaction = Transactor->GetTransaction(QueueIndex);
        if (!Transaction)
        {
            continue;
        }

        TArray<UObject*> Objects;
        Transaction->GetTransactionObjects(Objects);

        int32 DirectActorCount = 0;
        for (UObject* Object : Objects)
        {
            if (Cast<AActor>(Object))
            {
                ++DirectActorCount;
            }
        }
        const bool bBulk = DirectActorCount > BulkActorThreshold;

        for (UObject* Object : Objects)
        {
            if (!Object || Cast<ULevel>(Object))
            {
                continue;
            }

            AActor* Actor = Cast<AActor>(Object);
            if (Actor && bBulk)
            {
                Actor = nullptr;
            }

            if (!Actor)
            {
                for (UObject* Outer = Object->GetOuter(); Outer; Outer = Outer->GetOuter())
                {
                    Actor = Cast<AActor>(Outer);
                    if (Actor)
                    {
                        break;
                    }
                }
            }

            if (!Actor || !IsValid(Actor) || Actor->GetWorld() != World)
            {
                continue;
            }

            bool bAlreadyIn = false;
            SeenActors.Add(Actor, &bAlreadyIn);
            if (bAlreadyIn)
            {
                continue;
            }

            ULevel* Level = Actor->GetLevel();
            FMapUtilsDiffEntry Entry;
            Entry.Actor = Actor;
            Entry.ActorDisplayName = Actor->GetActorLabel();
            Entry.Level = Level;
            Entry.LevelDisplayName = FMapUtilsActions::GetLevelDisplayName(World, Level);
            Result.Add(MoveTemp(Entry));
        }
    }

    Result.Sort([](const FMapUtilsDiffEntry& A, const FMapUtilsDiffEntry& B)
    {
        if (A.LevelDisplayName != B.LevelDisplayName)
        {
            return A.LevelDisplayName < B.LevelDisplayName;
        }
        return A.ActorDisplayName < B.ActorDisplayName;
    });

    return Result;
}

FMapUtilsActorChangeSummary FMapUtilsDiffOps::GetActorChanges(AActor* Actor)
{
    FMapUtilsActorChangeSummary Result;

    if (!Actor || !GEditor || !GEditor->Trans)
    {
        return Result;
    }

    const FString ActorPath = Actor->GetPathName();
    const FString SubObjectPrefix = ActorPath + TEXT(".");

    UTransactor* Transactor = GEditor->Trans;
    const int32 QueueLength = Transactor->GetQueueLength();
    const int32 UndoCount = Transactor->GetUndoCount();
    const int32 ActiveCount = FMath::Max(0, QueueLength - UndoCount);

    TMap<FString, FMapUtilsObjectChange> Aggregated;

    for (int32 QueueIndex = 0; QueueIndex < ActiveCount; ++QueueIndex)
    {
        const FTransaction* Transaction = Transactor->GetTransaction(QueueIndex);
        if (!Transaction)
        {
            continue;
        }

        const FTransactionDiff Diff = Transaction->GenerateDiff();
        for (const TPair<FName, TSharedPtr<FTransactionObjectEvent>>& Pair : Diff.DiffMap)
        {
            if (!Pair.Value.IsValid())
            {
                continue;
            }
            const FTransactionObjectEvent& Event = *Pair.Value;
            const FString ObjectPath = Event.GetOriginalObjectId().ObjectPathName.ToString();

            const bool bIsActor = (ObjectPath == ActorPath);
            const bool bIsSubObject = ObjectPath.StartsWith(SubObjectPrefix);
            if (!bIsActor && !bIsSubObject)
            {
                continue;
            }

            FMapUtilsObjectChange& Change = Aggregated.FindOrAdd(ObjectPath);
            Change.ObjectPath = ObjectPath;
            Change.bHasOuterChange |= Event.HasOuterChange();
            Change.bHasNameChange |= Event.HasNameChange();
            Change.bHasExternalPackageChange |= Event.HasExternalPackageChange();
            Change.bHasPendingKillChange |= Event.HasPendingKillChange();
            Change.bHasNonPropertyChanges |= Event.HasNonPropertyChanges(/*InSerializationOnly=*/ true);

            for (const FName& Prop : Event.GetChangedProperties())
            {
                Change.ChangedProperties.AddUnique(Prop);
            }
        }
    }

    Aggregated.GenerateValueArray(Result.Objects);

    Result.Objects.Sort([](const FMapUtilsObjectChange& A, const FMapUtilsObjectChange& B)
    {
        return A.ObjectPath < B.ObjectPath;
    });

    for (FMapUtilsObjectChange& Change : Result.Objects)
    {
        Change.ChangedProperties.Sort([](const FName& A, const FName& B)
        {
            return A.LexicalLess(B);
        });
    }

    return Result;
}
