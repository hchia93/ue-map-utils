#include "Builder/MeshChainBuilderDetails.h"

#include "Builder/MeshChainBuilder.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/StaticMesh.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MeshChainBuilderDetails"

namespace MeshChainBuilderDetailsLocal
{
    static FText GetProfileLabel(const FMeshBuilderProfile& Profile, int32 IndexFallback)
    {
        if (Profile.Mesh)
        {
            return FText::FromString(Profile.Mesh->GetName());
        }
        return FText::Format(LOCTEXT("EmptyProfileFmt", "Profile #{0}"), FText::AsNumber(IndexFallback));
    }
}

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

    // Canonical Epic pattern (see FCustomPrimitiveDataCustomization): hook SetOnNumElementsChanged
    // on the array property handle, call IPropertyUtilities::RequestForceRefresh on size change.
    // RequestForceRefresh re-runs CustomizeDetails on next tick (RequestRefresh skips the rebuild;
    // ForceRefresh is synchronous and unsafe mid-PostEditChange).
    TSharedRef<IPropertyUtilities> PropertyUtilities = DetailBuilder.GetPropertyUtilities();
    TWeakPtr<IPropertyUtilities> WeakUtils = PropertyUtilities.ToWeakPtr();
    FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda(
        [WeakUtils]
        {
            if (TSharedPtr<IPropertyUtilities> Pinned = WeakUtils.Pin())
            {
                Pinned->RequestForceRefresh();
            }
        });
    auto HookArray = [&](FName PropertyName)
    {
        TSharedPtr<IPropertyHandle> Handle = DetailBuilder.GetProperty(PropertyName);
        if (!Handle.IsValid())
        {
            return;
        }
        TSharedPtr<IPropertyHandleArray> AsArray = Handle->AsArray();
        if (!AsArray.IsValid())
        {
            return;
        }
        // Array size change: redraw row count.
        AsArray->SetOnNumElementsChanged(RefreshDelegate);
        // Per-element Mesh-field hook only. Blanket ChildPropertyValueChanged fires during slider
        // drag inside Template (FTransform), tearing down the slider widget mid-drag.
        uint32 NumElements = 0;
        AsArray->GetNumElements(NumElements);
        for (uint32 i = 0; i < NumElements; ++i)
        {
            TSharedPtr<IPropertyHandle> ElementHandle = AsArray->GetElement(i);
            if (!ElementHandle.IsValid())
            {
                continue;
            }
            TSharedPtr<IPropertyHandle> MeshHandle = ElementHandle->GetChildHandle(FName(TEXT("Mesh")));
            if (MeshHandle.IsValid())
            {
                MeshHandle->SetOnPropertyValueChanged(RefreshDelegate);
            }
        }
    };
    HookArray(FName(TEXT("ForwardProfiles")));
    HookArray(FName(TEXT("CornerProfiles")));

    IDetailCategoryBuilder& Actions = DetailBuilder.EditCategory("Tool Action", FText::GetEmpty(), ECategoryPriority::Important);
    Actions.SetSortOrder(0);
    DetailBuilder.EditCategory("Tool Setup", FText::GetEmpty(), ECategoryPriority::Important).SetSortOrder(1);

    Actions.AddCustomRow(LOCTEXT("StatusRowFilter", "Status"))
    .WholeRowContent()
    [
        SNew(STextBlock).Text(this, &FMeshChainBuilderDetails::GetStatusText)
    ];

    BuildCornerProfileRows(Actions);
    BuildForwardProfileRows(Actions);
    BuildLifecycleRow(Actions);
    BuildBakeRow(Actions);
}

