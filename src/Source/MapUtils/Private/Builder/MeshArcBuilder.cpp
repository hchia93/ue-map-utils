#include "Builder/MeshArcBuilder.h"

#include "Operations/IsmBakeTag.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#endif // WITH_EDITOR

namespace MeshArcBuilderLocal
{
    constexpr float kEpsilon = 0.01f;
    constexpr float kBillboardLiftZ = 200.f;

#if WITH_EDITOR
    // Flush selection-outline + viewport after editor mutations destroy / spawn slot components.
    static void RefreshViewportAfterMutation()
    {
        if (GEditor)
        {
            GEditor->NoteSelectionChange();
            GEditor->RedrawLevelEditingViewports(true);
        }
    }
#endif // WITH_EDITOR

    static FName MakeSlotComponentName(EMeshBuilderSlotType Type, int32 SlotIndex)
    {
        const TCHAR* TypeTag = (Type == EMeshBuilderSlotType::Forward) ? TEXT("F") : TEXT("C");
        return FName(*FString::Printf(TEXT("Slot_%s_%d"), TypeTag, SlotIndex));
    }

    static FQuat AxisAlignmentQuat(EMeshOrientation Orient)
    {
        switch (Orient)
        {
        case EMeshOrientation::X:        return FQuat::Identity;
        case EMeshOrientation::InverseX: return FQuat(FVector::UpVector, PI);
        case EMeshOrientation::Y:        return FQuat(FVector::UpVector, -HALF_PI);
        case EMeshOrientation::InverseY: return FQuat(FVector::UpVector, HALF_PI);
        }
        return FQuat::Identity;
    }

}

AMeshArcBuilder::AMeshArcBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
    SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
    if (SpriteComponent)
    {
        static ConstructorHelpers::FObjectFinder<UTexture2D> SpriteAsset(TEXT("/Engine/EditorResources/S_Note"));
        if (SpriteAsset.Succeeded())
        {
            SpriteComponent->SetSprite(SpriteAsset.Object);
        }
        SpriteComponent->bIsScreenSizeScaled = true;
        SpriteComponent->SetMobility(EComponentMobility::Static);
        SpriteComponent->SetRelativeLocation(FVector(0.f, 0.f, MeshArcBuilderLocal::kBillboardLiftZ));
        SpriteComponent->SetupAttachment(SceneRoot);
    }
#endif // WITH_EDITORONLY_DATA
}

void AMeshArcBuilder::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RegenerateProfileIds();
    RebuildArc();
}

#if WITH_EDITOR
void AMeshArcBuilder::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
    {
        return;
    }
    RegenerateProfileIds();
    RebuildArc();
}
#endif // WITH_EDITOR

const FMeshBuilderProfile* AMeshArcBuilder::FindProfile(FGuid InId, bool bIsCorner) const
{
    if (!InId.IsValid())
    {
        return nullptr;
    }
    const TArray<FMeshBuilderProfile>& Arr = bIsCorner ? CornerProfiles : ForwardProfiles;
    for (const FMeshBuilderProfile& P : Arr)
    {
        if (P.ProfileId == InId)
        {
            return &P;
        }
    }
    return nullptr;
}

void AMeshArcBuilder::RegenerateProfileIds()
{
    auto FixUp = [](TArray<FMeshBuilderProfile>& Arr)
    {
        for (FMeshBuilderProfile& P : Arr)
        {
            if (!P.ProfileId.IsValid())
            {
                P.ProfileId = FGuid::NewGuid();
                P.BodyInstance.SetCollisionProfileName(TEXT("NoCollision"));
            }
        }
    };
    FixUp(ForwardProfiles);
    FixUp(CornerProfiles);
}

float AMeshArcBuilder::GetForwardScale(const FVector& Scale, EMeshOrientation Orient) const
{
    switch (Orient)
    {
    case EMeshOrientation::X:
    case EMeshOrientation::InverseX: return Scale.X;
    case EMeshOrientation::Y:
    case EMeshOrientation::InverseY: return Scale.Y;
    }
    return 1.f;
}

float AMeshArcBuilder::GetMeshForwardLength(const UStaticMesh* Mesh, EMeshOrientation Orient) const
{
    if (!Mesh)
    {
        return 0.f;
    }
    const FVector Size = Mesh->GetBoundingBox().GetSize();
    switch (Orient)
    {
    case EMeshOrientation::X:
    case EMeshOrientation::InverseX: return Size.X;
    case EMeshOrientation::Y:
    case EMeshOrientation::InverseY: return Size.Y;
    }
    return Size.X;
}

