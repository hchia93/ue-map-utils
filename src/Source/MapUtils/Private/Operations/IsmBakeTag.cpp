#include "Operations/IsmBakeTag.h"

#include "EditorActorFolders.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Folder.h"
#include "GameFramework/Actor.h"

namespace IsmBaked
{
    const FName Tag(TEXT("ISM_Baked"));

    int32 PeekNextLabelIndex(UWorld* World)
    {
        if (!World)
        {
            return 0;
        }

        const FString Prefix = TEXT("ISM_Baked_");
        const int32 PrefixLen = Prefix.Len();
        int32 NextIdx = 0;

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const FString Label = It->GetActorLabel();
            if (!Label.StartsWith(Prefix))
            {
                continue;
            }
            const FString Suffix = Label.Mid(PrefixLen);
            if (Suffix.IsNumeric())
            {
                NextIdx = FMath::Max(NextIdx, FCString::Atoi(*Suffix) + 1);
            }
        }

        return NextIdx;
    }

    int32 TagAndLabelWithIndex(AActor* Actor, int32 Index, FName FallbackFolderPath)
    {
        if (!Actor)
        {
            return Index;
        }
        if (!Actor->Tags.Contains(Tag))
        {
            Actor->Tags.Add(Tag);
        }
        Actor->SetActorLabel(FString::Printf(TEXT("ISM_Baked_%d"), Index));
        // SpawnActor bypasses the outliner's "Make Current Folder" state; re-apply it so every baked
        // ISM actor lands where freshly-placed editor actors would. Fall back to the caller-supplied
        // folder (typically the builder's or source actor's) when current is unset. NAME_None on
        // both means "leave at root" — SetFolderPath is skipped to avoid an unnecessary Modify.
        if (UWorld* World = Actor->GetWorld())
        {
            const FFolder CurrentFolder = FActorFolders::Get().GetActorEditorContextFolder(*World);
            const FName TargetFolder = !CurrentFolder.IsNone()
                ? CurrentFolder.GetPath()
                : FallbackFolderPath;
            if (!TargetFolder.IsNone())
            {
                Actor->SetFolderPath(TargetFolder);
            }
        }
        return Index + 1;
    }

    void TagAndLabel(AActor* Actor, FName FallbackFolderPath)
    {
        if (!Actor)
        {
            return;
        }
        TagAndLabelWithIndex(Actor, PeekNextLabelIndex(Actor->GetWorld()), FallbackFolderPath);
    }
}
