#pragma once

#include "CoreMinimal.h"

class FMapUtilsActorContextMenu
{
public:
    static void Register();
    static void Unregister();

private:
    static void OnReplaceStaticMesh();
    static bool CanReplaceStaticMesh();
};
