#pragma once

#include "CoreMinimal.h"
#include "Operations/MapUtilsDiffOps.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class AActor;
class SCheckBox;
class SWindow;

/**
 * Row holding one diff entry plus a per-row checkbox state.
 * Kept here because the row widget needs a shared pointer to read/write it.
 */
struct FMapUtilsDiffRow
{
    FMapUtilsDiffEntry Entry;
    bool bChecked = false;
};

/**
 * Slate widget for the Review Modified dialog. Use via static OpenWindow().
 *
 * The widget owns the root SWindow it lives in so it can survive async
 * interactions (Move sub-dialog, external editor tweaks, then refresh).
 */
class SMapUtilsDiffDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMapUtilsDiffDialog) {}
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Spawn a non-modal window containing this dialog and return it. */
    static TSharedRef<SWindow> OpenWindow();

private:
    void RefreshList();

    TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FMapUtilsDiffRow> Row, const TSharedRef<STableViewBase>& OwnerTable);

    FReply OnMoveClicked();
    FReply OnCloseClicked();
    FReply OnRefreshClicked();
    FReply OnSelectAllClicked();
    FReply OnSelectNoneClicked();

    TArray<AActor*> GetCheckedActors() const;

    TWeakPtr<SWindow> ParentWindow;
    TArray<TSharedPtr<FMapUtilsDiffRow>> Rows;
    TSharedPtr<SListView<TSharedPtr<FMapUtilsDiffRow>>> ListView;
};
