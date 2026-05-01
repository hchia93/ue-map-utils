#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "MapUtilsBlueprintToStaticMeshReplacerCommandlet.generated.h"

class AStaticMeshActor;
class UStaticMeshComponent;

/*
 * Replace instances of trivial "wrapper" Blueprint classes (those whose only purpose
 * is to wrap a single StaticMeshComponent under a custom pivot or default bundle)
 * with plain AStaticMeshActor across one or more level packages.
 *
 * Modify phase of the workflow. The snapshot phase (BlueprintEdGraphExport ->
 * Script/BlueprintToStaticMeshReplacer/classify.py ->
 * Script/BlueprintToStaticMeshReplacer/discover_levels.py) must run first to
 * produce the candidate list.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=MapUtilsBlueprintToStaticMeshReplacer
 *       -candidates="/Game/Path/BP_A,/Game/Path/BP_B"
 *       -levels="/Game/Maps/L_X,/Game/Maps/L_Y"
 *       -manifest="<abs path to manifest output JSON>"
 *       [-dryrun]
 *
 * Args:
 *   -candidates=  Comma-separated BP asset paths (no _C suffix; resolved internally)
 *   -levels=      Comma-separated level package paths (.umap)
 *   -manifest=    Where to write the per-replacement manifest JSON
 *   -dryrun       Capture state and emit manifest, but do not spawn / destroy / save
 *
 * Each replacement preserves:
 *   - World transform (taken from OldSMC->GetComponentTransform(), so visual position is stable
 *     even when the BP root is a SceneComponent with non-identity offset)
 *   - StaticMesh, OverrideMaterials (trimmed to mesh slot count)
 *   - BodyInstance (full struct copy: profile + responses + physMat + flags)
 *   - Mobility (taken from OldSMC, not BP root)
 *   - Folder path (so World Outliner organization survives)
 *
 * Per-level error policy: any failure during a level (load / world / save) skips that
 * level entirely and records it as RED in the manifest. The in-memory mutation is
 * discarded on the next GC; on-disk umap is untouched. Non-fatal per-instance failures
 * (e.g. unexpected component shape) are recorded but do not abort the level.
 */
UCLASS()
class UMapUtilsBlueprintToStaticMeshReplacerCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UMapUtilsBlueprintToStaticMeshReplacerCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    struct FOptions
    {
        TArray<FString> CandidateAssetPaths;
        TArray<FString> LevelPaths;
        FString ManifestPath;
        bool bDryRun = false;
    };

    struct FReplaceRecord
    {
        FString LevelPath;
        FString OldActorName;
        FString OldActorLabel;
        FString OldFolder;
        FString OldBPClassPath;
        FString NewActorName;          // empty in dry-run
        FString MeshPath;
        TArray<FString> MaterialPaths; // trimmed to mesh slot count
        FString CollisionProfileName;
        FString MobilityStr;
        FVector WorldLoc;
        FRotator WorldRot;
        FVector WorldScale;
        bool bReplaced = false;
        FString FailReason;
    };

    struct FLevelResult
    {
        FString LevelPath;
        int32 InstancesFound = 0;
        int32 InstancesReplaced = 0;
        bool bSaved = false;
        FString FailReason;
        TArray<FReplaceRecord> Records;
    };

    bool ParseOptions(const FString& Params, FOptions& OutOptions) const;

    bool ResolveCandidateClasses(
        const TArray<FString>& CandidateAssetPaths,
        TArray<UClass*>& OutClasses) const;

    bool ProcessLevel(
        const FString& LevelPath,
        const TArray<UClass*>& CandidateClasses,
        bool bDryRun,
        FLevelResult& OutResult) const;

    bool ReplaceInstance(
        AActor* OldActor,
        UWorld* World,
        UClass* MatchedClass,
        bool bDryRun,
        FReplaceRecord& OutRecord) const;

    bool WriteManifest(
        const FString& ManifestPath,
        const TArray<FLevelResult>& LevelResults,
        bool bDryRun) const;
};
