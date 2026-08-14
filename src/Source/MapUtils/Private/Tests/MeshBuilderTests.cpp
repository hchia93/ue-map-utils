// Copyright © TenTen Studio. All rights reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Builder/MeshArcBuilder.h"
#include "Builder/MeshChainBuilder.h"
#include "Builder/MeshBuilderProfile.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace MeshBuilderTestsLocal
{
    constexpr float kFloatTolerance = 0.5f;

    // Editor-only transient world so SpawnActor and component registration behave; tests dispose it explicitly.
    static UWorld* MakeTransientEditorWorld()
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/ false, NAME_None, GetTransientPackage());
        if (World)
        {
            World->WorldType = EWorldType::Editor;
        }
        return World;
    }

    static void DestroyTransientEditorWorld(UWorld* World)
    {
        if (!World)
        {
            return;
        }
        World->DestroyWorld(/*bBroadcastWorldDestroyedEvent=*/ false);
    }

    // Issue a non-Interactive property-changed event so AMeshChainBuilder / AMeshArcBuilder run their
    // full reaction (RegenerateProfileIds + PruneOrphanSteps + rebuild). Default-construct + ValueSet
    // matches the path taken when LD commits a Details panel edit.
    static void FireNonInteractivePropertyChange(UObject* Object)
    {
        if (!Object)
        {
            return;
        }
        FPropertyChangedEvent Evt(/*InProperty=*/ nullptr);
        Evt.ChangeType = EPropertyChangeType::ValueSet;
        Object->PostEditChangeProperty(Evt);
    }

    static UStaticMesh* TryLoadCubeMesh()
    {
        return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    // Build a profile with a freshly generated GUID and the given mesh. The chain / arc builders auto-assign
    // GUIDs via RegenerateProfileIds, but tests need to know the id up front to reference Steps.
    static FMeshBuilderProfile MakeProfile(UStaticMesh* Mesh, EMeshOrientation Orient = EMeshOrientation::X)
    {
        FMeshBuilderProfile P;
        P.ProfileId = FGuid::NewGuid();
        P.Mesh = Mesh;
        P.Orientation = Orient;
        return P;
    }
}

// Pure step-count contract: Editor_AddNode / Editor_RemoveLast / Editor_ClearChain track Steps.Num().
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshChainStepLifecycleTest, "MapUtils.Builder.MeshChain.StepLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMeshChainStepLifecycleTest::RunTest(const FString&)
{
    using namespace MeshBuilderTestsLocal;

    UWorld* World = MakeTransientEditorWorld();
    if (!TestNotNull(TEXT("transient editor world"), World))
    {
        return false;
    }

    AMeshChainBuilder* Builder = World->SpawnActor<AMeshChainBuilder>();
    if (!TestNotNull(TEXT("chain builder spawn"), Builder))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    // Empty chain.
    TestEqual(TEXT("empty chain step count"), Builder->GetStepCount(), 0);

    // Seed a forward profile and commit so RegenerateProfileIds runs.
    UStaticMesh* Cube = TryLoadCubeMesh();
    FMeshBuilderProfile FwdA = MakeProfile(Cube);
    const FGuid FwdAId = FwdA.ProfileId;

    // Reach into protected ForwardProfiles via a local accessor subclass; same UObject identity.
    struct FAccessChain : public AMeshChainBuilder
    {
        using AMeshChainBuilder::ForwardProfiles;
        using AMeshChainBuilder::CornerProfiles;
        using AMeshChainBuilder::Steps;
    };
    FAccessChain* Access = static_cast<FAccessChain*>(Builder);
    Access->ForwardProfiles.Add(FwdA);
    FireNonInteractivePropertyChange(Builder);

    // Straight forward add.
    Builder->Editor_AddNode(FwdAId, 0.f);
    TestEqual(TEXT("after one forward add"), Builder->GetStepCount(), 1);
    TestTrue(TEXT("straight step has zero turn"), FMath::IsNearlyZero(Access->Steps[0].TurnAngleDeg));

    // Add a corner profile and activate it, then place a turn step.
    FMeshBuilderProfile CornerA = MakeProfile(Cube);
    const FGuid CornerAId = CornerA.ProfileId;
    Access->CornerProfiles.Add(CornerA);
    FireNonInteractivePropertyChange(Builder);
    Builder->Editor_SetActiveCornerProfileId(CornerAId);

    Builder->Editor_AddNode(FwdAId, 90.f);
    TestEqual(TEXT("after turn add"), Builder->GetStepCount(), 2);
    TestEqual(TEXT("turn angle captured"), Access->Steps[1].TurnAngleDeg, 90.f);

    // Invalid forward id is rejected.
    Builder->Editor_AddNode(FGuid(), 0.f);
    TestEqual(TEXT("invalid forward id is a no-op"), Builder->GetStepCount(), 2);

    // RemoveLast.
    Builder->Editor_RemoveLast();
    TestEqual(TEXT("remove last drops one"), Builder->GetStepCount(), 1);

    // Clear.
    Builder->Editor_ClearChain();
    TestEqual(TEXT("clear empties chain"), Builder->GetStepCount(), 0);

    DestroyTransientEditorWorld(World);
    return true;
}

