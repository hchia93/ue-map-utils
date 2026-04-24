#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SWindow;
class ULevel;
class UWorld;

/**
 * Modal-style window showing all levels of a world and letting the user
 * pick one. Use via static OpenModal().
 */
class SMapUtilsLevelPickerDialog : public SCompoundWidget
{
public:
    struct FLevelItem
    {
        TWeakObjectPtr<ULevel> Level;
        FString DisplayName;
    };

    SLATE_BEGIN_ARGS(SMapUtilsLevelPickerDialog) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /**
     * Block until the user picks a level or cancels.
     * Returns the picked ULevel* (still valid at call time) or nullptr on cancel.
     */
    static ULevel* OpenModal(UWorld* World, TSharedPtr<SWindow> RootWindow);

private:
    TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FLevelItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void OnSelectionChanged(TSharedPtr<FLevelItem> Item, ESelectInfo::Type SelectInfo);
    FReply OnConfirmClicked();
    FReply OnCancelClicked();
    bool IsConfirmEnabled() const;

    TWeakObjectPtr<UWorld> World;
    TWeakPtr<SWindow> ParentWindow;
    TArray<TSharedPtr<FLevelItem>> LevelItems;
    TSharedPtr<SListView<TSharedPtr<FLevelItem>>> ListView;
    TWeakObjectPtr<ULevel> PickedLevel;
};
