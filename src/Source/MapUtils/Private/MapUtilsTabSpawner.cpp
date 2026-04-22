#include "MapUtilsTabSpawner.h"

#include "SMapUtilsPanel.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FMapUtilsTabSpawner"

const FName FMapUtilsTabSpawner::TabName(TEXT("MapUtilsTab"));

void FMapUtilsTabSpawner::Register()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabName,
        FOnSpawnTab::CreateStatic(&FMapUtilsTabSpawner::SpawnPanelTab))
        .SetDisplayName(LOCTEXT("TabTitle", "Map Utils"))
        .SetTooltipText(LOCTEXT("TabTooltip",
            "Level designer tools: audit StaticMesh refs, convert to blocking volumes, export context for AI."))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FMapUtilsTabSpawner::Unregister()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> FMapUtilsTabSpawner::SpawnPanelTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("TabTitle", "Map Utils"))
        [
            SNew(SMapUtilsPanel)
        ];
}

#undef LOCTEXT_NAMESPACE
