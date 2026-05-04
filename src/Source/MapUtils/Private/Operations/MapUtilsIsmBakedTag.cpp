#include "Operations/MapUtilsIsmBakedTag.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace MapUtilsIsmBaked
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

    int32 TagAndLabelWithIndex(AActor* Actor, int32 Index)
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
        return Index + 1;
    }

    void TagAndLabel(AActor* Actor)
    {
        if (!Actor)
        {
            return;
        }
        TagAndLabelWithIndex(Actor, PeekNextLabelIndex(Actor->GetWorld()));
    }
}
