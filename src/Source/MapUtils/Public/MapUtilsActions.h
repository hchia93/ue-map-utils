#pragma once

#include "CoreMinimal.h"

class ULevel;
class UWorld;

/**
 * Stateless action entry points invoked by the Slate panel and context menus.
 * Not menu registration (see MapUtilsTabSpawner / MapUtilsActorContextMenu).
 */
namespace MapUtilsActions
{
    void AuditCurrentLevel();
    void CreateBlockingVolumeFromSelection();
    void BakeSelectedToInstanceMesh();
    void BakeSelectedToMergedInstanceMesh();
    void FixBakedIsmRotation();
    void ExportStaticMeshContext();
    void ExportCollisionContext();

    /** Display name for level in UI (short name; "Persistent Level" for persistent). */
    FString GetLevelDisplayName(UWorld* World, ULevel* Level);
}