// Removing a ForwardProfile entry should drop any Steps that referenced it on the next non-Interactive
// PostEditChangeProperty (PruneOrphanSteps). Steps referencing live profiles survive.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshChainPruneOrphanStepsTest, "MapUtils.Builder.MeshChain.PruneOrphanSteps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMeshChainPruneOrphanStepsTest::RunTest(const FString&)
{
    using namespace MeshBuilderTestsLocal;

    UWorld* World = MakeTransientEditorWorld();
    if (!TestNotNull(TEXT("transient editor world"), World))
    {
        return false;
    }

    AMeshChainBuilder* Builder = World->SpawnActor<AMeshChainBuilder>();
    if (!TestNotNull(TEXT("chain builder spawn"), Builder))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    struct FAccessChain : public AMeshChainBuilder
    {
        using AMeshChainBuilder::ForwardProfiles;
        using AMeshChainBuilder::Steps;
    };
    FAccessChain* Access = static_cast<FAccessChain*>(Builder);

    UStaticMesh* Cube = TryLoadCubeMesh();
    FMeshBuilderProfile FwdA = MakeProfile(Cube);
    FMeshBuilderProfile FwdB = MakeProfile(Cube);
    const FGuid FwdAId = FwdA.ProfileId;
    const FGuid FwdBId = FwdB.ProfileId;
    Access->ForwardProfiles.Add(FwdA);
    Access->ForwardProfiles.Add(FwdB);
    FireNonInteractivePropertyChange(Builder);

    Builder->Editor_AddNode(FwdAId, 0.f);
    Builder->Editor_AddNode(FwdBId, 0.f);
    Builder->Editor_AddNode(FwdAId, 0.f);
    TestEqual(TEXT("seeded three steps"), Builder->GetStepCount(), 3);

    // Simulate LD deleting profile A from the array.
    Access->ForwardProfiles.RemoveAll([FwdAId](const FMeshBuilderProfile& P) { return P.ProfileId == FwdAId; });
    FireNonInteractivePropertyChange(Builder);

    TestEqual(TEXT("orphan steps pruned"), Builder->GetStepCount(), 1);
    TestEqual(TEXT("surviving step references FwdB"), Access->Steps[0].ForwardProfileId, FwdBId);

    DestroyTransientEditorWorld(World);
    return true;
}

// RegenerateProfileIds assigns a GUID to any default-constructed profile and stamps its BodyInstance with the
// NoCollision named profile. Pre-existing valid ids stay put and their BodyInstance is left alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshChainRegenerateProfileIdsTest, "MapUtils.Builder.MeshChain.RegenerateProfileIds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMeshChainRegenerateProfileIdsTest::RunTest(const FString&)
{
    using namespace MeshBuilderTestsLocal;

    UWorld* World = MakeTransientEditorWorld();
    if (!TestNotNull(TEXT("transient editor world"), World))
    {
        return false;
    }

    AMeshChainBuilder* Builder = World->SpawnActor<AMeshChainBuilder>();
    if (!TestNotNull(TEXT("chain builder spawn"), Builder))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    struct FAccessChain : public AMeshChainBuilder
    {
        using AMeshChainBuilder::ForwardProfiles;
        using AMeshChainBuilder::CornerProfiles;
    };
    FAccessChain* Access = static_cast<FAccessChain*>(Builder);

    // Pre-existing profile with a known id and a sticky collision profile.
    FMeshBuilderProfile Pre;
    Pre.ProfileId = FGuid::NewGuid();
    Pre.BodyInstance.SetCollisionProfileName(TEXT("BlockAll"));
    const FGuid PreId = Pre.ProfileId;
    Access->ForwardProfiles.Add(Pre);

    // Default-constructed profile (invalid id).
    FMeshBuilderProfile Fresh;
    TestFalse(TEXT("fresh profile starts with invalid id"), Fresh.ProfileId.IsValid());
    Access->ForwardProfiles.Add(Fresh);

    // Also exercise the corner array branch of RegenerateProfileIds.
    FMeshBuilderProfile FreshCorner;
    Access->CornerProfiles.Add(FreshCorner);

    FireNonInteractivePropertyChange(Builder);

    // Re-read via the public accessor.
    const TArray<FMeshBuilderProfile>& FwdArr = Builder->GetForwardProfiles();
    const TArray<FMeshBuilderProfile>& CornerArr = Builder->GetCornerProfiles();
    if (!TestEqual(TEXT("forward array size"), FwdArr.Num(), 2))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    TestEqual(TEXT("pre-existing id untouched"), FwdArr[0].ProfileId, PreId);
    TestEqual(TEXT("pre-existing collision profile untouched"), FwdArr[0].BodyInstance.GetCollisionProfileName(), FName(TEXT("BlockAll")));

    TestTrue(TEXT("fresh forward profile id assigned"), FwdArr[1].ProfileId.IsValid());
    TestEqual(TEXT("fresh forward collision -> NoCollision"), FwdArr[1].BodyInstance.GetCollisionProfileName(), FName(TEXT("NoCollision")));

    TestEqual(TEXT("corner array size"), CornerArr.Num(), 1);
    TestTrue(TEXT("fresh corner profile id assigned"), CornerArr[0].ProfileId.IsValid());
    TestEqual(TEXT("fresh corner collision -> NoCollision"), CornerArr[0].BodyInstance.GetCollisionProfileName(), FName(TEXT("NoCollision")));

    DestroyTransientEditorWorld(World);
    return true;
}

