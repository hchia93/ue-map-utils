#include "MapUtilsModule.h"

#include "MapUtilsContextMenu.h"
#include "MapUtilsTabSpawner.h"

#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogMapUtils);

#define LOCTEXT_NAMESPACE "FMapUtilsModule"

namespace
{
    const FName MapUtilsLogName(TEXT("MapUtils"));
}

void FMapUtilsModule::StartupModule()
{
    UE_LOG(LogMapUtils, Log, TEXT("MapUtils module started."));

    FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
    FMessageLogInitializationOptions InitOptions;
    InitOptions.bShowFilters = true;
    InitOptions.bShowPages = true;
    InitOptions.bAllowClear = true;
    MessageLogModule.RegisterLogListing(MapUtilsLogName, LOCTEXT("MapUtilsLog", "Map Utils"), InitOptions);

    FMapUtilsTabSpawner::Register();

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateStatic(&FMapUtilsContextMenu::Register));
}

void FMapUtilsModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    FMapUtilsContextMenu::Unregister();
    FMapUtilsTabSpawner::Unregister();

    if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
    {
        FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
        MessageLogModule.UnregisterLogListing(MapUtilsLogName);
    }

    UE_LOG(LogMapUtils, Log, TEXT("MapUtils module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMapUtilsModule, MapUtils)
