#include "Widgets/SMapUtilsDiffDialog.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsDiffOps.h"
#include "Operations/MapUtilsMoveActorsToLevelOps.h"
#include "Widgets/SMapUtilsActorChangesDialog.h"
#include "Widgets/SMapUtilsLevelPickerDialog.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "Logging/MessageLog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMapUtilsDiffDialog"

namespace
{
    const FName MapUtilsLogName(TEXT("MapUtils"));

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }
}

class SMapUtilsDiffRowWidget : public SMultiColumnTableRow<TSharedPtr<FMapUtilsDiffRow>>
{
public:
    SLATE_BEGIN_ARGS(SMapUtilsDiffRowWidget) {}
        SLATE_ARGUMENT(TSharedPtr<FMapUtilsDiffRow>, Row)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
    {
        Row = InArgs._Row;
        SMultiColumnTableRow<TSharedPtr<FMapUtilsDiffRow>>::Construct(
            FSuperRowType::FArguments(), OwnerTable);
    }

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
    {
        if (ColumnName == TEXT("Check"))
        {
            return SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .Padding(4, 0)
                .AutoWidth()
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]()
                    {
                        return Row.IsValid() && Row->bChecked
                            ? ECheckBoxState::Checked
                            : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                    {
                        if (Row.IsValid())
                        {
                            Row->bChecked = (NewState == ECheckBoxState::Checked);
                        }
                    })
                ];
        }

        if (ColumnName == TEXT("Actor"))
        {
            return SNew(STextBlock)
                .Margin(FMargin(4, 2))
                .Text(FText::FromString(Row.IsValid() ? Row->Entry.ActorDisplayName : FString()));
        }

        if (ColumnName == TEXT("Level"))
        {
            return SNew(STextBlock)
                .Margin(FMargin(4, 2))
                .Text(FText::FromString(Row.IsValid() ? Row->Entry.LevelDisplayName : FString()));
        }

        if (ColumnName == TEXT("Details"))
        {
            return SNew(SButton)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(LOCTEXT("DetailsTip", "Show transaction property deltas for this actor."))
                .OnClicked_Lambda([this]()
                {
                    if (Row.IsValid())
                    {
                        if (AActor* Actor = Row->Entry.Actor.Get())
                        {
                            SMapUtilsActorChangesDialog::OpenWindow(Actor);
                        }
                    }
                    return FReply::Handled();
                })
                [
                    SNew(SImage)
                    .Image(FAppStyle::Get().GetBrush("Icons.Details"))
                    .ColorAndOpacity(FSlateColor::UseForeground())
                ];
        }

        return SNullWidget::NullWidget;
    }

private:
    TSharedPtr<FMapUtilsDiffRow> Row;
};

void SMapUtilsDiffDialog::Construct(const FArguments& InArgs)
{
    ParentWindow = InArgs._ParentWindow;

    const FMargin SectionPadding(0, 6);
    const FMargin ButtonPadding(4, 0);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Header",
                    "Actors touched in this editor session. Checkboxes filter Move."))
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Refresh", "Refresh"))
                    .ToolTipText(LOCTEXT("RefreshTip", "Rescan this session's modified actors."))
                    .OnClicked(this, &SMapUtilsDiffDialog::OnRefreshClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SelectAll", "All"))
                    .OnClicked(this, &SMapUtilsDiffDialog::OnSelectAllClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SelectNone", "None"))
                    .OnClicked(this, &SMapUtilsDiffDialog::OnSelectNoneClicked)
                ]
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0, 4)
            [
                SAssignNew(ListView, SListView<TSharedPtr<FMapUtilsDiffRow>>)
                .ListItemsSource(&Rows)
                .SelectionMode(ESelectionMode::None)
                .OnGenerateRow(this, &SMapUtilsDiffDialog::OnGenerateRow)
                .HeaderRow
                (
                    SNew(SHeaderRow)
                    + SHeaderRow::Column(TEXT("Check"))
                      .DefaultLabel(FText::GetEmpty())
                      .FixedWidth(32)
                    + SHeaderRow::Column(TEXT("Actor"))
                      .DefaultLabel(LOCTEXT("ColActor", "Actor"))
                      .FillWidth(0.55f)
                    + SHeaderRow::Column(TEXT("Level"))
                      .DefaultLabel(LOCTEXT("ColLevel", "Level"))
                      .FillWidth(0.45f)
                    + SHeaderRow::Column(TEXT("Details"))
                      .DefaultLabel(FText::GetEmpty())
                      .FixedWidth(32)
                )
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4, 0, 0)
            [
                SNew(SSeparator)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 6, 0, 0)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FMargin(4, 0))

                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Move", "Move"))
                    .ToolTipText(LOCTEXT("MoveTip", "Move checked actors to another sub-level."))
                    .OnClicked(this, &SMapUtilsDiffDialog::OnMoveClicked)
                ]

                + SUniformGridPanel::Slot(1, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Close", "Close"))
                    .OnClicked(this, &SMapUtilsDiffDialog::OnCloseClicked)
                ]
            ]
        ]
    ];

    RefreshList();
}

