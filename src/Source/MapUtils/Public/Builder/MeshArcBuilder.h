#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Builder/MeshBuilderPivot.h"
#include "Builder/MeshBuilderProfile.h"

#include "MeshArcBuilder.generated.h"

class UBillboardComponent;
class UStaticMesh;
class UStaticMeshComponent;

// Parametric arc generator: Forward profiles as chord-tangent segments on a circle of radius R.
// Coverage (0..100%) controls span. Corner profiles (single Active) sit at joints.
// Template.Y = extra inward, Template.Z = vertical, Template.X unused.
UCLASS(Blueprintable)
class MAPUTILS_API AMeshArcBuilder : public AActor
{
    GENERATED_BODY()

public:

    AMeshArcBuilder();

    virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    const TArray<FMeshBuilderProfile>& GetForwardProfiles() const { return ForwardProfiles; }
    const TArray<FMeshBuilderProfile>& GetCornerProfiles() const { return CornerProfiles; }
    FGuid GetActiveCornerProfileId() const { return ActiveCornerProfileId; }
    void Editor_SetActiveCornerProfileId(FGuid InId);

    void Editor_RegenerateArc();
    void Editor_BakeToISM();
#endif // WITH_EDITOR

    int32 GetSlotCount() const { return m_Slots.Num(); }

protected:

    // Forward profiles cycle in array order around the arc (index N uses ForwardProfiles[N % Count]).
    // Place one for a uniform fence; place several for an A/B/C-style repeat pattern.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Forward Profiles")
    TArray<FMeshBuilderProfile> ForwardProfiles;

    // Only ActiveCornerProfileId is used; the Details panel renders these as a radio list.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Corner Profiles")
    TArray<FMeshBuilderProfile> CornerProfiles;

    UPROPERTY()
    FGuid ActiveCornerProfileId;

    // Fraction of the full circle to cover, 0..100 percent. Slider clamps to [0, 100].
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Arc", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "Percent"))
    float ArcCoveragePercent = 100.f;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Arc", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float Radius = 500.f;

    // Rotation around Z (in degrees) applied to where the arc starts. 0 = arc begins at +X axis.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Arc")
    float StartAngleDeg = 0.f;

    // Lays the arc clockwise (negative angle direction) when true. Default is CCW (math convention).
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Arc")
    bool bClockwise = false;

    // Positive value shifts every slot toward the center by this distance. Useful for placing
    // a fence ring "inside" the nominal R circle.
    UPROPERTY(EditAnywhere, Category = "Tool Setup|Arc")
    float InwardOffset = 0.f;

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

    void RebuildArc();

    const FMeshBuilderProfile* FindProfile(FGuid InId, bool bIsCorner) const;

    float GetMeshForwardLength(const UStaticMesh* Mesh, EMeshOrientation Orient) const;
    float GetForwardScale(const FVector& Scale, EMeshOrientation Orient) const;
    FQuat GetMeshAlignmentQuat(EMeshOrientation Orient) const;
    FVector GetMeshBoundsCenterLocal(const UStaticMesh* Mesh) const;

    UStaticMeshComponent* AcquireSlotComponent(EMeshBuilderSlotType InType, int32 SlotIndex, UStaticMesh* Mesh);

    void DestroyAllSlots();
    void ApplyProfileCollision(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const;
    void ApplyProfileOverrideMaterial(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const;

    void EnsureProfileIds();

#if WITH_EDITOR
    FTransform ComputeBakePivotXf() const;
#endif // WITH_EDITOR
};
