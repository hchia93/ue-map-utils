#pragma once

#include "CoreMinimal.h"

class ULevel;
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
    static void BakeSelectedToInstanceMesh();
    static void BakeSelectedToMergedInstanceMesh();
    static void ExportStaticMeshContext();
    static void ExportCollisionContext();

    /** Display name for level in UI (short name; "Persistent Level" for persistent). */
    static FString GetLevelDisplayName(UWorld* World, ULevel* Level);
};