void FMeshChainBuilderDetails::BuildForwardProfileRows(IDetailCategoryBuilder& Category)
{
    using namespace MeshChainBuilderDetailsLocal;

    AMeshChainBuilder* Builder = m_Target.Get();
    if (!Builder)
    {
        return;
    }

    const TArray<FMeshBuilderProfile>& Profiles = Builder->GetForwardProfiles();
    if (Profiles.Num() == 0)
    {
        Category.AddCustomRow(LOCTEXT("NoProfilesFilter", "NoForwardProfiles"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("NoForwardProfilesHint", "Add an entry to Forward Profiles below to spawn add-node buttons."))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
        return;
    }

    Category.AddCustomRow(LOCTEXT("ForwardHeaderFilter", "ForwardProfiles"))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text(LOCTEXT("ForwardHeader", "Add Node"))
        .Font(IDetailLayoutBuilder::GetDetailFontBold())
        .Justification(ETextJustify::Center)
    ];

    for (int32 i = 0; i < Profiles.Num(); ++i)
    {
        const FMeshBuilderProfile& Profile = Profiles[i];
        const FGuid CapturedId = Profile.ProfileId;
        const FText Label = GetProfileLabel(Profile, i);
        // No mesh -> the placement code would silently skip this profile. Grey the buttons so LD sees the gate.
        const bool bHasMesh = Profile.Mesh != nullptr;

        Category.AddCustomRow(FText::Format(LOCTEXT("ForwardRowFilterFmt", "Forward {0}"), Label))
        .WholeRowContent()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(0.4f)
            .Padding(FMargin(2.f, 4.f))
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(Label)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.6f)
            [
                SNew(SUniformGridPanel)
                .IsEnabled(bHasMesh)
                .SlotPadding(FMargin(2.f))
                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Btn45L", "45L"))
                    .ToolTipText(LOCTEXT("Btn45LTip", "Turn 45° left, then place this Forward profile."))
                    .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeClicked, CapturedId, -45.f)
                ]
                + SUniformGridPanel::Slot(1, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Btn90L", "90L"))
                    .ToolTipText(LOCTEXT("Btn90LTip", "Turn 90° left, then place this Forward profile."))
                    .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeClicked, CapturedId, -90.f)
                ]
                + SUniformGridPanel::Slot(2, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("BtnFwd", "Fwd"))
                    .ToolTipText(LOCTEXT("BtnFwdTip", "Place this Forward profile straight ahead. No corner is inserted."))
                    .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeClicked, CapturedId, 0.f)
                ]
                + SUniformGridPanel::Slot(3, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Btn90R", "90R"))
                    .ToolTipText(LOCTEXT("Btn90RTip", "Turn 90° right, then place this Forward profile."))
                    .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeClicked, CapturedId, 90.f)
                ]
                + SUniformGridPanel::Slot(4, 0)
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .Text(LOCTEXT("Btn45R", "45R"))
                    .ToolTipText(LOCTEXT("Btn45RTip", "Turn 45° right, then place this Forward profile."))
                    .OnClicked(this, &FMeshChainBuilderDetails::OnAddNodeClicked, CapturedId, 45.f)
                ]
            ]
        ];
    }
}

void FMeshChainBuilderDetails::BuildCornerProfileRows(IDetailCategoryBuilder& Category)
{
    using namespace MeshChainBuilderDetailsLocal;

    AMeshChainBuilder* Builder = m_Target.Get();
    if (!Builder)
    {
        return;
    }

    const TArray<FMeshBuilderProfile>& Profiles = Builder->GetCornerProfiles();
    if (Profiles.Num() == 0)
    {
        return;
    }

    Category.AddCustomRow(LOCTEXT("CornerHeaderFilter", "ActiveCorner"))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text(LOCTEXT("CornerHeader", "Active Corner"))
        .Justification(ETextJustify::Center)
        .Font(IDetailLayoutBuilder::GetDetailFontBold())
    ];

    // None option so LD can clear the active corner (turn clicks then place no corner mesh).
    Category.AddCustomRow(LOCTEXT("CornerNoneFilter", "CornerNone"))
    .WholeRowContent()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(FMargin(2.f, 2.f))
        .VAlign(VAlign_Center)
        [
            SNew(SCheckBox)
            .Style(FAppStyle::Get(), "RadioButton")
            .IsChecked(this, &FMeshChainBuilderDetails::IsActiveCorner, FGuid())
            .OnCheckStateChanged(this, &FMeshChainBuilderDetails::OnSetActiveCorner, FGuid())
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .Padding(FMargin(6.f, 2.f))
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("CornerNone", "None"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ]
    ];

    for (int32 i = 0; i < Profiles.Num(); ++i)
    {
        const FMeshBuilderProfile& Profile = Profiles[i];
        const FGuid CapturedId = Profile.ProfileId;
        const FText Label = GetProfileLabel(Profile, i);

        Category.AddCustomRow(FText::Format(LOCTEXT("CornerRowFilterFmt", "Corner {0}"), Label))
        .WholeRowContent()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(FMargin(2.f, 2.f))
            .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .Style(FAppStyle::Get(), "RadioButton")
                .IsChecked(this, &FMeshChainBuilderDetails::IsActiveCorner, CapturedId)
                .OnCheckStateChanged(this, &FMeshChainBuilderDetails::OnSetActiveCorner, CapturedId)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            .Padding(FMargin(6.f, 2.f))
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(Label)
            ]
        ];
    }
}

