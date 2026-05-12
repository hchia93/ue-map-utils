#pragma once

#include "CoreMinimal.h"

#include "MeshBuilderPivot.generated.h"

// Pivot for actors baked from MeshChainBuilder / MeshGridBuilder.
// Default = builder's own actor transform (where the LD placed it).
// Corners anchor on a YZ plane (front/back side of the bounds, the builder's "thickness" plane),
// at the X coordinate the caller picks as the plane closest to the builder's original pivot.
// Axis convention: Top = +Z, Bottom = -Z, Left = -Y, Right = +Y.
// Centroid = 3D AABB center.
UENUM()
enum class EBakedPivotLocation : uint8
{
    Default     UMETA(DisplayName = "Default (Builder Transform)"),
    TopLeft     UMETA(DisplayName = "Top Left (+Z, -Y)"),
    TopRight    UMETA(DisplayName = "Top Right (+Z, +Y)"),
    BottomLeft  UMETA(DisplayName = "Bottom Left (-Z, -Y)"),
    BottomRight UMETA(DisplayName = "Bottom Right (-Z, +Y)"),
    BoundCenter UMETA(DisplayName = "Centroid"),
};

namespace MeshBuilderPivot
{
    // Resolve a corner / center pivot location against an axis-aligned world bounds box.
    // Corner cases sit on the YZ plane at PlaneYZ (an X coordinate) so the gizmo lands on the
    // builder's thickness plane; BoundCenter uses the full 3D center and ignores PlaneYZ. ChainHead
    // is not bounds-derived and must be resolved by the chain builder using its own slot data.
    inline FVector ResolveBoundsAnchor(const FBox& InBounds, EBakedPivotLocation Loc, float PlaneYZ)
    {
        if (!InBounds.IsValid)
        {
            return FVector::ZeroVector;
        }
        switch (Loc)
        {
        case EBakedPivotLocation::TopLeft:     return FVector(PlaneYZ, InBounds.Min.Y, InBounds.Max.Z);
        case EBakedPivotLocation::TopRight:    return FVector(PlaneYZ, InBounds.Max.Y, InBounds.Max.Z);
        case EBakedPivotLocation::BottomLeft:  return FVector(PlaneYZ, InBounds.Min.Y, InBounds.Min.Z);
        case EBakedPivotLocation::BottomRight: return FVector(PlaneYZ, InBounds.Max.Y, InBounds.Min.Z);
        case EBakedPivotLocation::BoundCenter: return InBounds.GetCenter();
        default:                               return InBounds.GetCenter();
        }
    }

    // Pick the YZ plane (X coordinate) of InBounds closer to PivotRefX. Used by callers (e.g. grid
    // builder) that have no inherent forward axis but want corners to anchor on the side facing
    // the builder's own pivot.
    inline float NearestPlaneYZ(const FBox& InBounds, float PivotRefX)
    {
        if (!InBounds.IsValid)
        {
            return PivotRefX;
        }
        return (FMath::Abs(PivotRefX - InBounds.Min.X) <= FMath::Abs(PivotRefX - InBounds.Max.X))
            ? InBounds.Min.X
            : InBounds.Max.X;
    }
}
