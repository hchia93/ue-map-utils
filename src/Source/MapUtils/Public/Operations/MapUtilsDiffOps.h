#pragma once

#include "CoreMinimal.h"

class AActor;
class ULevel;
class UWorld;

/**
 * Delta entry: one actor that appeared in the editor transaction buffer
 * during the current session. Session-bound — cleared on editor restart.
 */
struct FMapUtilsDiffEntry
{
    TWeakObjectPtr<AActor> Actor;
    FString ActorDisplayName;
    FString LevelDisplayName;
    TWeakObjectPtr<ULevel> Level;
};

struct FMapUtilsObjectChange
{
    FString ObjectPath;
    TArray<FName> ChangedProperties;
    bool bHasOuterChange = false;
    bool bHasNameChange = false;
    bool bHasExternalPackageChange = false;
    bool bHasPendingKillChange = false;
    bool bHasNonPropertyChanges = false;
};

struct FMapUtilsActorChangeSummary
{
    TArray<FMapUtilsObjectChange> Objects;
};

class FMapUtilsDiffOps
{
public:
    /**
     * Scan the editor transaction buffer (GEditor->Trans) and collect every
     * AActor referenced by any transaction in the queue that still belongs
     * to the supplied world.
     *
     * De-duplicated by actor pointer. Destroyed / GC'd actors are skipped.
     *
     * Returns entries sorted by LevelDisplayName then ActorDisplayName.
     */
    static TArray<FMapUtilsDiffEntry> ScanModifiedActors(UWorld* World);

    /**
     * Aggregate property-level changes for a single actor (and its sub-objects)
     * across every active transaction. Pulls from FTransaction::GenerateDiff().
     */
    static FMapUtilsActorChangeSummary GetActorChanges(AActor* Actor);
};
