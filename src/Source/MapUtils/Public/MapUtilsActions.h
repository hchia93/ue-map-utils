#pragma once

#include "CoreMinimal.h"

class ULevel;
class UMaterialInterface;
class UWorld;

/**
 * Stateless action entry points invoked by the Slate panel and context menus.
 * Not menu registration (see MapUtilsTabSpawner / MapUtilsContextMenu).
 */
class FMapUtilsActions
{
public:
    static void AuditCurrentLevel();
    static void CreateBlockingVolumeFromSelection();
    static void BakeSelectedToInstanceMesh(UMaterialInterface* OverrideMaterial = nullptr);
    static void BakeSelectedToMergedInstanceMesh(UMaterialInterface* OverrideMaterial = nullptr);
    static void ExportStaticMeshContext();
    static void ExportCollisionContext();

    /** Display name for level in UI (short name; "Persistent Level" for persistent). */
    static FString GetLevelDisplayName(UWorld* World, ULevel* Level);
};
