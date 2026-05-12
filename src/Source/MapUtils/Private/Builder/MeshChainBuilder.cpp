#include "Builder/MeshChainBuilder.h"

#include "Operations/MapUtilsIsmBakedTag.h"

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
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#endif // WITH_EDITOR

namespace MeshChainBuilderLocal
{
    constexpr float kEpsilon = 0.01f;
    constexpr float kBillboardLiftZ = 200.f;

    static FName MakeSlotComponentName(EMeshChainSlotRole Role, int32 RoleIndex)
    {
        const TCHAR* RoleTag = TEXT("A");
        switch (Role)
        {
        case EMeshChainSlotRole::Main:       RoleTag = TEXT("A"); break;
        case EMeshChainSlotRole::Transition: RoleTag = TEXT("B"); break;
        case EMeshChainSlotRole::Corner:     RoleTag = TEXT("C"); break;
        }
        return FName(*FString::Printf(TEXT("Slot_%s_%d"), RoleTag, RoleIndex));
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

AMeshChainBuilder::AMeshChainBuilder()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    SetRootComponent(SceneRoot);

    BodyInstanceA.SetCollisionProfileName(TEXT("NoCollision"));
    BodyInstanceB.SetCollisionProfileName(TEXT("NoCollision"));
    BodyInstanceC.SetCollisionProfileName(TEXT("NoCollision"));

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
        // Lifted up so it sits clear of any Main mesh placed at origin and is the obvious actor click target.
        SpriteComponent->SetRelativeLocation(FVector(0.f, 0.f, MeshChainBuilderLocal::kBillboardLiftZ));
        SpriteComponent->SetupAttachment(SceneRoot);
    }
#endif // WITH_EDITORONLY_DATA
}

void AMeshChainBuilder::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildChain();
}

#if WITH_EDITOR
void AMeshChainBuilder::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    // Skip the rebuild while a slider is mid-drag; only react when the value is committed.
    if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
    {
        return;
    }
    RebuildChain();
}
#endif // WITH_EDITOR

int32 AMeshChainBuilder::GetMainCount() const
{
    int32 Count = 0;
    for (const FMeshChainStep& Step : Steps)
    {
        if (Step.Kind == EMeshChainStepKind::Forward)
        {
            ++Count;
        }
    }
    return Count;
}

UStaticMesh* AMeshChainBuilder::GetMeshForRole(EMeshChainSlotRole InRole) const
{
    switch (InRole)
    {
    case EMeshChainSlotRole::Main:       return MeshA;
    case EMeshChainSlotRole::Transition: return MeshB;
    case EMeshChainSlotRole::Corner:     return MeshC;
    }
    return nullptr;
}

const FTransform& AMeshChainBuilder::GetTemplateForRole(EMeshChainSlotRole InRole) const
{
    switch (InRole)
    {
    case EMeshChainSlotRole::Main:       return TemplateA;
    case EMeshChainSlotRole::Transition: return TemplateB;
    case EMeshChainSlotRole::Corner:     return TemplateC;
    }
    return FTransform::Identity;
}

float AMeshChainBuilder::GetForwardScale(const FVector& Scale, EMeshOrientation Orient) const
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

EMeshOrientation AMeshChainBuilder::GetOrientationForRole(EMeshChainSlotRole InRole) const
{
    switch (InRole)
    {
    case EMeshChainSlotRole::Main:       return OrientationA;
    case EMeshChainSlotRole::Transition: return OrientationB;
    case EMeshChainSlotRole::Corner:     return OrientationC;
    }
    return EMeshOrientation::X;
}

float AMeshChainBuilder::GetMeshForwardLength(const UStaticMesh* Mesh, EMeshOrientation Orient) const
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

FQuat AMeshChainBuilder::GetMeshAlignmentQuat(EMeshOrientation Orient) const
{
    return MeshChainBuilderLocal::AxisAlignmentQuat(Orient);
}

FVector AMeshChainBuilder::GetMeshBoundsCenterLocal(const UStaticMesh* Mesh) const
{
    if (!Mesh)
    {
        return FVector::ZeroVector;
    }
    return Mesh->GetBoundingBox().GetCenter();
}

