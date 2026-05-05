#include "Operations/MapUtilsBlockingVolumeOps.h"

#include "MapUtilsModule.h"

#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/BlockingVolume.h"
#include "Engine/Level.h"
#include "Engine/Polys.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Model.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MapUtilsBlockingVolumeOps"

namespace
{
    /**
     * Whether this primitive component represents real geometry we want a BV to wrap.
     * Excludes triggers (UBoxComponent / USphereComponent / UCapsuleComponent) and editor billboards
     * which inflate bounds far beyond the visible mesh.
     */
    bool IsMeshLikeComponent(UPrimitiveComponent* Prim)
    {
        if (!Prim || !Prim->IsRegistered())
        {
            return false;
        }
        // SMC catches plain StaticMesh, ISM, HISM, SplineMesh; SkeletalMesh + BrushComponent for completeness.
        return Prim->IsA<UStaticMeshComponent>()
            || Prim->IsA<USkeletalMeshComponent>()
            || Prim->IsA<UBrushComponent>();
    }

    /**
     * Raw render AABB of a StaticMesh, with the asset's PositiveBoundsExtension /
     * NegativeBoundsExtension stripped. UStaticMesh::GetBoundingBox() returns ExtendedBounds
     * which is RenderBounds ± Extension; LDs use the extension as a shadow / VFX margin and
     * it inflates the BV well beyond the visible mesh.
     */
    FBox GetMeshRawAabb(UStaticMesh* Mesh)
    {
        if (!Mesh)
        {
            return FBox(ForceInit);
        }
        FBox Box = Mesh->GetBoundingBox();
        Box.Min += Mesh->GetNegativeBoundsExtension();
        Box.Max -= Mesh->GetPositiveBoundsExtension();
        return Box;
    }

