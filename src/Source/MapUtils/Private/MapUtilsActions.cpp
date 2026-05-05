#include "MapUtilsActions.h"

#include "Exports/MapUtilsContextExporter.h"
#include "MapUtilsModule.h"
#include "Operations/MapUtilsBakeToInstanceMeshOps.h"
#include "Operations/MapUtilsBakeToMergedInstanceMeshOps.h"
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

    TArray<AStaticMeshActor*> GatherSelectedSMA()
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

    TArray<AActor*> GatherSelectedActorsAny()
    {
        TArray<AActor*> Result;
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
            if (AActor* A = Cast<AActor>(*It))
            {
                Result.Add(A);
            }
        }
        return Result;
    }
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
    else
    {
        // Only steal focus when there is something the user must act on.
        Log.Open(EMessageSeverity::Warning, true);
    }

    UE_LOG(LogMapUtils, Log, TEXT("AuditCurrentLevel: %d issue(s) found in %s"), IssueCount, *World->GetMapName());
}

void FMapUtilsActions::CreateBlockingVolumeFromSelection()
{
    const TArray<AActor*> Actors = GatherSelectedActorsAny();

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("CreateBVPage", "Create Blocking Volume for Actors"));

    if (Actors.IsEmpty())
    {
        Log.Warning(LOCTEXT("CreateBVNoSelection", "No actor selected. Select one or more in the Outliner / viewport first."));
        Log.Open(EMessageSeverity::Warning, true);
        return;
    }

    const FMapUtilsBlockingVolumeWrapResult Result = FMapUtilsBlockingVolumeOps::CreateBlockingVolumeForActors(Actors);

    if (Result.bSuccess)
    {
        const FText Message = FText::Format(LOCTEXT("CreateBVOK", "Created 1 BlockingVolume wrapping {0} actor(s). Skipped {1}. Sources preserved. Ctrl+Z to undo."), Result.SourceActorCount, Result.SkippedActorCount);
        Log.Info(Message);
        // Success path: keep MessageLog updated but don't steal focus.
    }
    else
    {
        const FText Message = Result.ErrorText.IsEmpty() ? LOCTEXT("CreateBVFail", "Create BlockingVolume failed. See Output Log.") : Result.ErrorText;
        Log.Error(Message);
        Log.Open(EMessageSeverity::Error, true);
    }
}

void FMapUtilsActions::BakeSelectedToInstanceMesh(UMaterialInterface* OverrideMaterial)
{
    const TArray<AStaticMeshActor*> Actors = GatherSelectedSMA();

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("BakeInstancePage", "Bake to Instance Mesh"));

    if (Actors.IsEmpty())
    {
        Log.Warning(LOCTEXT("BakeInstanceNoSelection", "No StaticMeshActor selected. Select one or more in the Outliner / viewport first."));
        Log.Open(EMessageSeverity::Warning, true);
        return;
    }

    const FMapUtilsBakeInstanceResult Result = FMapUtilsBakeToInstanceMeshOps::BakeToInstanceMesh(Actors, OverrideMaterial);

    if (Result.bSuccess)
    {
        const FText Message = FText::Format(LOCTEXT("BakeInstanceOK", "Baked {0} actor(s) into {1} ISM actor(s). Ctrl+Z to undo."), Result.SourceActorCount, Result.CreatedActorCount);
        Log.Info(Message);
    }
    else
    {
        const FText Message = Result.ErrorText.IsEmpty() ? LOCTEXT("BakeInstanceFail", "Bake to Instance Mesh failed. See Output Log.") : Result.ErrorText;
        Log.Error(Message);
    }

    // Surface partial failure (some sources spawned, some didn't) without forcing the user to scrape logs.
    for (const FString& FailedName : Result.FailedSourceNames)
    {
        Log.Warning(FText::Format(LOCTEXT("BakeInstancePartialFail", "Failed to spawn ISM actor for '{0}'. Source actor preserved."), FText::FromString(FailedName)));
    }

    // Open only on total or partial failure, otherwise leave the log silent.
    if (!Result.bSuccess || !Result.FailedSourceNames.IsEmpty())
    {
        Log.Open(Result.bSuccess ? EMessageSeverity::Warning : EMessageSeverity::Error, true);
    }
}

void FMapUtilsActions::BakeSelectedToMergedInstanceMesh(UMaterialInterface* OverrideMaterial)
{
    const TArray<AActor*> Actors = GatherSelectedActorsAny();

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("BakeMergedInstancePage", "Bake to Merged Instance Mesh"));

    if (Actors.IsEmpty())
    {
        Log.Warning(LOCTEXT("BakeMergedInstanceNoSelection", "No actor selected. Select StaticMeshActors or previously-baked ISMActors in the Outliner / viewport."));
        Log.Open(EMessageSeverity::Warning, true);
        return;
    }

    const FMapUtilsBakeMergedInstanceResult Result = FMapUtilsBakeToMergedInstanceMeshOps::BakeToMergedInstanceMesh(Actors, OverrideMaterial);

    if (Result.bUserCancelled)
    {
        return;
    }

    if (Result.bSuccess)
    {
        const FText Message = FText::Format(LOCTEXT("BakeMergedInstanceOK", "Merged {0} source(s) -> 1 ISM actor with {1} instance(s) across {2} ISMC group(s). Skipped {3} non-mesh actor(s). Ctrl+Z to undo."), Result.SourceActorCount, Result.InstanceCount, Result.GroupCount, Result.SkippedActorCount);
        Log.Info(Message);
    }
    else
    {
        const FText Message = Result.ErrorText.IsEmpty() ? LOCTEXT("BakeMergedInstanceFail", "Bake to Merged Instance Mesh failed. See Output Log.") : Result.ErrorText;
        Log.Error(Message);
        Log.Open(EMessageSeverity::Error, true);
    }
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
        Log.Open(EMessageSeverity::Error, true);
    }
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
        Log.Open(EMessageSeverity::Error, true);
    }
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