FQuat AMeshArcBuilder::GetMeshAlignmentQuat(EMeshOrientation Orient) const
{
    return MeshArcBuilderLocal::AxisAlignmentQuat(Orient);
}

FVector AMeshArcBuilder::GetMeshBoundsCenterLocal(const UStaticMesh* Mesh) const
{
    if (!Mesh)
    {
        return FVector::ZeroVector;
    }
    return Mesh->GetBoundingBox().GetCenter();
}

UStaticMeshComponent* AMeshArcBuilder::AcquireSlotComponent(EMeshBuilderSlotType InType, int32 SlotIndex, UStaticMesh* Mesh)
{
    if (!Mesh)
    {
        return nullptr;
    }

    const FName Name = MeshArcBuilderLocal::MakeSlotComponentName(InType, SlotIndex);

    for (UActorComponent* Existing : GetComponents())
    {
        if (Existing && Existing->GetFName() == Name)
        {
            UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Existing);
            if (AsMesh)
            {
                AsMesh->SetFlags(RF_Transactional);
                if (AsMesh->GetStaticMesh() != Mesh)
                {
                    AsMesh->Modify();
                    AsMesh->SetStaticMesh(Mesh);
                }
                return AsMesh;
            }
        }
    }

    UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this, Name, RF_Transactional);
    NewComp->CreationMethod = EComponentCreationMethod::Instance;
    NewComp->SetStaticMesh(Mesh);
    NewComp->SetMobility(EComponentMobility::Static);
    NewComp->SetupAttachment(SceneRoot);
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComp->RegisterComponent();
    AddInstanceComponent(NewComp);
    return NewComp;
}