UStaticMeshComponent* AMeshChainBuilder::AcquireSlotComponent(EMeshChainSlotRole InRole, int32 RoleIndex, UStaticMesh* Mesh)
{
    if (!Mesh)
    {
        return nullptr;
    }

    const FName Name = MeshChainBuilderLocal::MakeSlotComponentName(InRole, RoleIndex);

    // Reuse an existing component carrying this name (preserves user gizmo edits when the chain re-emits the same slot).
    for (UActorComponent* Existing : GetComponents())
    {
        if (Existing && Existing->GetFName() == Name)
        {
            UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Existing);
            if (AsMesh)
            {
                // Backfill RF_Transactional for slots saved before the flag was passed at NewObject time.
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

    // RF_Transactional is required for gizmo edits to enter the undo buffer; without it Modify() silently no-ops.
    UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this, Name, RF_Transactional);
    // CreationMethod must be set before RegisterComponent so the editor classifies it as an instance-owned component.
    NewComp->CreationMethod = EComponentCreationMethod::Instance;
    NewComp->SetStaticMesh(Mesh);
    NewComp->SetMobility(EComponentMobility::Static);
    NewComp->SetupAttachment(SceneRoot);
    // Edit-time slots carry no collision; per-role BodyInstance is only applied at Bake-to-ISM time.
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComp->RegisterComponent();
    AddInstanceComponent(NewComp);
    return NewComp;
}

const FBodyInstance& AMeshChainBuilder::GetBodyInstanceForRole(EMeshChainSlotRole InRole) const
{
    switch (InRole)
    {
    case EMeshChainSlotRole::Main:       return BodyInstanceA;
    case EMeshChainSlotRole::Transition: return BodyInstanceB;
    case EMeshChainSlotRole::Corner:     return BodyInstanceC;
    }
    return BodyInstanceA;
}

UMaterialInterface* AMeshChainBuilder::GetOverrideMaterialForRole(EMeshChainSlotRole InRole) const
{
    switch (InRole)
    {
    case EMeshChainSlotRole::Main:       return OverrideMaterialA;
    case EMeshChainSlotRole::Transition: return OverrideMaterialB;
    case EMeshChainSlotRole::Corner:     return OverrideMaterialC;
    }
    return nullptr;
}

void AMeshChainBuilder::ApplyRoleOverrideMaterial(EMeshChainSlotRole InRole, UStaticMeshComponent* Comp) const
{
    if (!Comp)
    {
        return;
    }
    UMaterialInterface* Override = GetOverrideMaterialForRole(InRole);
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
    // Modify so undo of a chain rebuild restores the prior material set.
    Comp->Modify();
    // Wipe any prior overrides first so unsetting the role's material reverts to mesh defaults.
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

void AMeshChainBuilder::ApplyRoleCollision(EMeshChainSlotRole InRole, UStaticMeshComponent* Comp) const
{
    if (!Comp)
    {
        return;
    }
    // CopyBodyInstancePropertiesFrom is unsafe on already-registered components.
    // SetCollisionProfileName / SetCollisionEnabled cover NoCollision / InvisibleWall / any named profile.
    // Skip when value is unchanged so we don't trigger spurious physics-state recreation each rebuild.
    const FBodyInstance& Body = GetBodyInstanceForRole(InRole);
    const FName Profile = Body.GetCollisionProfileName();
    const ECollisionEnabled::Type Enabled = Body.GetCollisionEnabled();
    if (Comp->GetCollisionProfileName() != Profile)
    {
        Comp->SetCollisionProfileName(Profile);
    }
    if (Comp->GetCollisionEnabled() != Enabled)
    {
        Comp->SetCollisionEnabled(Enabled);
    }
}

void AMeshChainBuilder::DestroyAllSlots()
{
    for (FMeshChainSlotState& Slot : m_Slots)
    {
        if (Slot.Component)
        {
            Slot.Component->Modify();
            Slot.Component->DestroyComponent();
        }
    }
    m_Slots.Reset();
}

void AMeshChainBuilder::RebuildChain()
{
    using namespace MeshChainBuilderLocal;

    auto FindExisting = [&](EMeshChainSlotRole InRole, int32 RoleIndex) -> int32
    {
        for (int32 i = 0; i < m_Slots.Num(); ++i)
        {
            if (m_Slots[i].Role == InRole && m_Slots[i].RoleIndex == RoleIndex)
            {
                return i;
            }
        }
        return INDEX_NONE;
    };

    TArray<FMeshChainSlotState> NewSlots;
    TBitArray<> Reused(false, m_Slots.Num());

    int32 MainCounter = 0;
    int32 TransitionCounter = 0;
    int32 CornerCounter = 0;

    FVector TailPos = FVector::ZeroVector;
    // TailRot is the chain-frame rotation (mesh-axis-agnostic). Its +X is the chain forward direction.
    FQuat TailRot = FQuat::Identity;

    // Shared placement core: writes the slot to its component (creating if needed), preserves the user's
    // rotation delta, and returns the chain-frame baseline transform so the caller can record it.
    auto PlaceSlot = [&](EMeshChainSlotRole InRole, int32 RoleIndex, UStaticMesh* Mesh, const FVector& SlotCenter, const FQuat& SlotBaselineRot, const FQuat& ExtraRot, const FVector& Scale)
    {
        const FQuat   FinalBaselineRot = SlotBaselineRot * ExtraRot;
        const FTransform SlotBaseline(FinalBaselineRot, SlotCenter);

        const int32 ExistingIndex = FindExisting(InRole, RoleIndex);
        FQuat UserRotDelta = FQuat::Identity;
        if (ExistingIndex != INDEX_NONE && m_Slots[ExistingIndex].Component)
        {
            const FQuat PrevBaselineRot = m_Slots[ExistingIndex].LastBaselineRelative.GetRotation();
            const FQuat CurrentRot = m_Slots[ExistingIndex].Component->GetRelativeRotation().Quaternion();
            UserRotDelta = CurrentRot * PrevBaselineRot.Inverse();
            UserRotDelta.Normalize();
        }

        const FQuat FinalRot = UserRotDelta * FinalBaselineRot;
        // Translation locked: gizmo translation is discarded. Bounds center (post-scale) lands on SlotCenter.
        const FVector BoundsCenterLocal = GetMeshBoundsCenterLocal(Mesh);
        const FVector ScaledBoundsCenter(BoundsCenterLocal * Scale);
        const FVector FinalLoc = SlotCenter - FinalRot.RotateVector(ScaledBoundsCenter);

        FMeshChainSlotState State;
        State.Role = InRole;
        State.RoleIndex = RoleIndex;
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
            State.Component = AcquireSlotComponent(InRole, RoleIndex, Mesh);
        }
        // NewObject failure path (OOM / GC mid-construction). Chain still advances ScaledLen, leaving a visual gap.
        if (!ensure(State.Component))
        {
            return UserRotDelta;
        }
        const FTransform Target(FinalRot, FinalLoc, Scale);
        if (!State.Component->GetRelativeTransform().Equals(Target, KINDA_SMALL_NUMBER))
        {
            // Modify so undo of a chain rebuild restores the slot's prior transform.
            State.Component->Modify();
            State.Component->SetRelativeTransform(Target);
        }
        ApplyRoleOverrideMaterial(InRole, State.Component);
        State.LastBaselineRelative = SlotBaseline;
        NewSlots.Add(State);
        return UserRotDelta;
    };

    auto EmitForwardSlot = [&](EMeshChainSlotRole InRole, int32& RoleCounter)
    {
        UStaticMesh* Mesh = GetMeshForRole(InRole);
        if (!Mesh)
        {
            return; // null role behaves as a zero-length placeholder.
        }
        const EMeshOrientation Orient = GetOrientationForRole(InRole);
        const float Len = GetMeshForwardLength(Mesh, Orient);
        if (Len <= kEpsilon)
        {
            return;
        }
        const FTransform& Template = GetTemplateForRole(InRole);
        const FVector Trans = Template.GetTranslation();
        const FQuat   ExtraRot = Template.GetRotation();
        const FVector Scale = Template.GetScale3D();
        const float ScaledLen = Len * GetForwardScale(Scale, Orient);
        const FQuat MeshAlign = GetMeshAlignmentQuat(Orient);
        const int32 RoleIndex = RoleCounter++;

        // Step 1: advance forward gap (Translation.X, accumulating into chain tail).
        const FVector ChainForward = TailRot.GetAxisX();
        const FVector ChainRight   = TailRot.GetAxisY();
        const FVector ChainUp      = TailRot.GetAxisZ();
        TailPos += ChainForward * Trans.X;

        // Step 2: chain center for tail progression (no Y/Z); slot center adds Y/Z displacement (per-slot only).
        const FVector ChainCenterPoint = TailPos + ChainForward * (ScaledLen * 0.5f);
        const FVector SlotCenter       = ChainCenterPoint + ChainRight * Trans.Y + ChainUp * Trans.Z;
        const FQuat   SlotBaselineRot  = TailRot * MeshAlign;

        const FQuat UserRotDelta = PlaceSlot(InRole, RoleIndex, Mesh, SlotCenter, SlotBaselineRot, ExtraRot, Scale);

        // Step 3: advance chain along forward by ScaledLen. User rotation propagates to subsequent slots.
        TailRot = UserRotDelta * TailRot;
        TailRot.Normalize();
        const FVector NewForward = TailRot.GetAxisX();
        TailPos = ChainCenterPoint + NewForward * (ScaledLen * 0.5f);
    };

    auto EmitCornerSlot = [&](int32& RoleCounter)
    {
        UStaticMesh* Mesh = MeshC;
        if (!Mesh)
        {
            return; // null Corner: nothing placed; caller still rotates and applies post-turn shift.
        }
        const float Len = GetMeshForwardLength(Mesh, OrientationC);
        if (Len <= kEpsilon)
        {
            return;
        }
        const FTransform& Template = TemplateC;
        const FQuat ExtraRot = Template.GetRotation();
        const FVector Scale = Template.GetScale3D();
        const FQuat MeshAlign = GetMeshAlignmentQuat(OrientationC);
        const int32 RoleIndex = RoleCounter++;

        // Corner is centered at the turn pivot (current TailPos); Y/Z of the template displace the corner from the pivot.
        const FVector ChainRight = TailRot.GetAxisY();
        const FVector ChainUp    = TailRot.GetAxisZ();
        const FVector Trans = Template.GetTranslation();
        const FVector SlotCenter = TailPos + ChainRight * Trans.Y + ChainUp * Trans.Z;
        const FQuat   SlotBaselineRot = TailRot * MeshAlign;

        // Place but do not advance TailPos. The post-turn forward shift (TemplateC.Translation.X) is applied by the Turn step caller.
        PlaceSlot(EMeshChainSlotRole::Corner, RoleIndex, Mesh, SlotCenter, SlotBaselineRot, ExtraRot, Scale);
    };

    bool bLastWasMain = false;
    for (const FMeshChainStep& Step : Steps)
    {
        if (Step.Kind == EMeshChainStepKind::Forward)
        {
            if (bLastWasMain)
            {
                EmitForwardSlot(EMeshChainSlotRole::Transition, TransitionCounter);
            }
            EmitForwardSlot(EMeshChainSlotRole::Main, MainCounter);
            bLastWasMain = true;
        }
        else
        {
            // Corner is centered on the pivot (no chain advance for Len_C). After rotation, TemplateC.Translation.X
            // shifts the post-turn tail along the new direction; positive leaves a gap, negative embeds.
            // UE is left-handed (+X forward, +Y right): yaw +N turns right, -N turns left.
            EmitCornerSlot(CornerCounter);
            const float TurnSign = (Step.Kind == EMeshChainStepKind::TurnLeft) ? -1.f : +1.f;
            const float AngleRad = FMath::DegreesToRadians(Step.TurnAngleDeg);
            TailRot = TailRot * FQuat(FVector::UpVector, TurnSign * AngleRad);
            TailRot.Normalize();
            TailPos += TailRot.GetAxisX() * TemplateC.GetTranslation().X;
            bLastWasMain = false;
        }
    }

    // Destroy non-reused slots. Modify so undo can revive them via the actor's instance-component revert.
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
void AMeshChainBuilder::Editor_AddNodeForward()
{
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "AddNodeForward", "Mesh Chain: Add Forward Node"));
    Modify();
    Steps.Add({ EMeshChainStepKind::Forward });
    RebuildChain();
}

