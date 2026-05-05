#include "Builder/MeshGridBuilderDetails.h"

#include "Builder/MeshGridBuilder.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IDetailCustomization> FMeshGridBuilderDetails::MakeInstance()
{
    return MakeShared<FMeshGridBuilderDetails>();
}

void FMeshGridBuilderDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }
    m_Target = Cast<AMeshGridBuilder>(Objects[0].Get());
    if (!m_Target.IsValid())
    {
        return;
    }

    // Hide inherited Actor / component clutter; the LD only needs Tool Setup / Grid / Tool Action.
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

    // ECategoryPriority::Important pins these three under Transform in stable order.
    DetailBuilder.EditCategory("Tool Setup", FText::GetEmpty(), ECategoryPriority::Important);
    IDetailCategoryBuilder& Grid = DetailBuilder.EditCategory("Grid", FText::GetEmpty(), ECategoryPriority::Important);
    IDetailCategoryBuilder& Action = DetailBuilder.EditCategory("Tool Action", FText::GetEmpty(), ECategoryPriority::Important);

    // -- Grid: explicit AddProperty first so GridSize renders above the Generate button.
    // Without this, AddCustomRow ends up above default properties in UE's default order. --
    Grid.AddProperty(DetailBuilder.GetProperty(TEXT("GridSize")));

    Grid.AddCustomRow(FText::FromString(TEXT("Generate")))
    .WholeRowContent()
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(FText::FromString(TEXT("Generate")))
        .ToolTipText(FText::FromString(TEXT("Spawn the M x N grid using TileMesh. Existing cells are wiped. Process is invoked at the end so connectivity-aware materials apply immediately.")))
        .OnClicked(this, &FMeshGridBuilderDetails::OnGenerateClicked)
    ];

    // -- Tool Action row 1: Process | Clear --
    Action.AddCustomRow(FText::FromString(TEXT("ProcessClear")))
    .WholeRowContent()
    [
        SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.f))
        + SUniformGridPanel::Slot(0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Process")))
            .ToolTipText(FText::FromString(TEXT("Recompute material per surviving cell from 4-neighbor topology.")))
            .OnClicked(this, &FMeshGridBuilderDetails::OnProcessClicked)
        ]
        + SUniformGridPanel::Slot(1, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString(TEXT("Clear")))
            .ToolTipText(FText::FromString(TEXT("Destroy all cell SMCs without touching the SceneRoot / sprite.")))
            .OnClicked(this, &FMeshGridBuilderDetails::OnClearClicked)
        ]
    ];

    // -- Tool Action row 2: Bake to ISM (whole row) --
    Action.AddCustomRow(FText::FromString(TEXT("Bake")))
    .WholeRowContent()
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(FText::FromString(TEXT("Bake to ISM")))
        .ToolTipText(FText::FromString(TEXT("Group cells by slot-0 material into ISMC, spawn a merged actor at the AABB center, then destroy this builder.")))
        .OnClicked(this, &FMeshGridBuilderDetails::OnBakeClicked)
    ];
}

FReply FMeshGridBuilderDetails::OnGenerateClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_Generate(); }
    return FReply::Handled();
}

FReply FMeshGridBuilderDetails::OnProcessClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_Process(); }
    return FReply::Handled();
}

FReply FMeshGridBuilderDetails::OnClearClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_Clear(); }
    return FReply::Handled();
}

FReply FMeshGridBuilderDetails::OnBakeClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_BakeToISM(); }
    return FReply::Handled();
}