void AMeshArcBuilder::ApplyProfileOverrideMaterial(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const
{
    if (!Comp)
    {
        return;
    }
    UMaterialInterface* Override = Profile.OverrideMaterial;
    const int32 SlotCount = Comp->GetNumMaterials();
    bool bChanged = false;
    if (Comp->OverrideMaterials.Num() > 0)
    {
        bChanged = true;
    }
    if (!bChanged && Override)
    {
        for (int32 SlotIdx = 0; SlotIdx < SlotCount; ++SlotIdx)
        {
            if (Comp->GetMaterial(SlotIdx) != Override)
            {
                bChanged = true;
                break;
            }
        }
    }
    if (!bChanged)
    {
        return;
    }
    Comp->Modify();
    Comp->EmptyOverrideMaterials();
    if (!Override)
    {
        return;
    }
    for (int32 SlotIdx = 0; SlotIdx < SlotCount; ++SlotIdx)
    {
        Comp->SetMaterial(SlotIdx, Override);
    }
}

void AMeshArcBuilder::ApplyProfileCollision(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const
{
    if (!Comp)
    {
        return;
    }
    const FBodyInstance& Body = Profile.BodyInstance;
    const FName Profile_Name = Body.GetCollisionProfileName();
    const ECollisionEnabled::Type Enabled = Body.GetCollisionEnabled();
    if (Comp->GetCollisionProfileName() != Profile_Name)
    {
        Comp->SetCollisionProfileName(Profile_Name);
    }
    if (Comp->GetCollisionEnabled() != Enabled)
    {
        Comp->SetCollisionEnabled(Enabled);
    }
}

void AMeshArcBuilder::DestroyAllSlots()
{
    for (FMeshBuilderSlotState& Slot : m_Slots)
    {
        if (Slot.Component)
        {
            Slot.Component->Modify();
            Slot.Component->DestroyComponent();
        }
    }
    m_Slots.Reset();
}

void AMeshArcBuilder::RebuildArc()
{
    using namespace MeshArcBuilderLocal;

    auto FindExisting = [&](EMeshBuilderSlotType InType, int32 SlotIndex) -> int32
    {
        for (int32 i = 0; i < m_Slots.Num(); ++i)
        {
            if (m_Slots[i].Type == InType && m_Slots[i].StepIndex == SlotIndex)
            {
                return i;
            }
        }
        return INDEX_NONE;
    };

    TArray<FMeshBuilderSlotState> NewSlots;
    TBitArray<> Reused(false, m_Slots.Num());

    auto PlaceSlot = [&](EMeshBuilderSlotType InType, int32 SlotIndex, const FMeshBuilderProfile& Profile,
                         const FVector& SlotCenter, const FQuat& SlotBaselineRot)
    {
        UStaticMesh* Mesh = Profile.Mesh;
        const FQuat ExtraRot = Profile.Template.GetRotation();
        const FVector Scale = Profile.Template.GetScale3D();
        const FQuat FinalBaselineRot = SlotBaselineRot * ExtraRot;
        const FTransform SlotBaseline(FinalBaselineRot, SlotCenter);

        const int32 ExistingIndex = FindExisting(InType, SlotIndex);
        FQuat UserRotDelta = FQuat::Identity;
        if (ExistingIndex != INDEX_NONE && m_Slots[ExistingIndex].Component)
        {
            const FQuat PrevBaselineRot = m_Slots[ExistingIndex].LastBaselineRelative.GetRotation();
            const FQuat CurrentRot = m_Slots[ExistingIndex].Component->GetRelativeRotation().Quaternion();
            UserRotDelta = CurrentRot * PrevBaselineRot.Inverse();
            UserRotDelta.Normalize();
        }
        const FQuat FinalRot = UserRotDelta * FinalBaselineRot;
        const FVector BoundsCenterLocal = GetMeshBoundsCenterLocal(Mesh);
        const FVector ScaledBoundsCenter(BoundsCenterLocal * Scale);
        const FVector FinalLoc = SlotCenter - FinalRot.RotateVector(ScaledBoundsCenter);

        FMeshBuilderSlotState State;
        State.Type = InType;
        State.StepIndex = SlotIndex;
        State.SourceProfileId = Profile.ProfileId;
        if (ExistingIndex != INDEX_NONE && m_Slots[ExistingIndex].Component)
        {
            State.Component = m_Slots[ExistingIndex].Component;
            Reused[ExistingIndex] = true;
            if (State.Component->GetStaticMesh() != Mesh)
            {
                State.Component->Modify();
                State.Component->SetStaticMesh(Mesh);
            }
        }
        else
        {
            State.Component = AcquireSlotComponent(InType, SlotIndex, Mesh);
        }
        if (!ensure(State.Component))
        {
            return;
        }
        const FTransform Target(FinalRot, FinalLoc, Scale);
        if (!State.Component->GetRelativeTransform().Equals(Target, KINDA_SMALL_NUMBER))
        {
            State.Component->Modify();
            State.Component->SetRelativeTransform(Target);
        }
        ApplyProfileOverrideMaterial(Profile, State.Component);
        State.LastBaselineRelative = SlotBaseline;
        NewSlots.Add(State);
    };

    if (Radius > kEpsilon && ForwardProfiles.Num() > 0)
    {
        const float CoverageFrac = FMath::Clamp(ArcCoveragePercent, 0.f, 100.f) / 100.f;
        const float TotalArcRad = 2.f * PI * CoverageFrac;
        const float StartRad = FMath::DegreesToRadians(StartAngleDeg);
        const float DirSign = bClockwise ? -1.f : 1.f;
        const bool bFullCircle = CoverageFrac >= 1.f - KINDA_SMALL_NUMBER;

        // Walk profiles around the arc until we run out of room. JointAngles records each chord boundary
        // for the corner pass.
        TArray<float> JointAngles;
        JointAngles.Add(StartRad);

        float Consumed = 0.f;
        int32 ProfileCursor = 0;
        int32 ForwardSlotIndex = 0;
        // Guard against infinite loop when every forward profile has empty mesh.
        int32 EmptyStreak = 0;

        while (Consumed < TotalArcRad - KINDA_SMALL_NUMBER)
        {
            const FMeshBuilderProfile& P = ForwardProfiles[ProfileCursor % ForwardProfiles.Num()];
            ++ProfileCursor;

            if (!P.Mesh)
            {
                if (++EmptyStreak >= ForwardProfiles.Num())
                {
                    break;
                }
                continue;
            }
            EmptyStreak = 0;

            const float L = GetMeshForwardLength(P.Mesh, P.Orientation) * GetForwardScale(P.Template.GetScale3D(), P.Orientation);
            if (L <= kEpsilon)
            {
                continue;
            }

            // Chord of length L subtends angle Alpha on circle of radius R: L = 2R sin(Alpha/2).
            const float SinHalfAlpha = L / (2.f * Radius);
            if (SinHalfAlpha >= 1.f - KINDA_SMALL_NUMBER)
            {
                break; // Mesh longer than diameter; cannot place a chord on this circle.
            }
            const float Alpha = 2.f * FMath::Asin(SinHalfAlpha);

            if (Consumed + Alpha > TotalArcRad + KINDA_SMALL_NUMBER)
            {
                break;
            }

            const float MidAngle = StartRad + DirSign * (Consumed + Alpha * 0.5f);
            // Profile template adds per-slot inward (Y) and vertical (Z) on top of globals; X is unused for arc.
            const FVector ProfileTrans = P.Template.GetTranslation();
            const float MidRadius = Radius * FMath::Cos(Alpha * 0.5f) - InwardOffset - ProfileTrans.Y;
            const FVector Center(FMath::Cos(MidAngle) * MidRadius, FMath::Sin(MidAngle) * MidRadius, ProfileTrans.Z);

            // Mesh +X (after AxisAlignmentQuat) aligns with the chord direction, which is the tangent at MidAngle.
            // Tangent at MidAngle on the unit circle points along (MidAngle + 90°) when going CCW; for CW we flip.
            const float TangentAngle = MidAngle + DirSign * HALF_PI;
            const FQuat TangentRot(FVector::UpVector, TangentAngle);
            const FQuat MeshAlign = GetMeshAlignmentQuat(P.Orientation);

            PlaceSlot(EMeshBuilderSlotType::Forward, ForwardSlotIndex++, P, Center, TangentRot * MeshAlign);

            Consumed += Alpha;
            JointAngles.Add(StartRad + DirSign * Consumed);
        }

        // Corners place iff ActiveCornerProfileId resolves to a profile with a mesh.
        // "None" radio (invalid GUID) and missing/empty profile both fall through here as no-op.
        {
            const FMeshBuilderProfile* CornerProfile = FindProfile(ActiveCornerProfileId, /*bIsCorner=*/ true);
            if (CornerProfile && CornerProfile->Mesh)
            {
                int32 CornerLimit = JointAngles.Num();
                // For 100% coverage the last joint coincides with the first; place only one corner there.
                if (bFullCircle && CornerLimit > 0)
                {
                    --CornerLimit;
                }
                const FVector CornerTrans = CornerProfile->Template.GetTranslation();
                const float CornerRadius = Radius - InwardOffset - CornerTrans.Y;
                const float CornerZ = CornerTrans.Z;
                const FQuat CornerMeshAlign = GetMeshAlignmentQuat(CornerProfile->Orientation);

                for (int32 i = 0; i < CornerLimit; ++i)
                {
                    const float Angle = JointAngles[i];
                    const FVector Center(FMath::Cos(Angle) * CornerRadius, FMath::Sin(Angle) * CornerRadius, CornerZ);
                    // Corner mesh +X points outward radially (away from arc center) for fence-post convention.
                    const FQuat RadialRot(FVector::UpVector, Angle);
                    PlaceSlot(EMeshBuilderSlotType::Corner, i, *CornerProfile, Center, RadialRot * CornerMeshAlign);
                }
            }
        }
    }

    for (int32 i = 0; i < m_Slots.Num(); ++i)
    {
        if (!Reused[i] && m_Slots[i].Component)
        {
            m_Slots[i].Component->Modify();
            m_Slots[i].Component->DestroyComponent();
        }
    }
    m_Slots = MoveTemp(NewSlots);
}

#if WITH_EDITOR
void AMeshArcBuilder::Editor_SetActiveCornerProfileId(FGuid InId)
{
    if (ActiveCornerProfileId == InId)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshArcBuilder", "SetActiveCorner", "Mesh Arc: Set Active Corner"));
    Modify();
    ActiveCornerProfileId = InId;
    RebuildArc();
    MeshArcBuilderLocal::RefreshViewportAfterMutation();
}

void AMeshArcBuilder::Editor_RegenerateArc()
{
    if (m_Slots.Num() == 0)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshArcBuilder", "Regenerate", "Mesh Arc: Regenerate"));
    Modify();
    DestroyAllSlots();
    RebuildArc();
    MeshArcBuilderLocal::RefreshViewportAfterMutation();
}

FTransform AMeshArcBuilder::ComputeBakePivotXf() const
{
    if (BakedPivotLocation == EBakedPivotLocation::Default)
    {
        return GetActorTransform();
    }

    FBox WorldBounds(ForceInit);
    for (const FMeshBuilderSlotState& Slot : m_Slots)
    {
        if (!Slot.Component)
        {
            continue;
        }
        const FMeshBuilderProfile* P = FindProfile(Slot.SourceProfileId, Slot.Type == EMeshBuilderSlotType::Corner);
        if (!P || !P->Mesh)
        {
            continue;
        }
        WorldBounds += P->Mesh->GetBoundingBox().TransformBy(Slot.Component->GetComponentTransform());
    }

    if (!WorldBounds.IsValid)
    {
        return GetActorTransform();
    }

    // For arc, the "thickness plane" is the X coord closer to the builder pivot.
    const float PlaneYZ = MeshBuilderPivot::NearestPlaneYZ(WorldBounds, GetActorLocation().X);
    const FVector Anchor = MeshBuilderPivot::ResolveBoundsAnchor(WorldBounds, BakedPivotLocation, PlaneYZ);
    return FTransform(GetActorTransform().GetRotation(), Anchor);
}

void AMeshArcBuilder::Editor_BakeToISM()
{
    UWorld* World = GetWorld();
    ULevel* Level = GetLevel();
    if (!World || !Level || m_Slots.Num() == 0)
    {
        return;
    }

    FScopedTransaction Tx(NSLOCTEXT("MeshArcBuilder", "Bake", "Mesh Arc: Bake to ISM"));
    Level->Modify();

    const FTransform PivotXf = ComputeBakePivotXf();

    FActorSpawnParameters SpawnParams;
    SpawnParams.OverrideLevel = Level;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* BakedActor = World->SpawnActor<AActor>(AActor::StaticClass(), PivotXf, SpawnParams);
    if (!BakedActor)
    {
        return;
    }
    BakedActor->Modify();

    UInstancedStaticMeshComponent* RootISM = nullptr;

    struct FBakeGroup
    {
        EMeshBuilderSlotType Type = EMeshBuilderSlotType::Forward;
        TArray<int32> SlotIndices;
    };
    TMap<FGuid, FBakeGroup> Groups;
    for (int32 i = 0; i < m_Slots.Num(); ++i)
    {
        const FMeshBuilderSlotState& S = m_Slots[i];
        if (!S.Component || !S.SourceProfileId.IsValid())
        {
            continue;
        }
        FBakeGroup& G = Groups.FindOrAdd(S.SourceProfileId);
        G.Type = S.Type;
        G.SlotIndices.Add(i);
    }

    auto MakeIsmName = [](const FMeshBuilderProfile& P, EMeshBuilderSlotType Type)
    {
        const FString TypePrefix = (Type == EMeshBuilderSlotType::Corner) ? TEXT("ISM_Corner_") : TEXT("ISM_Forward_");
        const FString MeshName = P.Mesh ? P.Mesh->GetName() : P.ProfileId.ToString(EGuidFormats::DigitsWithHyphens);
        return FName(*(TypePrefix + MeshName));
    };

    for (const TPair<FGuid, FBakeGroup>& Entry : Groups)
    {
        const FGuid& Guid = Entry.Key;
        const FBakeGroup& Group = Entry.Value;
        const bool bIsCorner = (Group.Type == EMeshBuilderSlotType::Corner);
        const FMeshBuilderProfile* Profile = FindProfile(Guid, bIsCorner);
        if (!Profile || !Profile->Mesh)
        {
            continue;
        }

        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(BakedActor, MakeIsmName(*Profile, Group.Type), RF_Transactional);
        ISM->Modify();
        ISM->bHasPerInstanceHitProxies = true;
        ISM->SetStaticMesh(Profile->Mesh);
        ISM->SetMobility(EComponentMobility::Static);
        ApplyProfileCollision(*Profile, static_cast<UStaticMeshComponent*>(ISM));
        ApplyProfileOverrideMaterial(*Profile, static_cast<UStaticMeshComponent*>(ISM));

        if (RootISM == nullptr)
        {
            BakedActor->SetRootComponent(ISM);
            RootISM = ISM;
        }
        else
        {
            ISM->AttachToComponent(RootISM, FAttachmentTransformRules::KeepRelativeTransform);
        }

        BakedActor->RemoveOwnedComponent(ISM);
        ISM->CreationMethod = EComponentCreationMethod::Instance;
        BakedActor->AddOwnedComponent(ISM);
        ISM->RegisterComponent();

        if (RootISM == ISM)
        {
            BakedActor->SetActorTransform(PivotXf);
        }

        for (int32 SlotIdx : Group.SlotIndices)
        {
            const FMeshBuilderSlotState& Slot = m_Slots[SlotIdx];
            const FTransform InstanceLocalXf = Slot.Component->GetComponentTransform().GetRelativeTransform(PivotXf);
            ISM->AddInstance(InstanceLocalXf, /*bWorldSpace*/ false);
        }
    }

    if (!RootISM)
    {
        World->EditorDestroyActor(BakedActor, true);
        return;
    }

    IsmBaked::TagAndLabel(BakedActor, GetFolderPath());
    BakedActor->PostEditChange();
}
#endif // WITH_EDITOR