// Arc parametric placement: at 100% coverage corners coincide on the seam (count == forwards); at a
// partial coverage the open arc places one extra corner at the trailing joint.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshArcCoverageTest, "MapUtils.Builder.MeshArc.Coverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMeshArcCoverageTest::RunTest(const FString&)
{
    using namespace MeshBuilderTestsLocal;

    UStaticMesh* Cube = TryLoadCubeMesh();
    if (!Cube)
    {
        // Without /Engine/BasicShapes/Cube the chord length is zero and the arc never places anything.
        // Don't pretend the test passed; report unable-to-run.
        AddWarning(TEXT("/Engine/BasicShapes/Cube unavailable; skipping arc placement test"));
        return true;
    }

    UWorld* World = MakeTransientEditorWorld();
    if (!TestNotNull(TEXT("transient editor world"), World))
    {
        return false;
    }

    AMeshArcBuilder* Builder = World->SpawnActor<AMeshArcBuilder>();
    if (!TestNotNull(TEXT("arc builder spawn"), Builder))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    struct FAccessArc : public AMeshArcBuilder
    {
        using AMeshArcBuilder::ForwardProfiles;
        using AMeshArcBuilder::CornerProfiles;
        using AMeshArcBuilder::Radius;
        using AMeshArcBuilder::ArcCoveragePercent;
        using AMeshArcBuilder::StartAngleDeg;
        using AMeshArcBuilder::bClockwise;
        using AMeshArcBuilder::InwardOffset;
    };
    FAccessArc* Access = static_cast<FAccessArc*>(Builder);

    Access->Radius = 500.f;
    Access->ArcCoveragePercent = 100.f;
    Access->StartAngleDeg = 0.f;
    Access->bClockwise = false;
    Access->InwardOffset = 0.f;

    FMeshBuilderProfile Fwd = MakeProfile(Cube);
    FMeshBuilderProfile Corner = MakeProfile(Cube);
    const FGuid CornerId = Corner.ProfileId;
    Access->ForwardProfiles.Add(Fwd);
    Access->CornerProfiles.Add(Corner);
    FireNonInteractivePropertyChange(Builder);
    Builder->Editor_SetActiveCornerProfileId(CornerId);

    // OnConstruction + PostEditChangeProperty have already triggered RebuildArc by this point.
    // Full circle total = ForwardCount + ForwardCount (corners == forwards because the seam joint
    // is shared). Capture this baseline.
    const int32 FullCircleSlots = Builder->GetSlotCount();
    TestTrue(TEXT("full circle places an even count (forwards == corners)"), FullCircleSlots > 0 && (FullCircleSlots % 2) == 0);

    // Active corner OFF at full circle: corners should disappear and only forwards remain.
    Builder->Editor_SetActiveCornerProfileId(FGuid());
    const int32 FullCircleForwardsOnly = Builder->GetSlotCount();
    TestEqual(TEXT("full circle forwards-only = half of fwd+corner total"), FullCircleForwardsOnly, FullCircleSlots / 2);

    // Re-enable corner and confirm the open-arc +1 trailing corner contract at partial coverage.
    Builder->Editor_SetActiveCornerProfileId(CornerId);

    // Forwards-only measurement at 50% coverage gives the partial forward count without corner noise.
    Access->ArcCoveragePercent = 50.f;
    Builder->Editor_SetActiveCornerProfileId(FGuid());
    FireNonInteractivePropertyChange(Builder);
    const int32 HalfCircleForwardsOnly = Builder->GetSlotCount();
    TestTrue(TEXT("50% coverage forwards-only produces slots"), HalfCircleForwardsOnly > 0);

    // Add corners back: open arc places ForwardsOnly + 1 corners (one cap per joint, no seam fold).
    Builder->Editor_SetActiveCornerProfileId(CornerId);
    FireNonInteractivePropertyChange(Builder);
    const int32 HalfCircleTotal = Builder->GetSlotCount();
    TestEqual(TEXT("open arc: corners = forwards + 1"), HalfCircleTotal, HalfCircleForwardsOnly + HalfCircleForwardsOnly + 1);

    // 0% coverage -> no slots placed.
    Access->ArcCoveragePercent = 0.f;
    FireNonInteractivePropertyChange(Builder);
    TestEqual(TEXT("0% coverage yields no slots"), Builder->GetSlotCount(), 0);

    // Clockwise flip: rebuild full circle CW; slot count must match CCW because geometry is symmetric.
    Access->ArcCoveragePercent = 100.f;
    Access->bClockwise = true;
    FireNonInteractivePropertyChange(Builder);
    TestEqual(TEXT("CW full circle matches CCW slot count"), Builder->GetSlotCount(), FullCircleSlots);

    DestroyTransientEditorWorld(World);
    return true;
}

