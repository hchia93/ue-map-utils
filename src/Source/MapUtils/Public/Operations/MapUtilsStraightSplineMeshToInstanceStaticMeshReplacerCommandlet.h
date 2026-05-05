#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "Math/Transform.h"

#include "MapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet.generated.h"

class UStaticMesh;

/*
 * Replace candidate spline-mesh wrapper actors whose entire spline is straight (no real
 * bend on any segment) with a per-actor wrapper that hosts one InstancedStaticMeshComponent
 * per unique mesh asset, with one instance per source segment.
 *
 * Motivation: spline-mesh dressings (railings, fences, beams) are heavy at runtime even when
 * authored with no actual curvature, because each segment is its own deformed
 * USplineMeshComponent. Once the spline is straight the mesh is just a translated /
 * rotated / forward-scaled copy of the source mesh, which an ISMC handles for free.
 * This commandlet detects those cases, bakes each segment to a static transform, and
 * collapses the actor's segments into one ISMC per unique mesh.
 *
 * Eligibility: every USplineMeshComponent on the actor must satisfy
 *      dot(StartTangent.norm, EndTangent.norm)   > TangentParallelDot
 *  AND dot(StartTangent.norm, Chord.norm)        > TangentChordDot
 * Mixed-curvature actors are skipped (v1).
 *
 * Per-segment baked transform places a mesh at the chord origin, oriented with the mesh's
 * forward axis along the chord direction, scaled along forward so its extent matches the
 * chord length. Right and up axes retain scale 1.
 *
 * Collision on the produced ISMs is forced to NoCollision; this op is intended for
 * decorative geometry, not blockers.
 *
 * Output actor layout: one new AActor per source actor, USceneComponent root at the
 * source actor's world transform, with UInstancedStaticMeshComponent children grouped
 * by mesh asset. Outliner folder + label are preserved (label suffixed "_ISM").
 *
 * Defaults to DRY RUN. Pass -execute to mutate and save.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=MapUtilsStraightSplineMeshToInstanceStaticMeshReplacer
 *       -candidates="/Game/Path/BP_A,/Game/Path/BP_B"
 *       -levels="/Game/Maps/L_X,/Game/Maps/L_Y"
 *       -manifest="<abs path output JSON>"
 *       [-execute]                         (default: dry-run, no actor mutation, no save)
 *       [-tangentdot=0.995]                (parallel-tangent dot threshold)
 *       [-chorddot=0.995]                  (tangent-vs-chord dot threshold)
 *
 * Per-level error policy: any failure (load / world / save) skips the level entirely and
 * records it as RED in the manifest. Per-actor failures are logged but do not abort the level.
 */
UCLASS()
class UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UMapUtilsStraightSplineMeshToInstanceStaticMeshReplacerCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    struct FOptions
    {
        TArray<FString> CandidateAssetPaths;
        TArray<FString> LevelPaths;
        FString ManifestPath;
        bool bDryRun = true;
        float TangentParallelDot = 0.995f;
        float TangentChordDot = 0.995f;
    };

    struct FSegmentRecord
    {
        FString MeshPath;
        FVector LocalStartPos = FVector::ZeroVector;
        FVector LocalStartTangent = FVector::ZeroVector;
        FVector LocalEndPos = FVector::ZeroVector;
        FVector LocalEndTangent = FVector::ZeroVector;
        FTransform BakedActorLocal = FTransform::Identity;   // chord-aligned local transform (relative to source actor)
        FTransform BakedWorld = FTransform::Identity;        // chord-aligned world transform (for manifest preview)
        bool bStraight = false;
    };

    struct FActorRecord
    {
        FString OldActorName;
        FString OldActorLabel;
        FString OldFolder;
        FString OldBPClassPath;
        FString NewActorName;          // empty in dry-run
        FTransform OldActorWorldXform = FTransform::Identity;
        TArray<FSegmentRecord> Segments;
        bool bAllStraight = false;
        bool bReplaced = false;
        FString FailReason;
    };

    struct FLevelResult
    {
        FString LevelPath;
        int32 ActorsScanned = 0;
        int32 ActorsEligible = 0;
        int32 ActorsReplaced = 0;
        int32 SegmentsTotal = 0;
        int32 SegmentsStraight = 0;
        int32 SegmentsCurved = 0;
        bool bSaved = false;
        FString FailReason;
        TArray<FActorRecord> Records;
    };

    bool ParseOptions(const FString& Params, FOptions& OutOptions) const;

    bool ResolveCandidateClasses(const TArray<FString>& CandidateAssetPaths, TArray<UClass*>& OutClasses) const;

    bool ProcessLevel(const FString& LevelPath, const TArray<UClass*>& CandidateClasses, const FOptions& Opts, FLevelResult& OutResult) const;

    // Inspect a candidate actor, classify each spline mesh segment, populate Record.
    // Does not mutate. Returns true if Record.bAllStraight.
    bool AnalyzeActor(AActor* OldActor, const FOptions& Opts, FActorRecord& OutRecord) const;

    // Replace OldActor with a new ISM-hosting actor at OldActor's world transform.
    // Mutates the world. Caller saves the package.
    bool ReplaceActor(AActor* OldActor, UWorld* World, FActorRecord& InOutRecord) const;

    bool WriteManifest(const FString& ManifestPath, const TArray<FLevelResult>& LevelResults, const FOptions& Opts) const;
};
