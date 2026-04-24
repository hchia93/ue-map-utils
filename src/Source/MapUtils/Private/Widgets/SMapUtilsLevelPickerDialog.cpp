#include "Widgets/SMapUtilsLevelPickerDialog.h"

#include "MapUtilsActions.h"
#include "MapUtilsModule.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMapUtilsLevelPickerDialog"

void SMapUtilsLevelPickerDialog::Construct(const FArguments& InArgs)
{
    World = InArgs._World;
    ParentWindow = InArgs._ParentWindow;

    UWorld* WorldPtr = World.Get();
    if (WorldPtr)
    {
        for (ULevel* Level : WorldPtr->GetLevels())
        {
            if (!Level)
            {
                continue;
            }
            TSharedPtr<FLevelItem> Item = MakeShared<FLevelItem>();
            Item->Level = Level;
            Item->DisplayName = FMapUtilsActions::GetLevelDisplayName(WorldPtr, Level);
            LevelItems.Add(Item);
        }

        LevelItems.Sort([](const TSharedPtr<FLevelItem>& A, const TSharedPtr<FLevelItem>& B)
        {
            return A->DisplayName < B->DisplayName;
        });
    }

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 6)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PickLevelHeader", "Pick destination level:"))
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SAssignNew(ListView, SListView<TSharedPtr<FLevelItem>>)
                .ListItemsSource(&LevelItems)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow(this, &SMapUtilsLevelPickerDialog::OnGenerateRow)
                .OnSelectionChanged(this, &SMapUtilsLevelPickerDialog::OnSelectionChanged)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 8, 0, 0)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FMargin(4, 0))

                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Confirm", "Confirm"))
                    .IsEnabled(this, &SMapUtilsLevelPickerDialog::IsConfirmEnabled)
                    .OnClicked(this, &SMapUtilsLevelPickerDialog::OnConfirmClicked)
                ]

                + SUniformGridPanel::Slot(1, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Cancel", "Cancel"))
                    .OnClicked(this, &SMapUtilsLevelPickerDialog::OnCancelClicked)
                ]
            ]
        ]
    ];
}

TSharedRef<ITableRow> SMapUtilsLevelPickerDialog::OnGenerateRow(
    TSharedPtr<FLevelItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TSharedPtr<FLevelItem>>, OwnerTable)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Item.IsValid() ? Item->DisplayName : FString()))
            .Margin(FMargin(4, 2))
        ];
}

void SMapUtilsLevelPickerDialog::OnSelectionChanged(
    TSharedPtr<FLevelItem> Item, ESelectInfo::Type /*SelectInfo*/)
{
    if (Item.IsValid())
    {
        PickedLevel = Item->Level;
    }
    else
    {
        PickedLevel = nullptr;
    }
}

FReply SMapUtilsLevelPickerDialog::OnConfirmClicked()
{
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

FReply SMapUtilsLevelPickerDialog::OnCancelClicked()
{
    PickedLevel = nullptr;
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

bool SMapUtilsLevelPickerDialog::IsConfirmEnabled() const
{
    return PickedLevel.IsValid();
}

ULevel* SMapUtilsLevelPickerDialog::OpenModal(UWorld* WorldPtr, TSharedPtr<SWindow> RootWindow)
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("MoveToLevelTitle", "Move to Level"))
        .ClientSize(FVector2D(360, 420))
        .SupportsMaximize(false)
        .SupportsMinimize(false);

    TSharedPtr<SMapUtilsLevelPickerDialog> Dialog;
    Window->SetContent(
        SAssignNew(Dialog, SMapUtilsLevelPickerDialog)
        .World(WorldPtr)
        .ParentWindow(Window)
    );

    if (RootWindow.IsValid())
    {
        FSlateApplication::Get().AddModalWindow(Window, RootWindow, false);
    }
    else
    {
        FSlateApplication::Get().AddModalWindow(Window, nullptr, false);
    }

    ULevel* Result = Dialog.IsValid() ? Dialog->PickedLevel.Get() : nullptr;
    const FString PickedName = Result ? FMapUtilsActions::GetLevelDisplayName(WorldPtr, Result) : FString(TEXT("<cancel>"));
    UE_LOG(LogMapUtils, Log, TEXT("LevelPicker: picked %s"), *PickedName);
    return Result;
}

#undef LOCTEXT_NAMESPACE
