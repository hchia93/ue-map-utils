#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "SanitizeMapReferenceCommandlet.generated.h"

/*
 * Repoint asset references inside level (.umap) packages: replace every reference
 * to an old asset with a new one, then resave the map. For the LD case where an
 * asset was renamed but the old copy is kept alive, and other levels still import
 * the old path; this rewrites those imports to the new asset so the old copy can
 * be deleted safely afterwards.
 *
 * Both old and new must still exist on disk: FArchiveReplaceObjectRef has to load
 * both to swap the pointer. Run this BEFORE deleting the old assets, never after.
 * Pairs counterpart to AuditMapReference (audit finds the breakage, this fixes it).
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=SanitizeMapReference
 *       -levels="/Game/Maps/L_A,/Game/Maps/L_B"
 *       -replace="/Game/Old/SM_Arch=/Game/New/SM_Foundation,/Game/Old/MI_X=/Game/New/MI_Y"
 *       [-report="<abs path to report JSON>"]
 *       [-dryrun]
 *
 * Args:
 *   -levels=   Comma-separated level package paths to sanitize.
 *   -replace=  Comma-separated old=new asset path pairs.
 *   -report=   Where to write the report JSON. Defaults under Intermediate/SanitizeMapReference/.
 *   -dryrun    Count references that would change, write report, but do not modify or save.
 *
 * Per-level error policy: a load / save failure records the level as failed and
 * moves on. The in-memory mutation is discarded on GC; on-disk umap stays untouched.
 *
 * Exit code: 0 = done (or nothing to replace), 2 = at least one level failed,
 *            1 = bad args or report write failure.
 */
UCLASS()
class USanitizeMapReferenceCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    USanitizeMapReferenceCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    struct FReplacePair
    {
        FString OldPath;
        FString NewPath;
    };

    struct FOptions
    {
        TArray<FString> LevelPaths;
        TArray<FReplacePair> Pairs;
        FString ReportPath;
        bool bDryRun = false;
    };

    struct FMapResult
    {
        FString MapPath;
        int32 ReferencesReplaced = 0;
        bool bSaved = false;
        FString FailReason;
    };

    bool ParseOptions(const FString& Params, FOptions& OutOptions) const;

    bool ResolvePairs(const TArray<FReplacePair>& Pairs, TMap<UObject*, UObject*>& OutReplaceMap) const;

    bool ProcessLevel(const FString& LevelPath, const TMap<UObject*, UObject*>& ReplaceMap, bool bDryRun, FMapResult& OutResult) const;

    bool WriteReport(const FString& ReportPath, const TArray<FMapResult>& Results, bool bDryRun) const;
};
