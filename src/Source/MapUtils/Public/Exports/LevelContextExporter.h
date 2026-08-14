#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FLevelContextExportResult
{
    FString OutputPath;
    int32 ItemCount = 0;
    bool bSuccess = false;
};

namespace LevelContextExporter
{
    /** Export current level's StaticMeshActor refs (actor / mesh path / transform / materials / bounds). */
    FLevelContextExportResult ExportStaticMeshContext(UWorld* World);

    /** Export current level's collision candidates (hidden + collision enabled bias, but records all non-NoCollision). */
    FLevelContextExportResult ExportCollisionContext(UWorld* World);
}