void SMapUtilsDiffDialog::RefreshList()
{
    UWorld* World = GetEditorWorld();

    TSet<AActor*> PreviouslyChecked;
    for (const TSharedPtr<FMapUtilsDiffRow>& OldRow : Rows)
    {
        if (OldRow.IsValid() && OldRow->bChecked)
        {
            if (AActor* Actor = OldRow->Entry.Actor.Get())
            {
                PreviouslyChecked.Add(Actor);
            }
        }
    }

    Rows.Reset();

    if (World)
    {
        const TArray<FMapUtilsDiffEntry> Entries = FMapUtilsDiffOps::ScanModifiedActors(World);
        Rows.Reserve(Entries.Num());
        for (const FMapUtilsDiffEntry& Entry : Entries)
        {
            TSharedPtr<FMapUtilsDiffRow> Row = MakeShared<FMapUtilsDiffRow>();
            Row->Entry = Entry;
            Row->bChecked = PreviouslyChecked.Contains(Entry.Actor.Get());
            Rows.Add(Row);
        }
    }

    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SMapUtilsDiffDialog::OnGenerateRow(
    TSharedPtr<FMapUtilsDiffRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SMapUtilsDiffRowWidget, OwnerTable).Row(Row);
}

FReply SMapUtilsDiffDialog::OnMoveClicked()
{
    TArray<AActor*> Checked = GetCheckedActors();

    FMessageLog Log(MapUtilsLogName);
    Log.NewPage(LOCTEXT("MovePage", "Move Modified Actors"));

    if (Checked.IsEmpty())
    {
        Log.Warning(LOCTEXT("NoCheckedActors",
            "No actors checked. Tick one or more entries first."));
        Log.Open(EMessageSeverity::Info, true);
        return FReply::Handled();
    }

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        Log.Error(LOCTEXT("NoWorld", "No editor world."));
        Log.Open(EMessageSeverity::Info, true);
        return FReply::Handled();
    }

    ULevel* DestLevel = SMapUtilsLevelPickerDialog::OpenModal(World, ParentWindow.Pin());
    if (!DestLevel)
    {
        // User cancelled — do not surface as error.
        return FReply::Handled();
    }

    const FMapUtilsMoveResult Result = FMapUtilsMoveActorsToLevelOps::MoveActorsToLevel(Checked, DestLevel);

    if (Result.bSuccess)
    {
        const FText LevelName = FText::FromString(Result.DestLevelName);
        const FText Message = FText::Format(LOCTEXT("MoveOK", "Moved {0} actor(s) to {1}. Ctrl+Z to undo."), Result.MovedCount, LevelName);
        Log.Info(Message);
    }
    else
    {
        Log.Error(LOCTEXT("MoveFail", "Move to level failed (no actors moved). See Output Log."));
    }
    Log.Open(EMessageSeverity::Info, true);

    RefreshList();
    return FReply::Handled();
}

FReply SMapUtilsDiffDialog::OnCloseClicked()
{
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

FReply SMapUtilsDiffDialog::OnRefreshClicked()
{
    RefreshList();
    return FReply::Handled();
}

FReply SMapUtilsDiffDialog::OnSelectAllClicked()
{
    for (TSharedPtr<FMapUtilsDiffRow>& Row : Rows)
    {
        if (Row.IsValid())
        {
            Row->bChecked = true;
        }
    }
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
    return FReply::Handled();
}

FReply SMapUtilsDiffDialog::OnSelectNoneClicked()
{
    for (TSharedPtr<FMapUtilsDiffRow>& Row : Rows)
    {
        if (Row.IsValid())
        {
            Row->bChecked = false;
        }
    }
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
    return FReply::Handled();
}

TArray<AActor*> SMapUtilsDiffDialog::GetCheckedActors() const
{
    TArray<AActor*> Result;
    Result.Reserve(Rows.Num());
    for (const TSharedPtr<FMapUtilsDiffRow>& Row : Rows)
    {
        if (!Row.IsValid() || !Row->bChecked)
        {
            continue;
        }
        if (AActor* Actor = Row->Entry.Actor.Get())
        {
            Result.Add(Actor);
        }
    }
    return Result;
}

TSharedRef<SWindow> SMapUtilsDiffDialog::OpenWindow()
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("DiffTitle", "Review Modified Actors"))
        .ClientSize(FVector2D(520, 520))
        .SupportsMaximize(true)
        .SupportsMinimize(false);

    TSharedPtr<SMapUtilsDiffDialog> Dialog;
    Window->SetContent(
        SAssignNew(Dialog, SMapUtilsDiffDialog)
        .ParentWindow(Window)
    );

    FSlateApplication::Get().AddWindow(Window);
    UE_LOG(LogMapUtils, Log, TEXT("DiffDialog opened."));
    return Window;
}

#undef LOCTEXT_NAMESPACE
