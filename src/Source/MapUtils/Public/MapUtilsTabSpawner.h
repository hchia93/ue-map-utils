#pragma once

#include "CoreMinimal.h"

class SDockTab;
class FSpawnTabArgs;

class FMapUtilsTabSpawner
{
public:
    static const FName TabName;

    static void Register();
    static void Unregister();

private:
    static TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args);
};
