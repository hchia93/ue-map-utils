#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"

#include "Builder/MeshBuilderPivot.h"

#include "MeshChainBuilder.generated.h"

class UBillboardComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EMeshOrientation : uint8
{
    X         UMETA(DisplayName = "X"),
    Y         UMETA(DisplayName = "Y"),
    InverseX  UMETA(DisplayName = "InverseX"),
    InverseY  UMETA(DisplayName = "InverseY"),
};

UENUM()
enum class EMeshChainStepKind : uint8
{
    Forward    UMETA(DisplayName = "Forward"),
    TurnLeft   UMETA(DisplayName = "Turn Left"),
    TurnRight  UMETA(DisplayName = "Turn Right"),
};

UENUM()
enum class EMeshChainSlotRole : uint8
{
    Main        UMETA(DisplayName = "Main"),
    Transition  UMETA(DisplayName = "Transition"),
    Corner      UMETA(DisplayName = "Corner"),
};

USTRUCT()
struct FMeshChainStep
{
    GENERATED_BODY()

    UPROPERTY()
    EMeshChainStepKind Kind = EMeshChainStepKind::Forward;

    // Turn magnitude in degrees, used when Kind is TurnLeft/TurnRight (sign comes from Kind).
    // Default 90 keeps pre-multi-angle saves behaving exactly as before.
    UPROPERTY()
    float TurnAngleDeg = 90.f;
};

// Editor-time bookkeeping for one spawned slot. Tracks the auto-computed baseline
// so user gizmo edits can be detected and preserved across rebuilds.
USTRUCT()
struct FMeshChainSlotState
{
    GENERATED_BODY()

    UPROPERTY()
    EMeshChainSlotRole Role = EMeshChainSlotRole::Main;

    UPROPERTY()
    int32 RoleIndex = 0;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Component;

    UPROPERTY()
    FTransform LastBaselineRelative = FTransform::Identity;
};

UCLASS(Blueprintable)
class MAPUTILS_API AMeshChainBuilder : public AActor
{
    GENERATED_BODY()

public:

    AMeshChainBuilder();

    virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    // Single-click node operations: each adds a Main slot at the chosen heading.
    // Left/Right take the turn magnitude in degrees (sign applied by direction).
    void Editor_AddNodeForward();
    void Editor_AddNodeLeft(float AngleDeg = 90.f);
    void Editor_AddNodeRight(float AngleDeg = 90.f);

    // Pop the last node (any trailing turns + the last Forward).
    void Editor_RemoveLast();

    // Wipe step list and all spawned slots.
    void Editor_ClearChain();

    // Discard every slot's user-applied transform delta and rebuild from baseline.
    void Editor_RegenerateChain();

    // Bake the current chain into a standalone actor with per-role ISMs.
    void Editor_BakeToISM();
#endif // WITH_EDITOR

    int32 GetMainCount() const;

protected:

    // -- Mesh slots: one mesh per role, each with optional breathing-room offset and its own forward axis. --
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Main")
    TObjectPtr<UStaticMesh> MeshA;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Main")
    EMeshOrientation OrientationA = EMeshOrientation::X;

    // Per-slot template overlay. Translation.X is the forward gap (accumulates along chain, scaled by Scale forward axis).
    // Translation.Y / Z are per-slot lateral / vertical displacement (do not propagate).
    // Rotation is layered on top of the orientation alignment (per-slot extra rotation).
    // Scale 3D scales the mesh and the chain's forward step.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Main", meta = (ShowOnlyInnerProperties))
    FTransform TemplateA;

    // Collision applied to Main slots only at Bake-to-ISM time. Edit-time SMCs always run NoCollision.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Main")
    FBodyInstance BodyInstanceA;

    // When set, every material slot on Main components is forced to this material (edit-time preview + bake).
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Main")
    TObjectPtr<UMaterialInterface> OverrideMaterialA;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Transition")
    TObjectPtr<UStaticMesh> MeshB;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Transition")
    EMeshOrientation OrientationB = EMeshOrientation::X;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Transition", meta = (ShowOnlyInnerProperties))
    FTransform TemplateB;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Transition")
    FBodyInstance BodyInstanceB;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Transition")
    TObjectPtr<UMaterialInterface> OverrideMaterialB;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner")
    TObjectPtr<UStaticMesh> MeshC;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner")
    EMeshOrientation OrientationC = EMeshOrientation::X;

    // Corner sits with its bounds center on the turn pivot. Translation.X = post-turn forward shift along the new direction
    // (positive leaves a gap to next slot, negative lets adjacent walls embed). Translation.Y / Z = corner's own lateral / vertical
    // displacement from the pivot (in the pre-turn chain frame). Rotation / Scale layered on as for Main / Transition.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner", meta = (ShowOnlyInnerProperties))
    FTransform TemplateC;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner")
    FBodyInstance BodyInstanceC;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner")
    TObjectPtr<UMaterialInterface> OverrideMaterialC;

    // Authored discrete sequence of steps. Add{Forward|Left|Right}Node always ends in a Forward,
    // optionally preceded by a Turn (for left/right). The chain walk consumes Steps in order.
    UPROPERTY()
    TArray<FMeshChainStep> Steps;

    // Pivot location for the baked actor. Default = builder's own actor transform.
    // Corners anchor on the chain head's connection face (YZ plane of the first Main slot);
    // Centroid is the 3D AABB center for scattered debris.
    UPROPERTY(EditAnywhere, Category = "Tool Action")
    EBakedPivotLocation BakedPivotLocation = EBakedPivotLocation::Default;

private:

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<UBillboardComponent> SpriteComponent;
#endif // WITH_EDITORONLY_DATA

    UPROPERTY()
    TArray<FMeshChainSlotState> m_Slots;

    void RebuildChain();

    UStaticMesh* GetMeshForRole(EMeshChainSlotRole InRole) const;
    const FTransform& GetTemplateForRole(EMeshChainSlotRole InRole) const;
    EMeshOrientation GetOrientationForRole(EMeshChainSlotRole InRole) const;
    float GetMeshForwardLength(const UStaticMesh* Mesh, EMeshOrientation Orient) const;
    float GetForwardScale(const FVector& Scale, EMeshOrientation Orient) const;
    FQuat GetMeshAlignmentQuat(EMeshOrientation Orient) const;
    FVector GetMeshBoundsCenterLocal(const UStaticMesh* Mesh) const;

    // Find or create a slot component for (Role, RoleIndex). Returns nullptr if mesh unset.
    UStaticMeshComponent* AcquireSlotComponent(EMeshChainSlotRole InRole, int32 RoleIndex, UStaticMesh* Mesh);

    void DestroyAllSlots();
    const FBodyInstance& GetBodyInstanceForRole(EMeshChainSlotRole InRole) const;
    void ApplyRoleCollision(EMeshChainSlotRole InRole, UStaticMeshComponent* Comp) const;
    UMaterialInterface* GetOverrideMaterialForRole(EMeshChainSlotRole InRole) const;
    void ApplyRoleOverrideMaterial(EMeshChainSlotRole InRole, UStaticMeshComponent* Comp) const;

#if WITH_EDITOR
    FTransform ComputeBakePivotXf() const;
#endif
};
