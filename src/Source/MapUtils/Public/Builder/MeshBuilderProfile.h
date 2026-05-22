#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/BodyInstance.h"

#include "MeshBuilderProfile.generated.h"

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
enum class EMeshBuilderSlotType : uint8
{
    Forward UMETA(DisplayName = "Forward"),
    Corner  UMETA(DisplayName = "Corner"),
};

// One mesh entry shared by all builders (chain / arc) for both Forward and Corner roles.
// ProfileId is the stable handle a Step / slot references; reordering / inserting into the
// array does not invalidate placed slots.
USTRUCT()
struct FMeshBuilderProfile
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid ProfileId;

    UPROPERTY(EditAnywhere, Category = "Profile")
    TObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, Category = "Profile")
    EMeshOrientation Orientation = EMeshOrientation::X;

    // Translation: chain Fwd.X = gap, chain Corner.X = post-turn shift; arc Y = inward, Z = vertical.
    UPROPERTY(EditAnywhere, Category = "Profile")
    FTransform Template;

    UPROPERTY(EditAnywhere, Category = "Profile")
    FBodyInstance BodyInstance;

    UPROPERTY(EditAnywhere, Category = "Profile")
    TObjectPtr<UMaterialInterface> OverrideMaterial;
};

// Editor-time bookkeeping for one spawned slot. SourceProfileId links back to the profile;
// LastBaselineRelative captures the auto pose so gizmo edits survive across rebuilds.
USTRUCT()
struct FMeshBuilderSlotState
{
    GENERATED_BODY()

    UPROPERTY()
    EMeshBuilderSlotType Type = EMeshBuilderSlotType::Forward;

    UPROPERTY()
    int32 StepIndex = 0;

    UPROPERTY()
    FGuid SourceProfileId;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Component;

    UPROPERTY()
    FTransform LastBaselineRelative = FTransform::Identity;
};
