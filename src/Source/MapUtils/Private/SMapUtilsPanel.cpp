#include "SMapUtilsPanel.h"

#include "MapUtilsActions.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMapUtilsPanel"

void SMapUtilsPanel::Construct(const FArguments& InArgs)
{
    const FMargin GroupHeaderPadding(0, 8, 0, 4);
    const FMargin ButtonPadding(0, 2);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(GroupHeaderPadding)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AuditHeader", "Audit"))
                    .TextStyle(FAppStyle::Get(), "LargeText")
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("AuditStaticMesh", "Audit StaticMesh Refs (current level)"))
                    .ToolTipText(LOCTEXT("AuditStaticMeshTooltip",
                        "Scan current level for AStaticMeshActor with null StaticMesh. "
                        "Results in Message Log with click-to-actor tokens."))
                    .OnClicked(this, &SMapUtilsPanel::OnAuditClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(GroupHeaderPadding)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(GroupHeaderPadding)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ActionsHeader", "Actions"))
                    .TextStyle(FAppStyle::Get(), "LargeText")
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ConvertToBV", "Convert Selected to Blocking Volume"))
                    .ToolTipText(LOCTEXT("ConvertToBVTooltip",
                        "Merge selected StaticMeshActors into BlockingVolume(s). "
                        "New volumes spawn in the level of the first selected actor. Undo-safe."))
                    .OnClicked(this, &SMapUtilsPanel::OnConvertClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(GroupHeaderPadding)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(GroupHeaderPadding)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ExportHeader", "Export (AI consumable)"))
                    .TextStyle(FAppStyle::Get(), "LargeText")
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ExportSM", "Export StaticMesh Context"))
                    .ToolTipText(LOCTEXT("ExportSMTooltip",
                        "Write focused JSON (actors, mesh paths, materials, bounds) "
                        "to Intermediate/MapUtilsContext/."))
                    .OnClicked(this, &SMapUtilsPanel::OnExportStaticMeshClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ExportColl", "Export Collision Context"))
                    .ToolTipText(LOCTEXT("ExportCollTooltip",
                        "Write collision-enabled actor candidates JSON "
                        "to Intermediate/MapUtilsContext/."))
                    .OnClicked(this, &SMapUtilsPanel::OnExportCollisionClicked)
                ]
            ]
        ]
    ];
}

FReply SMapUtilsPanel::OnAuditClicked()
{
    FMapUtilsActions::AuditCurrentLevel();
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnConvertClicked()
{
    FMapUtilsActions::ConvertSelectedToBlockingVolume();
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnExportStaticMeshClicked()
{
    FMapUtilsActions::ExportStaticMeshContext();
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnExportCollisionClicked()
{
    FMapUtilsActions::ExportCollisionContext();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
