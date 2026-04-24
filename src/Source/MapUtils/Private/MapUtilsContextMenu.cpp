#include "MapUtilsContextMenu.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsReplaceStaticMeshOps.h"

#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "IContentBrowserSingleton.h"
#include "Selection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FMapUtilsContextMenu"

namespace
{
    const FName ActorContextMenuPath(TEXT("LevelEditor.ActorContextMenu"));
    const FName MapUtilsSection(TEXT("MapUtils"));

    TArray<AStaticMeshActor*> GatherSelectedStaticMeshActors()
    {
        TArray<AStaticMeshActor*> Result;
        if (!GEditor)
        {
            return Result;
        }

        USelection* Selection = GEditor->GetSelectedActors();
        if (!Selection)
        {
            return Result;
        }

        for (FSelectionIterator It(*Selection); It; ++It)
        {
            if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(*It))
            {
                Result.Add(SMA);
            }
        }
        return Result;
    }
}

void FMapUtilsContextMenu::Register()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus)
    {
        return;
    }

    UToolMenu* ActorMenu = ToolMenus->ExtendMenu(ActorContextMenuPath);
    if (!ActorMenu)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("FMapUtilsContextMenu::Register: '%s' not found. Replace StaticMesh available only via Tools menu."), *ActorContextMenuPath.ToString());
        return;
    }

    FToolMenuSection& Section = ActorMenu->FindOrAddSection(
        MapUtilsSection,
        LOCTEXT("MapUtilsSection", "Map Utils"));

    FUIAction Action;
    Action.ExecuteAction = FExecuteAction::CreateStatic(&FMapUtilsContextMenu::OnReplaceStaticMesh);
    Action.CanExecuteAction = FCanExecuteAction::CreateStatic(&FMapUtilsContextMenu::CanReplaceStaticMesh);

    Section.AddMenuEntry(
        TEXT("ReplaceStaticMesh"),
        LOCTEXT("ReplaceStaticMesh", "Replace StaticMesh..."),
        LOCTEXT("ReplaceStaticMeshTooltip",
            "Pick a StaticMesh asset and apply to all selected StaticMeshActors. Undo-safe."),
        FSlateIcon(),
        Action);
}

void FMapUtilsContextMenu::Unregister()
{
    if (UToolMenus* ToolMenus = UToolMenus::Get())
    {
        ToolMenus->RemoveSection(ActorContextMenuPath, MapUtilsSection);
    }
}

bool FMapUtilsContextMenu::CanReplaceStaticMesh()
{
    return GatherSelectedStaticMeshActors().Num() > 0;
}

void FMapUtilsContextMenu::OnReplaceStaticMesh()
{
    const TArray<AStaticMeshActor*> Actors = GatherSelectedStaticMeshActors();
    if (Actors.IsEmpty())
    {
        UE_LOG(LogMapUtils, Warning, TEXT("OnReplaceStaticMesh: no StaticMeshActor selected."));
        return;
    }

    FOpenAssetDialogConfig Config;
    Config.DialogTitleOverride = LOCTEXT("PickerTitle", "Select Replacement StaticMesh");
    Config.AssetClassNames.Add(UStaticMesh::StaticClass()->GetClassPathName());
    Config.bAllowMultipleSelection = false;

    FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    TArray<FAssetData> Picked = CBModule.Get().CreateModalOpenAssetDialog(Config);
    if (Picked.Num() == 0)
    {
        return;
    }

    UStaticMesh* NewMesh = Cast<UStaticMesh>(Picked[0].GetAsset());
    if (!NewMesh)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("OnReplaceStaticMesh: picked asset is not a StaticMesh."));
        return;
    }

    FMapUtilsReplaceStaticMeshOps::ReplaceStaticMesh(Actors, NewMesh);
}

#undef LOCTEXT_NAMESPACE
