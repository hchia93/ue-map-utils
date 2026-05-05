#include "Builder/MeshChainBuilderDetails.h"

#include "Builder/MeshChainBuilder.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IDetailCustomization> FMeshChainBuilderDetails::MakeInstance()
{
    return MakeShared<FMeshChainBuilderDetails>();
}

void FMeshChainBuilderDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }
    m_Target = Cast<AMeshChainBuilder>(Objects[0].Get());
    if (!m_Target.IsValid())
    {
        return;
    }

    // Hide inherited Actor / component clutter; this tool only cares about Tool Action, Tool Setup, Collision.
    const TArray<FName> HiddenCategories = {
        TEXT("ActorTick"), TEXT("Actor Tick"), TEXT("Tick"),
        TEXT("RayTracing"), TEXT("TextureStreaming"), TEXT("MeshPainting"), TEXT("Mesh Painting"),
        TEXT("MaterialCache"), TEXT("Material Cache"), TEXT("HLOD"), TEXT("Mobile"),
        TEXT("VirtualTexture"), TEXT("Sprite"), TEXT("Input"), TEXT("Events"),
        TEXT("MaterialParameters"), TEXT("WorldPartition"), TEXT("LevelInstance"),
        TEXT("DataLayers"), TEXT("Replication"), TEXT("ComponentReplication"), TEXT("Component Replication"),
        TEXT("Actor"), TEXT("Variable"), TEXT("Activation"), TEXT("Tags"), TEXT("Cooking"),
        TEXT("AssetUserData"), TEXT("Rendering"), TEXT("LOD"), TEXT("Physics"), TEXT("Lighting"),
        TEXT("ComponentTick"), TEXT("Component Tick"), TEXT("Networking"),
        TEXT("Components"),
    };
    for (const FName& Cat : HiddenCategories)
    {
        DetailBuilder.HideCategory(Cat);
    }

    // ECategoryPriority::Important pins both action and setup just below Transform.
    DetailBuilder.EditCategory("Tool Setup", FText::GetEmpty(), ECategoryPriority::Important);
    IDetailCategoryBuilder& Actions = DetailBuilder.EditCategory("Tool Action", FText::GetEmpty(), ECategoryPriority::Important);

    Actions.AddCustomRow(FText::FromString(TEXT("Status")))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text(this, &FMeshChainBuilderDetails::GetStatusText)
    ];

    // Row 1: directional add. Each click commits one node. Order goes widest-left to widest-right
    // (45°L, 90°L, Forward, 90°R, 45°R) so the spatial layout matches what the click does.
    Actions.AddCustomRow(FText::FromString(TEXT("AddNode")))
    .WholeRowContent()
    [
        SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.f))
        + SUniformGridPanel::Slot(0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Add 45° L")))
            .ToolTipText(FText::FromString(TEXT("Turn 45° left, then place a Main slot. If MeshC is set, a Corner is inserted at the turn (sphere corner: angle-invariant).")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeTurnClicked, 45.f, false)
        ]
        + SUniformGridPanel::Slot(1, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Add 90° L")))
            .ToolTipText(FText::FromString(TEXT("Turn 90° left, then place a Main slot. If MeshC is set, a Corner is inserted at the turn.")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeTurnClicked, 90.f, false)
        ]
        + SUniformGridPanel::Slot(2, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Add Forward")))
            .ToolTipText(FText::FromString(TEXT("Place one Main slot ahead. If MeshB is set, a Transition is inserted between consecutive Mains.")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeForwardClicked)
        ]
        + SUniformGridPanel::Slot(3, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Add 90° R")))
            .ToolTipText(FText::FromString(TEXT("Turn 90° right, then place a Main slot. If MeshC is set, a Corner is inserted at the turn.")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeTurnClicked, 90.f, true)
        ]
        + SUniformGridPanel::Slot(4, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Add 45° R")))
            .ToolTipText(FText::FromString(TEXT("Turn 45° right, then place a Main slot. If MeshC is set, a Corner is inserted at the turn (sphere corner: angle-invariant).")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeTurnClicked, 45.f, true)
        ]
    ];

    // Row 2: edit lifecycle.
    Actions.AddCustomRow(FText::FromString(TEXT("Lifecycle")))
    .WholeRowContent()
    [
        SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.f))
        + SUniformGridPanel::Slot(0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Undo")))
            .ToolTipText(FText::FromString(TEXT("Remove the last added node (and any turn that came with it).")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnUndoClicked)
        ]
        + SUniformGridPanel::Slot(1, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Clear")))
            .ToolTipText(FText::FromString(TEXT("Remove all spawned slots and steps.")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnClearClicked)
        ]
        + SUniformGridPanel::Slot(2, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Regenerate")))
            .ToolTipText(FText::FromString(TEXT("Discard all per-slot transform overrides and rebuild from baseline.")))
            .OnClicked(this, &FMeshChainBuilderDetails::OnRegenerateClicked)
        ]
    ];

    // Row 3: bake to ISM.
    Actions.AddCustomRow(FText::FromString(TEXT("Bake")))
    .WholeRowContent()
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(FText::FromString(TEXT("Bake to ISM")))
        .ToolTipText(FText::FromString(TEXT("Spawn a standalone actor with one InstancedStaticMesh per role (Main / Transition / Corner). The builder actor is left intact.")))
        .OnClicked(this, &FMeshChainBuilderDetails::OnBakeClicked)
    ];
}

FText FMeshChainBuilderDetails::GetStatusText() const
{
    if (!m_Target.IsValid())
    {
        return FText::GetEmpty();
    }
    return FText::FromString(FString::Printf(TEXT("Main count: %d"), m_Target->GetMainCount()));
}

FReply FMeshChainBuilderDetails::OnAddNodeForwardClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_AddNodeForward(); }
    return FReply::Handled();
}

FReply FMeshChainBuilderDetails::OnAddNodeTurnClicked(float AngleDeg, bool bRight)
{
    if (m_Target.IsValid())
    {
        if (bRight)
        {
            m_Target->Editor_AddNodeRight(AngleDeg);
        }
        else
        {
            m_Target->Editor_AddNodeLeft(AngleDeg);
        }
    }
    return FReply::Handled();
}

FReply FMeshChainBuilderDetails::OnUndoClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_RemoveLast(); }
    return FReply::Handled();
}

FReply FMeshChainBuilderDetails::OnClearClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_ClearChain(); }
    return FReply::Handled();
}

FReply FMeshChainBuilderDetails::OnRegenerateClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_RegenerateChain(); }
    return FReply::Handled();
}

FReply FMeshChainBuilderDetails::OnBakeClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_BakeToISM(); }
    return FReply::Handled();
}
