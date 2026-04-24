#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class SWindow;

/**
 * Non-modal dialog that lists every property / meta change recorded in the
 * editor transaction buffer for a single actor (and its sub-objects).
 */
class SMapUtilsActorChangesDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMapUtilsActorChangesDialog) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AActor>, Actor)
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    static TSharedRef<SWindow> OpenWindow(AActor* Actor);

private:
    FReply OnCloseClicked();

    TWeakObjectPtr<AActor> TargetActor;
    TWeakPtr<SWindow> ParentWindow;
};
