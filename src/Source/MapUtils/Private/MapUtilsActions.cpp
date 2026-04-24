#include "MapUtilsActions.h"

#include "Exports/MapUtilsContextExporter.h"
#include "MapUtilsModule.h"
#include "Operations/MapUtilsBlockingVolumeOps.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging/MessageLog.h"
#include "Misc/PackageName.h"
#include "Misc/UObjectToken.h"
#include "Selection.h"

#define LOCTEXT_NAMESPACE "FMapUtilsActions"

namespace
{
    const FName MapUtilsLogName(TEXT("MapUtils"));
}

void FMapUtilsActions::AuditCurrentLevel()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("AuditCurrentLevel: no editor world available."));
        return;
    }

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(FText::Format(LOCTEXT("AuditPage", "Audit {0}"), FText::FromString(World->GetMapName())));

    int32 IssueCount = 0;

    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }

        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        if (!Component)
        {
            continue;
        }

        if (Component->GetStaticMesh() == nullptr)
        {
            const FText ActorName = FText::FromString(Actor->GetName());
            const FText LevelName = FText::FromString(GetLevelDisplayName(World, Actor->GetLevel()));
            const FText Message = FText::Format(LOCTEXT("NullStaticMesh", "StaticMesh is null on {0} ({1})"), ActorName, LevelName);
            Log.Error(Message)->AddToken(FUObjectToken::Create(Actor));
            ++IssueCount;
        }
    }

    if (IssueCount == 0)
    {
        Log.Info(LOCTEXT("NoIssues", "No issues found."));
    }

    Log.Open(EMessageSeverity::Info, true);

    UE_LOG(LogMapUtils, Log, TEXT("AuditCurrentLevel: %d issue(s) found in %s"), IssueCount, *World->GetMapName());
}

void FMapUtilsActions::ConvertSelectedToBlockingVolume()
{
    if (!GEditor)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ConvertSelectedToBlockingVolume: GEditor null."));
        return;
    }

    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection)
    {
        return;
    }

    TArray<AStaticMeshActor*> Selected;
    for (FSelectionIterator It(*Selection); It; ++It)
    {
        if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(*It))
        {
            Selected.Add(SMA);
        }
    }

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("ConvertPage", "Convert to BlockingVolume"));

    if (Selected.IsEmpty())
    {
        Log.Warning(LOCTEXT("NoSelection", "No StaticMeshActor in current selection. Select one or more in the Outliner / viewport first."));
        Log.Open(EMessageSeverity::Info, true);
        return;
    }

    const FMapUtilsBlockingVolumeConvertResult Result = FMapUtilsBlockingVolumeOps::ConvertActorsToBlockingVolumes(Selected);

    if (Result.bSuccess)
    {
        const int32 DestroyedCount = Result.DestroyedActorNames.Num();
        const int32 CreatedCount = Result.CreatedVolumes.Num();
        const FText Fmt = LOCTEXT("ConvertSuccess", "Converted {0} actor(s) into {1} BlockingVolume(s) across {2} cluster(s). Ctrl+Z to undo.");
        const FText Message = FText::Format(Fmt, DestroyedCount, CreatedCount, Result.ClusterCount);
        Log.Info(Message);
    }
    else
    {
        Log.Error(LOCTEXT("ConvertFailed", "Convert to BlockingVolume failed. See Output Log for details."));
    }

    Log.Open(EMessageSeverity::Info, true);

    UE_LOG(LogMapUtils, Log, TEXT("ConvertSelectedToBlockingVolume: %d -> %d (success=%d)"), Result.DestroyedActorNames.Num(), Result.CreatedVolumes.Num(), Result.bSuccess ? 1 : 0);
}

void FMapUtilsActions::ExportStaticMeshContext()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ExportStaticMeshContext: no editor world."));
        return;
    }

    const FMapUtilsContextExportResult Result = FMapUtilsContextExporter::ExportStaticMeshContext(World);

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("ExportStaticMeshPage", "Export StaticMesh Context"));
    if (Result.bSuccess)
    {
        const FText OutputPath = FText::FromString(Result.OutputPath);
        const FText Message = FText::Format(LOCTEXT("ExportStaticMeshOK", "Exported {0} actor(s) to {1}"), Result.ItemCount, OutputPath);
        Log.Info(Message);
    }
    else
    {
        Log.Error(LOCTEXT("ExportStaticMeshFail", "StaticMesh context export failed. See Output Log."));
    }
    Log.Open(EMessageSeverity::Info, true);
}

void FMapUtilsActions::ExportCollisionContext()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("ExportCollisionContext: no editor world."));
        return;
    }

    const FMapUtilsContextExportResult Result = FMapUtilsContextExporter::ExportCollisionContext(World);

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("ExportCollisionPage", "Export Collision Context"));
    if (Result.bSuccess)
    {
        const FText OutputPath = FText::FromString(Result.OutputPath);
        const FText Message = FText::Format(LOCTEXT("ExportCollisionOK", "Exported {0} candidate(s) to {1}"), Result.ItemCount, OutputPath);
        Log.Info(Message);
    }
    else
    {
        Log.Error(LOCTEXT("ExportCollisionFail", "Collision context export failed. See Output Log."));
    }
    Log.Open(EMessageSeverity::Info, true);
}

FString FMapUtilsActions::GetLevelDisplayName(UWorld* World, ULevel* Level)
{
    if (!Level)
    {
        return TEXT("<null>");
    }
    if (World && Level == World->PersistentLevel)
    {
        return TEXT("Persistent Level");
    }
    return FPackageName::GetShortName(Level->GetOutermost()->GetName());
}

#undef LOCTEXT_NAMESPACE