    /**
     * Mesh AABB transformed into the supplied frame, bypassing FBoxSphereBounds-based accessors
     * (Prim->Bounds, Prim->CalcBounds) that include component-side BoundsScale and asset-side
     * BoundsExtension. Those settings are used for shadow / culling margin and inflate the wrap.
     */
    FBox CalcRawComponentBounds(UPrimitiveComponent* Prim, const FTransform& OutputFrameRelative)
    {
        if (!Prim)
        {
            return FBox(ForceInit);
        }

        // ISMC must come before SMC since it inherits from UStaticMeshComponent.
        if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Prim))
        {
            const FBox MeshLocal = GetMeshRawAabb(ISMC->GetStaticMesh());
            if (!MeshLocal.IsValid)
            {
                return FBox(ForceInit);
            }
            FBox Result(ForceInit);
            const int32 Count = ISMC->GetInstanceCount();
            for (int32 i = 0; i < Count; ++i)
            {
                FTransform InstCompLocal;
                if (!ISMC->GetInstanceTransform(i, InstCompLocal, /*bWorldSpace*/ false))
                {
                    continue;
                }
                // Mesh -> instance-local -> component-local -> output frame.
                const FTransform InstInOutputFrame = InstCompLocal * OutputFrameRelative;
                Result += MeshLocal.TransformBy(InstInOutputFrame);
            }
            return Result;
        }

        if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Prim))
        {
            const FBox MeshLocal = GetMeshRawAabb(SMC->GetStaticMesh());
            if (!MeshLocal.IsValid)
            {
                return FBox(ForceInit);
            }
            return MeshLocal.TransformBy(OutputFrameRelative);
        }

        // SkeletalMesh / BrushComponent: FBoxSphereBounds is acceptable, no per-mesh extension knobs
        // typically applied at the level-design tier.
        return Prim->CalcBounds(OutputFrameRelative).GetBox();
    }

    bool ComputeActorMeshWorldBounds(AActor* Actor, FBox& OutWorldBox)
    {
        if (!IsValid(Actor) || Actor->IsA<ABlockingVolume>())
        {
            return false;
        }

        TArray<UPrimitiveComponent*> Prims;
        Actor->GetComponents<UPrimitiveComponent>(Prims);

        FBox WorldAABB(ForceInit);
        for (UPrimitiveComponent* Prim : Prims)
        {
            if (!IsMeshLikeComponent(Prim))
            {
                continue;
            }
            // Output frame = world: pass component's full world transform.
            WorldAABB += CalcRawComponentBounds(Prim, Prim->GetComponentTransform());
        }

        if (!WorldAABB.IsValid)
        {
            return false;
        }
        OutWorldBox = WorldAABB;
        return true;
    }

    /**
     * Actor-local AABB of mesh-like components, packaged as a world-space BV transform + size.
     * Lets a single-selection BV align with the actor rotation (tight OBB).
     *
     * Uses CompWorldXf.GetRelativeTransform(ActorXf) to obtain the component-to-actor transform
     * regardless of attachment depth. Prim->GetRelativeTransform() alone is parent-relative and
     * gives wrong bounds for nested BP component hierarchies.
     */
    bool ComputeActorObb(AActor* Actor, FTransform& OutBvWorldXf, FVector& OutWorldSize)
    {
        if (!IsValid(Actor))
        {
            return false;
        }

        const FTransform ActorXf = Actor->GetActorTransform();

        TArray<UPrimitiveComponent*> Prims;
        Actor->GetComponents<UPrimitiveComponent>(Prims);

        FBox LocalAABB(ForceInit);
        for (UPrimitiveComponent* Prim : Prims)
        {
            if (!IsMeshLikeComponent(Prim))
            {
                continue;
            }
            const FTransform CompActorRel = Prim->GetComponentTransform().GetRelativeTransform(ActorXf);
            LocalAABB += CalcRawComponentBounds(Prim, CompActorRel);
        }
        if (!LocalAABB.IsValid)
        {
            return false;
        }

        const FVector LocalCenter = LocalAABB.GetCenter();
        const FVector LocalSize = LocalAABB.GetSize();

        // BV brush is unit-scaled relative to its actor; bake actor scale into the world size so the
        // unit brush plus actor-rotation transform places the volume tightly.
        OutWorldSize = LocalSize * ActorXf.GetScale3D();
        OutBvWorldXf = FTransform(ActorXf.GetRotation(), ActorXf.TransformPosition(LocalCenter), FVector::OneVector);

        UE_LOG(LogMapUtils, Verbose, TEXT("ComputeActorObb: actor=%s, localCenter=(%.1f,%.1f,%.1f) localSize=(%.1f,%.1f,%.1f) -> worldLoc=(%.1f,%.1f,%.1f) worldSize=(%.1f,%.1f,%.1f)"), *Actor->GetName(), LocalCenter.X, LocalCenter.Y, LocalCenter.Z, LocalSize.X, LocalSize.Y, LocalSize.Z, OutBvWorldXf.GetLocation().X, OutBvWorldXf.GetLocation().Y, OutBvWorldXf.GetLocation().Z, OutWorldSize.X, OutWorldSize.Y, OutWorldSize.Z);

        return true;
    }

    /**
     * Mirror of UActorFactory::CreateBrushForVolumeActor (engine internal). The previous direct
     * Brush+Build path produced empty BrushComponent bounds (nav octree warnings) because it
     * skipped the explicit UPolys creation, BrushComponent->Brush wiring, and FBSPOps::csgPrepMovingBrush
     * step that finalize the brush geometry. Reproduced here verbatim with our Cube dimensions.
     */
    void BuildVolumeBrush(AVolume* Volume, UCubeBuilder* Builder)
    {
        if (!Volume || !Builder)
        {
            return;
        }

        Volume->PreEditChange(nullptr);

        const EObjectFlags ObjectFlags = Volume->GetFlags() & (RF_Transient | RF_Transactional);

        Volume->PolyFlags = 0;
        Volume->Brush = NewObject<UModel>(Volume, NAME_None, ObjectFlags);
        Volume->Brush->Initialize(nullptr, true);
        Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, ObjectFlags);
        Volume->GetBrushComponent()->Brush = Volume->Brush;
        Volume->BrushBuilder = DuplicateObject<UBrushBuilder>(Builder, Volume);

        Builder->Build(Volume->GetWorld(), Volume);

        FBSPOps::csgPrepMovingBrush(Volume);

        // Strip any auto-assigned poly textures so the BV doesn't hold material refs.
        if (Volume->Brush != nullptr && Volume->Brush->Polys != nullptr)
        {
            for (int32 PolyIdx = 0; PolyIdx < Volume->Brush->Polys->Element.Num(); ++PolyIdx)
            {
                Volume->Brush->Polys->Element[PolyIdx].Material = nullptr;
            }
        }

        Volume->PostEditChange();
    }

    ABlockingVolume* SpawnBlockingVolume(UWorld* World, ULevel* TargetLevel, const FTransform& BvXf, const FVector& Size)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.OverrideLevel = TargetLevel;

        // Spawn at the full target transform (location + rotation). Brush polys are built in actor-
        // local space which is independent of actor rotation, so building after spawning rotated is
        // fine. Avoids the late-apply rotation path which left brush bounds and visible mesh out of
        // sync in some configurations.
        ABlockingVolume* Volume = World->SpawnActor<ABlockingVolume>(ABlockingVolume::StaticClass(), BvXf, SpawnParams);
        if (!Volume)
        {
            UE_LOG(LogMapUtils, Error, TEXT("SpawnActor<ABlockingVolume> failed."));
            return nullptr;
        }

        Volume->Modify();

        UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
        Builder->X = Size.X;
        Builder->Y = Size.Y;
        Builder->Z = Size.Z;

        BuildVolumeBrush(Volume, Builder);

        // Refresh component bounds cache after brush polys are settled.
        Volume->PostEditMove(true);

        return Volume;
    }
}

