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
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ScopedTransaction.h"
#endif // WITH_EDITOR

namespace MeshChainBuilderLocal
{
    constexpr float kEpsilon = 0.01f;
    constexpr float kBillboardLiftZ = 200.f;

#if WITH_EDITOR
    // Flush selection-outline + viewport after editor mutations destroy / spawn slot components.
    // Without this, deselected component proxies linger as a ghost until LD reselects the actor.
    static void RefreshViewportAfterMutation()
    {
        if (GEditor)
        {
            GEditor->NoteSelectionChange();
            GEditor->RedrawLevelEditingViewports(true);
        }
    }
#endif // WITH_EDITOR

    static FName MakeSlotComponentName(EMeshBuilderSlotType Type, int32 StepIndex)
    {
        const TCHAR* TypeTag = (Type == EMeshBuilderSlotType::Forward) ? TEXT("F") : TEXT("C");
        return FName(*FString::Printf(TEXT("Slot_%s_%d"), TypeTag, StepIndex));
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
    EnsureProfileIds();
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
    EnsureProfileIds();
    PruneOrphanSteps();
    RebuildChain();
}
#endif // WITH_EDITOR

const FMeshBuilderProfile* AMeshChainBuilder::FindProfile(FGuid InId, bool bIsCorner) const
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

void AMeshChainBuilder::PruneOrphanSteps()
{
    // Two-pass: peek first so Modify is only called when a prune will happen, and is called
    // pre-mutation so the transaction snapshot captures the original Steps for undo.
    bool bWouldPrune = false;
    for (const FMeshChainStep& Step : Steps)
    {
        if (FindProfile(Step.ForwardProfileId, /*bIsCorner=*/ false) == nullptr)
        {
            bWouldPrune = true;
            break;
        }
    }
    if (!bWouldPrune)
    {
        return;
    }
    Modify();
    Steps.RemoveAll([this](const FMeshChainStep& Step)
    {
        return FindProfile(Step.ForwardProfileId, /*bIsCorner=*/ false) == nullptr;
    });
}

void AMeshChainBuilder::EnsureProfileIds()
{
    auto FixUp = [](TArray<FMeshBuilderProfile>& Arr)
    {
        for (FMeshBuilderProfile& P : Arr)
        {
            if (!P.ProfileId.IsValid())
            {
                P.ProfileId = FGuid::NewGuid();
                // Freshly-added profile defaults to NoCollision so bake doesn't ship blocking geometry
                // until the LD explicitly opts in. Pre-existing profiles' BodyInstance is left intact.
                P.BodyInstance.SetCollisionProfileName(TEXT("NoCollision"));
            }
        }
    };
    FixUp(ForwardProfiles);
    FixUp(CornerProfiles);
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

UStaticMeshComponent* AMeshChainBuilder::AcquireSlotComponent(EMeshBuilderSlotType InType, int32 StepIndex, UStaticMesh* Mesh)
{
    if (!Mesh)
    {
        return nullptr;
    }

    const FName Name = MeshChainBuilderLocal::MakeSlotComponentName(InType, StepIndex);

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
    // Edit-time slots carry no collision; per-profile BodyInstance is only applied at Bake-to-ISM time.
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComp->RegisterComponent();
    AddInstanceComponent(NewComp);
    return NewComp;
}

void AMeshChainBuilder::ApplyProfileOverrideMaterial(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const
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
    // Modify so undo of a chain rebuild restores the prior material set.
    Comp->Modify();
    // Wipe any prior overrides first so unsetting the profile's material reverts to mesh defaults.
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

void AMeshChainBuilder::ApplyProfileCollision(const FMeshBuilderProfile& Profile, UStaticMeshComponent* Comp) const
{
    if (!Comp)
    {
        return;
    }
    // SetCollisionProfileName / SetCollisionEnabled cover NoCollision / InvisibleWall / any named profile.
    // Skip when value is unchanged so we don't trigger spurious physics-state recreation each rebuild.
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

void AMeshChainBuilder::DestroyAllSlots()
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

void AMeshChainBuilder::RebuildChain()
{
    using namespace MeshChainBuilderLocal;

    auto FindExisting = [&](EMeshBuilderSlotType InType, int32 StepIndex) -> int32
    {
        for (int32 i = 0; i < m_Slots.Num(); ++i)
        {
            if (m_Slots[i].Type == InType && m_Slots[i].StepIndex == StepIndex)
            {
                return i;
            }
        }
        return INDEX_NONE;
    };

    TArray<FMeshBuilderSlotState> NewSlots;
    TBitArray<> Reused(false, m_Slots.Num());

    FVector TailPos = FVector::ZeroVector;
    // TailRot is the chain-frame rotation (mesh-axis-agnostic). Its +X is the chain forward direction.
    FQuat TailRot = FQuat::Identity;

    // Shared placement core: writes the slot to its component (creating if needed), preserves the user's
    // rotation delta, and returns it so the caller can propagate to TailRot.
    auto PlaceSlot = [&](EMeshBuilderSlotType InType, int32 StepIndex, const FMeshBuilderProfile& Profile,
                         const FVector& SlotCenter, const FQuat& SlotBaselineRot)
    {
        UStaticMesh* Mesh = Profile.Mesh;
        const FQuat ExtraRot = Profile.Template.GetRotation();
        const FVector Scale = Profile.Template.GetScale3D();
        const FQuat   FinalBaselineRot = SlotBaselineRot * ExtraRot;
        const FTransform SlotBaseline(FinalBaselineRot, SlotCenter);

        const int32 ExistingIndex = FindExisting(InType, StepIndex);
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

        FMeshBuilderSlotState State;
        State.Type = InType;
        State.StepIndex = StepIndex;
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
            State.Component = AcquireSlotComponent(InType, StepIndex, Mesh);
        }
        if (!ensure(State.Component))
        {
            return UserRotDelta;
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
        return UserRotDelta;
    };

    auto EmitForwardSlot = [&](int32 StepIndex, const FMeshBuilderProfile& Profile)
    {
        UStaticMesh* Mesh = Profile.Mesh;
        if (!Mesh)
        {
            return; // empty profile behaves as a zero-length placeholder.
        }
        const EMeshOrientation Orient = Profile.Orientation;
        const float Len = GetMeshForwardLength(Mesh, Orient);
        if (Len <= kEpsilon)
        {
            return;
        }
        const FVector Trans = Profile.Template.GetTranslation();
        const FVector Scale = Profile.Template.GetScale3D();
        const float ScaledLen = Len * GetForwardScale(Scale, Orient);
        const FQuat MeshAlign = GetMeshAlignmentQuat(Orient);

        // Step 1: advance forward gap (Translation.X, accumulating into chain tail).
        const FVector ChainForward = TailRot.GetAxisX();
        const FVector ChainRight   = TailRot.GetAxisY();
        const FVector ChainUp      = TailRot.GetAxisZ();
        TailPos += ChainForward * Trans.X;

        // Step 2: chain center for tail progression (no Y/Z); slot center adds Y/Z displacement (per-slot only).
        const FVector ChainCenterPoint = TailPos + ChainForward * (ScaledLen * 0.5f);
        const FVector SlotCenter       = ChainCenterPoint + ChainRight * Trans.Y + ChainUp * Trans.Z;
        const FQuat   SlotBaselineRot  = TailRot * MeshAlign;

        const FQuat UserRotDelta = PlaceSlot(EMeshBuilderSlotType::Forward, StepIndex, Profile, SlotCenter, SlotBaselineRot);

        // Step 3: advance chain along forward by ScaledLen. User rotation propagates to subsequent slots.
        TailRot = UserRotDelta * TailRot;
        TailRot.Normalize();
        const FVector NewForward = TailRot.GetAxisX();
        TailPos = ChainCenterPoint + NewForward * (ScaledLen * 0.5f);
    };

    auto EmitCornerSlot = [&](int32 StepIndex, const FMeshBuilderProfile& Profile, bool bLeftTurn)
    {
        UStaticMesh* Mesh = Profile.Mesh;
        if (!Mesh)
        {
            return;
        }
        const float Len = GetMeshForwardLength(Mesh, Profile.Orientation);
        if (Len <= kEpsilon)
        {
            return;
        }
        FVector Trans = Profile.Template.GetTranslation();
        if (bLeftTurn)
        {
            // Mirror Trans.Y so positive stays toward turn-inside for both directions. Geometric mesh
            // mirror is avoided: bake packs corners sharing a GUID into one ISM (single bReverseCulling),
            // so negative-det instances back-face-cull. Author symmetric, or split L/R profiles.
            Trans.Y = -Trans.Y;
        }
        const FQuat MeshAlign = GetMeshAlignmentQuat(Profile.Orientation);

        // Corner is centered at the turn pivot (current TailPos); Y/Z of the template displace from the pivot.
        const FVector ChainRight = TailRot.GetAxisY();
        const FVector ChainUp    = TailRot.GetAxisZ();
        const FVector SlotCenter = TailPos + ChainRight * Trans.Y + ChainUp * Trans.Z;
        const FQuat   SlotBaselineRot = TailRot * MeshAlign;

        // Place but do not advance TailPos. The post-turn forward shift (Translation.X) is applied by the caller.
        PlaceSlot(EMeshBuilderSlotType::Corner, StepIndex, Profile, SlotCenter, SlotBaselineRot);
    };

    for (int32 StepIndex = 0; StepIndex < Steps.Num(); ++StepIndex)
    {
        const FMeshChainStep& Step = Steps[StepIndex];

        // Turn first (if any): place corner at current TailPos, then rotate, then apply post-turn shift.
        const bool bHasTurn = !FMath::IsNearlyZero(Step.TurnAngleDeg);
        if (bHasTurn)
        {
            const bool bLeftTurn = (Step.TurnAngleDeg < 0.f);
            const FMeshBuilderProfile* CornerProfile = FindProfile(ActiveCornerProfileId, /*bIsCorner=*/ true);
            if (CornerProfile)
            {
                EmitCornerSlot(StepIndex, *CornerProfile, bLeftTurn);
            }
            const float AngleRad = FMath::DegreesToRadians(Step.TurnAngleDeg);
            TailRot = TailRot * FQuat(FVector::UpVector, AngleRad);
            TailRot.Normalize();
            if (CornerProfile)
            {
                TailPos += TailRot.GetAxisX() * CornerProfile->Template.GetTranslation().X;
            }
        }

        // Forward placement: every step ends in one Forward profile.
        const FMeshBuilderProfile* ForwardProfile = FindProfile(Step.ForwardProfileId, /*bIsCorner=*/ false);
        if (ForwardProfile)
        {
            EmitForwardSlot(StepIndex, *ForwardProfile);
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
void AMeshChainBuilder::Editor_SetActiveCornerProfileId(FGuid InId)
{
    if (ActiveCornerProfileId == InId)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "SetActiveCorner", "Mesh Chain: Set Active Corner"));
    Modify();
    ActiveCornerProfileId = InId;
    // Rebuild so every turn step picks up the new active (or drops corner if active is None).
    RebuildChain();
    MeshChainBuilderLocal::RefreshViewportAfterMutation();
}

void AMeshChainBuilder::Editor_AddNode(FGuid ForwardProfileId, float TurnAngleDeg)
{
    if (!ForwardProfileId.IsValid())
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "AddNode", "Mesh Chain: Add Node"));
    Modify();
    FMeshChainStep Step;
    Step.ForwardProfileId = ForwardProfileId;
    Step.TurnAngleDeg = TurnAngleDeg;
    Steps.Add(Step);
    RebuildChain();
    MeshChainBuilderLocal::RefreshViewportAfterMutation();
}

void AMeshChainBuilder::Editor_RemoveLast()
{
    if (Steps.Num() == 0)
    {
        return;
    }
    FScopedTransaction Tx(NSLOCTEXT("MeshChainBuilder", "RemoveLast", "Mesh Chain: Undo"));
    Modify();
    Steps.Pop();
    RebuildChain();
    MeshChainBuilderLocal::RefreshViewportAfterMutation();
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
    MeshChainBuilderLocal::RefreshViewportAfterMutation();
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
    MeshChainBuilderLocal::RefreshViewportAfterMutation();
}

FTransform AMeshChainBuilder::ComputeBakePivotXf() const
{
    if (BakedPivotLocation == EBakedPivotLocation::Default)
    {
        return GetActorTransform();
    }

    if (BakedPivotLocation == EBakedPivotLocation::BoundCenter)
    {
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
        return FTransform(GetActorTransform().GetRotation(), WorldBounds.GetCenter());
    }

    // TL / TR / BL / BR: corners of the chain head's connection face (YZ plane of the first Forward slot).
    for (const FMeshBuilderSlotState& Slot : m_Slots)
    {
        if (Slot.Type != EMeshBuilderSlotType::Forward || !Slot.Component)
        {
            continue;
        }
        const FMeshBuilderProfile* P = FindProfile(Slot.SourceProfileId, /*bIsCorner=*/ false);
        if (!P || !P->Mesh)
        {
            continue;
        }

        const FBox B = P->Mesh->GetBoundingBox();
        const float TopZ = B.Max.Z;
        const float BotZ = B.Min.Z;

        // Per-orientation: which axis carries the back-face plane and where chain-left / chain-right
        // sit in mesh-local. Chain-right axis in mesh-local (after AxisAlignmentQuat):
        //   X -> +Y_mesh, InverseX -> -Y_mesh, Y -> -X_mesh, InverseY -> +X_mesh.
        int32 BackAxisIdx = 0;
        float BackCoord = 0.f;
        float LeftLat = 0.f;
        float RightLat = 0.f;
        switch (P->Orientation)
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

    // Group slots by (ProfileId, Type). One ISM per unique profile used in the chain.
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

        // SetRootComponent / AttachToComponent already added the component to OwnedComponents,
        // but with the default CreationMethod (Native), so it never reaches InstanceComponents.
        // Re-add with CreationMethod=Instance so the transaction system serializes its transform on snapshot.
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

    MapUtilsIsmBaked::TagAndLabel(BakedActor, GetFolderPath());
    BakedActor->PostEditChange();
}
#endif // WITH_EDITOR
