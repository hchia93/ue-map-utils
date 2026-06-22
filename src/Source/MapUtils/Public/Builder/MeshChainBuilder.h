#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Builder/MeshBuilderPivot.h"
#include "Builder/MeshBuilderProfile.h"

#include "MeshChainBuilder.generated.h"

class UBillboardComponent;

// TurnAngleDeg sign: 0 = forward only, > 0 = right turn, < 0 = left turn.
// Corner is live-tracked from ActiveCornerProfileId at rebuild time, not captured per step.
USTRUCT()
struct FMeshChainStep
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid ForwardProfileId;

    UPROPERTY()
    float TurnAngleDeg = 0.f;
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

    const TArray<FMeshBuilderProfile>& GetForwardProfiles() const { return ForwardProfiles; }
    const TArray<FMeshBuilderProfile>& GetCornerProfiles() const { return CornerProfiles; }
    FGuid GetActiveCornerProfileId() const { return ActiveCornerProfileId; }
    void Editor_SetActiveCornerProfileId(FGuid InId);

    // One click = one step. TurnAngleDeg sign: 0 = straight forward, > 0 = right, < 0 = left.
    void Editor_AddNode(FGuid ForwardProfileId, float TurnAngleDeg);

    void Editor_RemoveLast();
    void Editor_ClearChain();
    void Editor_RegenerateChain();
    void Editor_BakeToISM();
#endif // WITH_EDITOR

    int32 GetStepCount() const { return Steps.Num(); }

protected:

    // Each entry exposes one row of [45L|90L|Fwd|90R|45R] in the Details panel.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Forward Profiles")
    TArray<FMeshBuilderProfile> ForwardProfiles;

    // Only one corner is active at a time; every turn step reads it live at rebuild time.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner Profiles")
    TArray<FMeshBuilderProfile> CornerProfiles;

    UPROPERTY()
    FGuid ActiveCornerProfileId;

    UPROPERTY()
    TArray<FMeshChainStep> Steps;

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
    TArray<FMeshBuilderSlotState> m_Slots;

    void RebuildChain();

    // Drop Steps whose ForwardProfileId no longer resolves (e.g. LD deleted the profile entry).
    void PruneOrphanSteps();

    const FMeshBuilderProfile* FindProfile(FGuid InId, bool bIsCorner) const;

    float GetMeshForwardLength(const UStaticMesh* Mesh, EMeshOrientation Orient) const;
    float GetForwardScale(const FVector& Scale, EMeshOrientation Orient) const;
    FQuat GetMeshAlignmentQuat(EMeshOrientation Orient) const;
    FVector GetMeshBoundsCenterLocal(const UStaticMesh* Mesh) const;

    UStaticMeshComponent* AcquireSlotComponent(EMeshBuilderSlotType InType, int32 StepIndex, UStaticMesh* Mesh);

    void DestroyAllSlots();
    void ApplyProfileCollision(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const;
    void ApplyProfileOverrideMaterial(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const;

    // Auto-assign GUIDs to any profiles whose ProfileId is still default (e.g. freshly added in Details).
    void RegenerateProfileIds();

#if WITH_EDITOR
    FTransform ComputeBakePivotXf() const;
#endif // WITH_EDITOR
};