void AMeshChainBuilder::Editor_AddNodeLeft(float AngleDeg)
{
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "AddNodeLeft", "Mesh Chain: Add Left Node"));
    Modify();
    FMeshChainStep Turn;
    Turn.Kind = EMeshChainStepKind::TurnLeft;
    Turn.TurnAngleDeg = AngleDeg;
    Steps.Add(Turn);
    Steps.Add({ EMeshChainStepKind::Forward });
    RebuildChain();
}

void AMeshChainBuilder::Editor_AddNodeRight(float AngleDeg)
{
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "AddNodeRight", "Mesh Chain: Add Right Node"));
    Modify();
    FMeshChainStep Turn;
    Turn.Kind = EMeshChainStepKind::TurnRight;
    Turn.TurnAngleDeg = AngleDeg;
    Steps.Add(Turn);
    Steps.Add({ EMeshChainStepKind::Forward });
    RebuildChain();
}

void AMeshChainBuilder::Editor_RemoveLast()
{
    if (Steps.Num() == 0)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "RemoveLast", "Mesh Chain: Undo"));
    Modify();
    // Pop the last node: the trailing Forward, plus any Turns immediately before it (which were the same Add-Node click).
    bool bRemovedForward = false;
    while (Steps.Num() > 0)
    {
        const bool bLastIsForward = Steps.Last().Kind == EMeshChainStepKind::Forward;
        if (bLastIsForward && bRemovedForward)
        {
            break;
        }
        Steps.Pop();
        if (bLastIsForward)
        {
            bRemovedForward = true;
        }
    }
    RebuildChain();
}

