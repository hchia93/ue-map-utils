#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MeshGridBuilder.generated.h"

class UBillboardComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

UENUM()
enum class EFloorConnectivity : uint8
{
    Fourway          UMETA(DisplayName = "Fourway (4 neighbors)"),
    Threeway         UMETA(DisplayName = "Threeway (3 neighbors, T-junction)"),
    TwowayAdjacent   UMETA(DisplayName = "Twoway Adjacent (2 neighbors, L corner)"),
    TwowayOpposite   UMETA(DisplayName = "Twoway Opposite (2 neighbors, I corridor)"),
    Single           UMETA(DisplayName = "Single (1 neighbor, dead end)"),
    None             UMETA(DisplayName = "None (isolated tile)"),
};

/**
 * Editor authoring actor: paints an M x N grid of static-mesh tiles, lets the LD delete
 * cells to carve a footprint, then auto-assigns one of six connectivity-aware materials
 * per cell and bakes the result to a single ISM-hosting actor.
 *
 * Workflow:
 *  1. Set TileMesh + materials, set GridSize, click Generate -> M*N cell SMCs.
 *  2. Delete cell SMCs in the viewport / Details to carve the footprint.
 *  3. Click Process -> material per cell is recomputed from 4-neighbor topology.
 *  4. Click BakeToISM -> grouped ISMC actor spawned, this builder destroyed.
 */
UCLASS(Blueprintable)
class MAPUTILS_API AMeshGridBuilder : public AActor
{
    GENERATED_BODY()

public:
    AMeshGridBuilder();

#if WITH_EDITOR
    // Spawn the M x N grid; existing cells are wiped. Process is invoked at the end so the LD
    // sees connectivity-correct materials immediately.
    void Editor_Generate();

    // Recompute material per surviving cell from 4-neighbor topology.
    void Editor_Process();

    // Destroy all cell SMCs without touching the SceneRoot / sprite.
    void Editor_Clear();

    // Group cells by slot-0 material into ISMC, spawn merged actor at AABB center, destroy this builder.
    void Editor_BakeToISM();
#endif // WITH_EDITOR

protected:
    UPROPERTY(EditAnywhere, Category = "Tool Setup")
    TObjectPtr<UStaticMesh> TileMesh;

    /** Fallback material when a connectivity-specific slot is left empty. */
    UPROPERTY(EditAnywhere, Category = "Tool Setup")
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> FourwayMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> ThreewayMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> TwowayAdjacentMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> TwowayOppositeMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> SingleMaterial;

    UPROPERTY(EditAnywhere, Category = "Tool Setup|Variants")
    TObjectPtr<UMaterialInterface> NoneMaterial;

    /** Rows x Columns. Row index advances along local X, column index along local Y. */
    UPROPERTY(EditAnywhere, Category = "Grid", meta = (ClampMin = "1"))
    FIntPoint GridSize = FIntPoint(4, 4);

private:
    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<UBillboardComponent> SpriteComponent;
#endif // WITH_EDITORONLY_DATA

    UMaterialInterface* ResolveMaterial(EFloorConnectivity Connectivity) const;
    FVector2D ComputeTileStep() const;

#if WITH_EDITOR
    UStaticMeshComponent* SpawnCell(int32 Row, int32 Col, const FVector2D& Step);
    void DestroyAllCellComponents();

    /** Returns true if Name matches "Cell_R<row>_C<col>" and writes the parsed indices. */
    static bool ParseCellName(const FString& Name, int32& OutRow, int32& OutCol);

    /** Component name encoding the (row, col) for a cell. */
    static FName MakeCellName(int32 Row, int32 Col);
#endif // WITH_EDITOR
};
