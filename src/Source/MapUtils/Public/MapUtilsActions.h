#pragma once

#include "CoreMinimal.h"

/**
 * Stateless action entry points invoked by the Slate panel and context menus.
 * Not menu registration (see MapUtilsTabSpawner / MapUtilsContextMenu).
 */
class FMapUtilsActions
{
public:
    static void AuditCurrentLevel();
    static void ConvertSelectedToBlockingVolume();
    static void ExportStaticMeshContext();
    static void ExportCollisionContext();
};