void FMeshChainBuilderDetails::BuildLifecycleRow(IDetailCategoryBuilder& Category)
{
    Category.AddCustomRow(LOCTEXT("LifecycleFilter", "Lifecycle"))
    .WholeRowContent()
    [
        SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.f))
        + SUniformGridPanel::Slot(0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Undo", "Undo"))
            .ToolTipText(LOCTEXT("UndoTip", "Remove the last added step."))
            .OnClicked(this, &FMeshChainBuilderDetails::OnUndoClicked)
        ]
        + SUniformGridPanel::Slot(1, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Clear", "Clear"))
            .ToolTipText(LOCTEXT("ClearTip", "Remove all spawned slots and steps."))
            .OnClicked(this, &FMeshChainBuilderDetails::OnClearClicked)
        ]
        + SUniformGridPanel::Slot(2, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("Regen", "Regenerate"))
            .ToolTipText(LOCTEXT("RegenTip", "Discard per-slot transform overrides and rebuild from baseline."))
            .OnClicked(this, &FMeshChainBuilderDetails::OnRegenerateClicked)
        ]
    ];
}

void FMeshChainBuilderDetails::BuildBakeRow(IDetailCategoryBuilder& Category)
{
    Category.AddCustomRow(LOCTEXT("BakeFilter", "Bake"))
    .WholeRowContent()
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(LOCTEXT("Bake", "Bake to ISM"))
        .ToolTipText(LOCTEXT("BakeTip", "Spawn a standalone actor with one InstancedStaticMesh per profile used in the chain."))
        .OnClicked(this, &FMeshChainBuilderDetails::OnBakeClicked)
    ];
}

FText FMeshChainBuilderDetails::GetStatusText() const
{
    if (!m_Target.IsValid())
    {
        return FText::GetEmpty();
    }
    return FText::Format(LOCTEXT("StatusFmt", "Steps: {0}"), FText::AsNumber(m_Target->GetStepCount()));
}

ECheckBoxState FMeshChainBuilderDetails::IsActiveCorner(FGuid ProfileId) const
{
    if (!m_Target.IsValid())
    {
        return ECheckBoxState::Unchecked;
    }
    return (m_Target->GetActiveCornerProfileId() == ProfileId) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshChainBuilderDetails::OnSetActiveCorner(ECheckBoxState NewState, FGuid ProfileId)
{
    // Radio behavior: ignore unchecks; only commit when a row is selected.
    if (NewState != ECheckBoxState::Checked || !m_Target.IsValid())
    {
        return;
    }
    m_Target->Editor_SetActiveCornerProfileId(ProfileId);
}

FReply FMeshChainBuilderDetails::OnAddNodeClicked(FGuid ProfileId, float TurnAngleDeg)
{
    if (m_Target.IsValid())
    {
        m_Target->Editor_AddNode(ProfileId, TurnAngleDeg);
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

#undef LOCTEXT_NAMESPACE
