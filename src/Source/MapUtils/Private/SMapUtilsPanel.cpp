#include "SMapUtilsPanel.h"

#include "MapUtilsActions.h"
#include "Widgets/SMapUtilsDiffDialog.h"

#include "AssetRegistry/AssetData.h"
#include "Materials/MaterialInterface.h"
#include "PropertyCustomizationHelpers.h"
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
    const FMargin SectionHeaderPadding(0, 12, 0, 4);
    const FMargin ButtonPadding(0, 2);
    const FMargin SeparatorPadding(0, 8, 0, 0);

    auto MakeHeader = [](const FText& Text) -> TSharedRef<SWidget>
    {
        return SNew(STextBlock)
            .Text(Text)
            .TextStyle(FAppStyle::Get(), "LargeText");
    };

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

                // -- Tool Setup --
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SectionHeaderPadding)
                [
                    MakeHeader(LOCTEXT("ToolSetupHeader", "Tool Setup"))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0, 0, 8, 0)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("OverrideMaterialLabel", "Override Material"))
                        .ToolTipText(LOCTEXT("OverrideMaterialTooltip", "Optional. When set, every ISMC slot on baked actors is forced to this material. Source-side material override divergence is also ignored when grouping in the Merged bake, so visually identical walls collapse into one ISMC."))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SObjectPropertyEntryBox)
                        .AllowedClass(UMaterialInterface::StaticClass())
                        .ObjectPath(this, &SMapUtilsPanel::GetOverrideMaterialPath)
                        .OnObjectChanged(this, &SMapUtilsPanel::OnOverrideMaterialChanged)
                        .DisplayBrowse(true)
                        .DisplayUseSelected(true)
                        .DisplayThumbnail(false)
                    ]
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SeparatorPadding)
                [
                    SNew(SSeparator)
                ]

                // -- Level --
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SectionHeaderPadding)
                [
                    MakeHeader(LOCTEXT("LevelHeader", "Level"))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("BakeToInstance", "Bake Selected to Instance Mesh"))
                    .ToolTipText(LOCTEXT("BakeToInstanceTooltip", "Replace each selected StaticMeshActor with its own ISM actor (1 instance each), tagged ISM_Baked and labelled ISM_Baked_<idx>. No grouping, even if multiple actors share the same mesh. Sources destroyed. Undo-safe."))
                    .OnClicked(this, &SMapUtilsPanel::OnBakeToInstanceClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("BakeToMergedInstance", "Bake Selected to Merged Instance Mesh"))
                    .ToolTipText(LOCTEXT("BakeToMergedInstanceTooltip", "Merge selected StaticMeshActors AND previously-baked ISMActors into ONE ISM actor at the centroid of all instances. Different (mesh / materials / collision) tuples become separate ISMC inside one actor. Mismatched collision profiles trigger a confirmation dialog. Use this instead of UE's Group Actor. Sources destroyed."))
                    .OnClicked(this, &SMapUtilsPanel::OnBakeToMergedInstanceClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("CreateBV", "Create Blocking Volume for Actors"))
                    .ToolTipText(LOCTEXT("CreateBVTooltip", "Compute the combined world-space bounds of selected actors (StaticMeshActor, BP actors, anything with primitive components) and spawn ONE BlockingVolume sized to wrap them. Single-selection covers that actor; multi-selection wraps the AABB of all bounds (perpendicular walls produce an L-corner box, fine for blocking). Sources preserved. Existing BlockingVolumes in the selection are skipped. Undo-safe."))
                    .OnClicked(this, &SMapUtilsPanel::OnCreateBlockingVolumeClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SeparatorPadding)
                [
                    SNew(SSeparator)
                ]

                // -- Audit & Review --
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SectionHeaderPadding)
                [
                    MakeHeader(LOCTEXT("AuditReviewHeader", "Audit & Review"))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("AuditStaticMesh", "Audit StaticMesh References"))
                    .ToolTipText(LOCTEXT("AuditStaticMeshTooltip", "Scan current level for AStaticMeshActor with null StaticMesh. Results in Message Log with click-to-actor tokens."))
                    .OnClicked(this, &SMapUtilsPanel::OnAuditClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ReviewModified", "Review Modified Objects"))
                    .ToolTipText(LOCTEXT("ReviewModifiedTooltip", "Open a dialog listing actors touched in this editor session. Check entries and Move them to another sub-level, or click Details to inspect per-property transaction deltas on a single actor."))
                    .OnClicked(this, &SMapUtilsPanel::OnReviewModifiedClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SeparatorPadding)
                [
                    SNew(SSeparator)
                ]

                // -- Export --
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(SectionHeaderPadding)
                [
                    MakeHeader(LOCTEXT("ExportHeader", "Export"))
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ExportSM", "Export StaticMesh Context"))
                    .ToolTipText(LOCTEXT("ExportSMTooltip", "Write focused JSON (actors, mesh paths, materials, bounds) to Intermediate/MapUtilsContext/."))
                    .OnClicked(this, &SMapUtilsPanel::OnExportStaticMeshClicked)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(ButtonPadding)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("ExportColl", "Export Collision Context"))
                    .ToolTipText(LOCTEXT("ExportCollTooltip", "Write collision-enabled actor candidates JSON to Intermediate/MapUtilsContext/."))
                    .OnClicked(this, &SMapUtilsPanel::OnExportCollisionClicked)
                ]
            ]
        ]
    ];
}

FReply SMapUtilsPanel::OnBakeToInstanceClicked()
{
    FMapUtilsActions::BakeSelectedToInstanceMesh(OverrideMaterial.Get());
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnBakeToMergedInstanceClicked()
{
    FMapUtilsActions::BakeSelectedToMergedInstanceMesh(OverrideMaterial.Get());
    return FReply::Handled();
}

FString SMapUtilsPanel::GetOverrideMaterialPath() const
{
    if (UMaterialInterface* Mat = OverrideMaterial.Get())
    {
        return Mat->GetPathName();
    }
    return FString();
}

void SMapUtilsPanel::OnOverrideMaterialChanged(const FAssetData& AssetData)
{
    OverrideMaterial = Cast<UMaterialInterface>(AssetData.GetAsset());
}

FReply SMapUtilsPanel::OnCreateBlockingVolumeClicked()
{
    FMapUtilsActions::CreateBlockingVolumeFromSelection();
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnAuditClicked()
{
    FMapUtilsActions::AuditCurrentLevel();
    return FReply::Handled();
}

FReply SMapUtilsPanel::OnReviewModifiedClicked()
{
    SMapUtilsDiffDialog::OpenWindow();
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