// Arc RegenerateProfileIds: same contract as the chain builder. Verifies the shared logic actually runs in
// the arc actor's PostEditChangeProperty path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshArcRegenerateProfileIdsTest, "MapUtils.Builder.MeshArc.RegenerateProfileIds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMeshArcRegenerateProfileIdsTest::RunTest(const FString&)
{
    using namespace MeshBuilderTestsLocal;

    UWorld* World = MakeTransientEditorWorld();
    if (!TestNotNull(TEXT("transient editor world"), World))
    {
        return false;
    }

    AMeshArcBuilder* Builder = World->SpawnActor<AMeshArcBuilder>();
    if (!TestNotNull(TEXT("arc builder spawn"), Builder))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    struct FAccessArc : public AMeshArcBuilder
    {
        using AMeshArcBuilder::ForwardProfiles;
        using AMeshArcBuilder::CornerProfiles;
    };
    FAccessArc* Access = static_cast<FAccessArc*>(Builder);

    FMeshBuilderProfile Pre;
    Pre.ProfileId = FGuid::NewGuid();
    Pre.BodyInstance.SetCollisionProfileName(TEXT("BlockAll"));
    const FGuid PreId = Pre.ProfileId;
    Access->ForwardProfiles.Add(Pre);

    FMeshBuilderProfile Fresh;
    Access->ForwardProfiles.Add(Fresh);
    FMeshBuilderProfile FreshCorner;
    Access->CornerProfiles.Add(FreshCorner);

    FireNonInteractivePropertyChange(Builder);

    const TArray<FMeshBuilderProfile>& FwdArr = Builder->GetForwardProfiles();
    const TArray<FMeshBuilderProfile>& CornerArr = Builder->GetCornerProfiles();
    if (!TestEqual(TEXT("forward array size"), FwdArr.Num(), 2))
    {
        DestroyTransientEditorWorld(World);
        return false;
    }

    TestEqual(TEXT("pre-existing id untouched"), FwdArr[0].ProfileId, PreId);
    TestEqual(TEXT("pre-existing collision profile untouched"), FwdArr[0].BodyInstance.GetCollisionProfileName(), FName(TEXT("BlockAll")));
    TestTrue(TEXT("fresh forward id assigned"), FwdArr[1].ProfileId.IsValid());
    TestEqual(TEXT("fresh forward -> NoCollision"), FwdArr[1].BodyInstance.GetCollisionProfileName(), FName(TEXT("NoCollision")));
    TestEqual(TEXT("corner array size"), CornerArr.Num(), 1);
    TestTrue(TEXT("fresh corner id assigned"), CornerArr[0].ProfileId.IsValid());
    TestEqual(TEXT("fresh corner -> NoCollision"), CornerArr[0].BodyInstance.GetCollisionProfileName(), FName(TEXT("NoCollision")));

    DestroyTransientEditorWorld(World);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
