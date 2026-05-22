#include "Builder/MeshArcBuilderDetails.h"

#include "Builder/MeshArcBuilder.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/StaticMesh.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MeshArcBuilderDetails"

namespace MeshArcBuilderDetailsLocal
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

TSharedRef<IDetailCustomization> FMeshArcBuilderDetails::MakeInstance()
{
    return MakeShared<FMeshArcBuilderDetails>();
}

void FMeshArcBuilderDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }
    m_Target = Cast<AMeshArcBuilder>(Objects[0].Get());
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
    TSharedPtr<IPropertyHandle> CornerArrayHandle = DetailBuilder.GetProperty(FName(TEXT("CornerProfiles")));
    if (CornerArrayHandle.IsValid())
    {
        if (TSharedPtr<IPropertyHandleArray> AsArray = CornerArrayHandle->AsArray())
        {
            AsArray->SetOnNumElementsChanged(RefreshDelegate);
            // Per-element Mesh-field hook only. Blanket ChildPropertyValueChanged would fire during
            // slider drag inside Template (FTransform), tearing the slider widget mid-drag.
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
        }
    }

    IDetailCategoryBuilder& Actions = DetailBuilder.EditCategory("Tool Action", FText::GetEmpty(), ECategoryPriority::Important);
    Actions.SetSortOrder(0);
    DetailBuilder.EditCategory("Tool Setup", FText::GetEmpty(), ECategoryPriority::Important).SetSortOrder(1);

    Actions.AddCustomRow(LOCTEXT("StatusRowFilter", "Status"))
    .WholeRowContent()
    [
        SNew(STextBlock).Text(this, &FMeshArcBuilderDetails::GetStatusText)
    ];

    BuildCornerProfileRows(Actions);
    BuildLifecycleRow(Actions);
    BuildBakeRow(Actions);
}

void FMeshArcBuilderDetails::BuildCornerProfileRows(IDetailCategoryBuilder& Category)
{
    using namespace MeshArcBuilderDetailsLocal;

    AMeshArcBuilder* Builder = m_Target.Get();
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
        .Font(IDetailLayoutBuilder::GetDetailFontBold())
        .Justification(ETextJustify::Center)
    ];

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
            .IsChecked(this, &FMeshArcBuilderDetails::IsActiveCorner, FGuid())
            .OnCheckStateChanged(this, &FMeshArcBuilderDetails::OnSetActiveCorner, FGuid())
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
                .IsChecked(this, &FMeshArcBuilderDetails::IsActiveCorner, CapturedId)
                .OnCheckStateChanged(this, &FMeshArcBuilderDetails::OnSetActiveCorner, CapturedId)
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

void FMeshArcBuilderDetails::BuildLifecycleRow(IDetailCategoryBuilder& Category)
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
            .Text(LOCTEXT("Regen", "Regenerate"))
            .ToolTipText(LOCTEXT("RegenTip", "Discard per-slot transform overrides and rebuild from baseline."))
            .OnClicked(this, &FMeshArcBuilderDetails::OnRegenerateClicked)
        ]
    ];
}

void FMeshArcBuilderDetails::BuildBakeRow(IDetailCategoryBuilder& Category)
{
    Category.AddCustomRow(LOCTEXT("BakeFilter", "Bake"))
    .WholeRowContent()
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(LOCTEXT("Bake", "Bake to ISM"))
        .ToolTipText(LOCTEXT("BakeTip", "Spawn a standalone actor with one InstancedStaticMesh per profile used in the arc."))
        .OnClicked(this, &FMeshArcBuilderDetails::OnBakeClicked)
    ];
}

FText FMeshArcBuilderDetails::GetStatusText() const
{
    if (!m_Target.IsValid())
    {
        return FText::GetEmpty();
    }
    return FText::Format(LOCTEXT("StatusFmt", "Slots: {0}"), FText::AsNumber(m_Target->GetSlotCount()));
}

ECheckBoxState FMeshArcBuilderDetails::IsActiveCorner(FGuid ProfileId) const
{
    if (!m_Target.IsValid())
    {
        return ECheckBoxState::Unchecked;
    }
    return (m_Target->GetActiveCornerProfileId() == ProfileId) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshArcBuilderDetails::OnSetActiveCorner(ECheckBoxState NewState, FGuid ProfileId)
{
    if (NewState != ECheckBoxState::Checked || !m_Target.IsValid())
    {
        return;
    }
    m_Target->Editor_SetActiveCornerProfileId(ProfileId);
}

FReply FMeshArcBuilderDetails::OnRegenerateClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_RegenerateArc(); }
    return FReply::Handled();
}

FReply FMeshArcBuilderDetails::OnBakeClicked()
{
    if (m_Target.IsValid()) { m_Target->Editor_BakeToISM(); }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