void AMeshChainBuilder::Editor_ClearChain()
{
    if (Steps.Num() == 0 && m_Slots.Num() == 0)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "Clear", "Mesh Chain: Clear"));
    Modify();
    Steps.Reset();
    DestroyAllSlots();
}

void AMeshChainBuilder::Editor_RegenerateChain()
{
    if (m_Slots.Num() == 0)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "Regenerate", "Mesh Chain: Regenerate"));
    Modify();
    // Drop all per-slot transforms by tearing down the components; the rebuild creates fresh ones with no delta.
    DestroyAllSlots();
    RebuildChain();
}

FTransform AMeshChainBuilder::ComputeBakePivotXf() const
{
    // Default: builder's own world transform.
    if (BakedPivotLocation == EBakedPivotLocation::Default)
    {
        return GetActorTransform();
    }

    // Centroid: 3D AABB center over every slot's world bounds.
    if (BakedPivotLocation == EBakedPivotLocation::BoundCenter)
    {
        FBox WorldBounds(ForceInit);
        for (const FMeshChainSlotState& Slot : m_Slots)
        {
            if (!Slot.Component)
            {
                continue;
            }
            UStaticMesh* RoleMesh = GetMeshForRole(Slot.Role);
            if (!RoleMesh)
            {
                continue;
            }
            WorldBounds += RoleMesh->GetBoundingBox().TransformBy(Slot.Component->GetComponentTransform());
        }
        if (!WorldBounds.IsValid)
        {
            return GetActorTransform();
        }
        // Keep the builder's rotation on the pivot so the baked actor still has a meaningful
        // Local gizmo basis; only the location switches to the AABB center.
        return FTransform(GetActorTransform().GetRotation(), WorldBounds.GetCenter());
    }

    // TL / TR / BL / BR: corners of the chain head's connection face (YZ plane of the first Main slot).
    // T/B = mesh top/bottom Z, L/R = chain-left/right perpendicular to chain forward (viewed from outside).
    for (const FMeshChainSlotState& Slot : m_Slots)
    {
        if (Slot.Role != EMeshChainSlotRole::Main || Slot.RoleIndex != 0 || !Slot.Component)
        {
            continue;
        }
        UStaticMesh* RoleMesh = GetMeshForRole(EMeshChainSlotRole::Main);
        if (!RoleMesh)
        {
            continue;
        }

        const FBox B = RoleMesh->GetBoundingBox();
        const float TopZ = B.Max.Z;
        const float BotZ = B.Min.Z;

        // Per-orientation: which axis carries the back-face plane and where chain-left / chain-right
        // sit in mesh-local. Chain-right axis in mesh-local (after AxisAlignmentQuat):
        //   X -> +Y_mesh, InverseX -> -Y_mesh, Y -> -X_mesh, InverseY -> +X_mesh.
        int32 BackAxisIdx = 0;
        float BackCoord = 0.f;
        float LeftLat = 0.f;
        float RightLat = 0.f;
        switch (OrientationA)
        {
        case EMeshOrientation::X:
            BackAxisIdx = 0; BackCoord = B.Min.X; LeftLat = B.Min.Y; RightLat = B.Max.Y; break;
        case EMeshOrientation::InverseX:
            BackAxisIdx = 0; BackCoord = B.Max.X; LeftLat = B.Max.Y; RightLat = B.Min.Y; break;
        case EMeshOrientation::Y:
            BackAxisIdx = 1; BackCoord = B.Min.Y; LeftLat = B.Max.X; RightLat = B.Min.X; break;
        case EMeshOrientation::InverseY:
            BackAxisIdx = 1; BackCoord = B.Max.Y; LeftLat = B.Min.X; RightLat = B.Max.X; break;
        default:
            BackAxisIdx = 0; BackCoord = B.Min.X; LeftLat = B.Min.Y; RightLat = B.Max.Y; break;
        }

        float LatCoord = 0.f;
        float ZCoord = 0.f;
        switch (BakedPivotLocation)
        {
        case EBakedPivotLocation::TopLeft:     ZCoord = TopZ; LatCoord = LeftLat;  break;
        case EBakedPivotLocation::TopRight:    ZCoord = TopZ; LatCoord = RightLat; break;
        case EBakedPivotLocation::BottomLeft:  ZCoord = BotZ; LatCoord = LeftLat;  break;
        case EBakedPivotLocation::BottomRight: ZCoord = BotZ; LatCoord = RightLat; break;
        default:                               ZCoord = BotZ; LatCoord = (LeftLat + RightLat) * 0.5f; break;
        }

        const FVector LocalPivot = (BackAxisIdx == 0)
            ? FVector(BackCoord, LatCoord, ZCoord)
            : FVector(LatCoord, BackCoord, ZCoord);

        const FTransform SlotXf = Slot.Component->GetComponentTransform();
        return FTransform(GetActorTransform().GetRotation(), SlotXf.TransformPosition(LocalPivot));
    }
    return GetActorTransform();
}