FMapUtilsBlockingVolumeWrapResult FMapUtilsBlockingVolumeOps::CreateBlockingVolumeForActors(const TArray<AActor*>& Actors)
{
    FMapUtilsBlockingVolumeWrapResult Result;

    if (!GEditor)
    {
        Result.ErrorText = LOCTEXT("NoEditor", "Editor not available.");
        return Result;
    }

    // Collect acceptable actors (non-BV, has bounds).
    TArray<AActor*> Acceptable;
    for (AActor* Actor : Actors)
    {
        FBox Probe;
        if (ComputeActorMeshWorldBounds(Actor, Probe))
        {
            Acceptable.Add(Actor);
        }
        else if (IsValid(Actor))
        {
            ++Result.SkippedActorCount;
            UE_LOG(LogMapUtils, Verbose, TEXT("CreateBlockingVolumeForActors: skipping %s (no usable bounds)"), *Actor->GetName());
        }
    }

    if (Acceptable.IsEmpty())
    {
        Result.ErrorText = LOCTEXT("NoBounds", "No valid actor with primitive bounds in selection. Select actors with static / skeletal / brush components.");
        return Result;
    }

    Result.SourceActorCount = Acceptable.Num();

    AActor* Anchor = Acceptable[0];
    UWorld* World = Anchor->GetWorld();
    ULevel* TargetLevel = Anchor->GetLevel();
    if (!World || !TargetLevel || !World->IsEditorWorld())
    {
        Result.ErrorText = LOCTEXT("BadWorld", "Invalid editor world.");
        return Result;
    }

    FTransform BvXf;
    FVector Size;

    if (Acceptable.Num() == 1)
    {
        // Single-actor: tight OBB aligned with the actor's rotation.
        if (!ComputeActorObb(Acceptable[0], BvXf, Size))
        {
            Result.ErrorText = LOCTEXT("ObbFailed", "Failed to compute oriented bounds for the selected actor.");
            return Result;
        }
    }
    else
    {
        // Multi-actor: identity-rotation world AABB across all sources (acknowledged L-corner case).
        FBox Combined(ForceInit);
        for (AActor* Actor : Acceptable)
        {
            FBox Box;
            if (ComputeActorMeshWorldBounds(Actor, Box))
            {
                Combined += Box;
            }
        }
        if (!Combined.IsValid)
        {
            Result.ErrorText = LOCTEXT("NoBounds2", "No usable combined bounds.");
            return Result;
        }
        BvXf = FTransform(FRotator::ZeroRotator, Combined.GetCenter(), FVector::OneVector);
        Size = Combined.GetSize();
    }

    {
        FScopedTransaction Transaction(LOCTEXT("CreateBV", "Create Blocking Volume for Actors"));
        TargetLevel->Modify();

        ABlockingVolume* Volume = SpawnBlockingVolume(World, TargetLevel, BvXf, Size);
        if (!Volume)
        {
            Result.ErrorText = LOCTEXT("SpawnFailed", "Failed to spawn BlockingVolume.");
            return Result;
        }

        Result.CreatedVolume = Volume;
        Result.bSuccess = true;

        GEditor->SelectNone(true, true);
        GEditor->SelectActor(Volume, true, true);
    }

    UE_LOG(LogMapUtils, Log, TEXT("CreateBlockingVolumeForActors: wrapping %d actor(s), skipped %d, mode=%s, size=(%.1f, %.1f, %.1f)"), Result.SourceActorCount, Result.SkippedActorCount, Acceptable.Num() == 1 ? TEXT("OBB") : TEXT("AABB"), Size.X, Size.Y, Size.Z);

    return Result;
}

#undef LOCTEXT_NAMESPACE
