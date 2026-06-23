#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FLevelContextExportResult
{
    FString OutputPath;
    int32 ItemCount = 0;
    bool bSuccess = false;
};

class FLevelContextExporter
{
public:
    /** Export current level's StaticMeshActor refs (actor / mesh path / transform / materials / bounds). */
    static FLevelContextExportResult ExportStaticMeshContext(UWorld* World);

    /** Export current level's collision candidates (hidden + collision enabled bias, but records all non-NoCollision). */
    static FLevelContextExportResult ExportCollisionContext(UWorld* World);

private:
    static FString MakeOutputPath(const FString& Topic, UWorld* World);
};