void AMeshChainBuilder::Editor_BakeToISM()
{
    UWorld* World = GetWorld();
    ULevel* Level = GetLevel();
    if (!World || !Level || m_Slots.Num() == 0)
    {
        return;
    }

    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "Bake", "Mesh Chain: Bake to ISM"));
    // Modify the level so Actors-array growth from SpawnActor enters the transaction snapshot;
    // without this, undo cannot remove the freshly-spawned BakedActor from the level.
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

    auto BakeRole = [&](EMeshChainSlotRole InRole, const TCHAR* CompName)
    {
        UStaticMesh* RoleMesh = GetMeshForRole(InRole);
        if (!RoleMesh)
        {
            return;
        }

        // RF_Transactional so post-bake gizmo moves and per-property edits enter the undo buffer.
        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(BakedActor, FName(CompName), RF_Transactional);
        ISM->Modify();
        ISM->bHasPerInstanceHitProxies = true;
        ISM->SetStaticMesh(RoleMesh);
        ISM->SetMobility(EComponentMobility::Static);
        ApplyRoleCollision(InRole, static_cast<UStaticMeshComponent*>(ISM));
        ApplyRoleOverrideMaterial(InRole, static_cast<UStaticMeshComponent*>(ISM));

        if (RootISM == nullptr)
        {
            BakedActor->SetRootComponent(ISM);
            RootISM = ISM;
        }
        else
        {
            ISM->AttachToComponent(RootISM, FAttachmentTransformRules::KeepRelativeTransform);
        }

        // SetRootComponent / AttachToComponent already added the component to OwnedComponents,
        // but with the default CreationMethod (Native), so it never reaches InstanceComponents.
        // Without that, undo of post-bake gizmo moves silently no-ops because the editor cannot
        // see the component as a save-tracked instance. Re-add with CreationMethod=Instance so
        // AddOwnedComponent funnels it into InstanceComponents and the transaction system
        // serializes its transform on snapshot.
        BakedActor->RemoveOwnedComponent(ISM);
        ISM->CreationMethod = EComponentCreationMethod::Instance;
        BakedActor->AddOwnedComponent(ISM);
        ISM->RegisterComponent();

        // SetRootComponent on a fresh ISM (identity transform) snaps the actor to (0,0,0);
        // restore the chain builder's transform so instance-relative transforms resolve correctly.
        if (RootISM == ISM)
        {
            BakedActor->SetActorTransform(PivotXf);
        }

        for (const FMeshChainSlotState& Slot : m_Slots)
        {
            if (Slot.Role != InRole || !Slot.Component)
            {
                continue;
            }
            const FTransform InstanceLocalXf = Slot.Component->GetComponentTransform().GetRelativeTransform(PivotXf);
            ISM->AddInstance(InstanceLocalXf, /*bWorldSpace*/ false);
        }
    };

    BakeRole(EMeshChainSlotRole::Main,       TEXT("ISM_Main"));
    BakeRole(EMeshChainSlotRole::Transition, TEXT("ISM_Transition"));
    BakeRole(EMeshChainSlotRole::Corner,     TEXT("ISM_Corner"));

    if (!RootISM)
    {
        World->EditorDestroyActor(BakedActor, true);
        return;
    }

    MapUtilsIsmBaked::TagAndLabel(BakedActor);
    BakedActor->PostEditChange();
}
#endif // WITH_EDITOR
