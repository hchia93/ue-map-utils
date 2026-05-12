#include "Builder/MeshGridBuilder.h"

#include "MapUtilsModule.h"
#include "Operations/MapUtilsIsmBakedTag.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#endif // WITH_EDITORONLY_DATA

namespace MeshGridBuilderLocal
{
    constexpr float kBillboardLiftZ = 200.f;
}

AMeshGridBuilder::AMeshGridBuilder()
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
        // Lifted up so it sits clear of any cell mesh placed at origin and is the obvious actor click target.
        SpriteComponent->SetRelativeLocation(FVector(0.f, 0.f, MeshGridBuilderLocal::kBillboardLiftZ));
        SpriteComponent->SetupAttachment(SceneRoot);
    }
#endif // WITH_EDITORONLY_DATA
}

UMaterialInterface* AMeshGridBuilder::ResolveMaterial(EFloorConnectivity Connectivity) const
{
    UMaterialInterface* Variant = nullptr;
    switch (Connectivity)
    {
        case EFloorConnectivity::Fourway:        Variant = FourwayMaterial; break;
        case EFloorConnectivity::Threeway:       Variant = ThreewayMaterial; break;
        case EFloorConnectivity::TwowayAdjacent: Variant = TwowayAdjacentMaterial; break;
        case EFloorConnectivity::TwowayOpposite: Variant = TwowayOppositeMaterial; break;
        case EFloorConnectivity::Single:         Variant = SingleMaterial; break;
        case EFloorConnectivity::None:           Variant = NoneMaterial; break;
    }
    return Variant ? Variant : BaseMaterial.Get();
}

FVector2D AMeshGridBuilder::ComputeTileStep() const
{
    if (!TileMesh)
    {
        return FVector2D::ZeroVector;
    }
    const FVector Size = TileMesh->GetBoundingBox().GetSize();
    return FVector2D(Size.X, Size.Y);
}

#if WITH_EDITOR

FName AMeshGridBuilder::MakeCellName(int32 Row, int32 Col)
{
    return FName(*FString::Printf(TEXT("Cell_R%d_C%d"), Row, Col));
}

bool AMeshGridBuilder::ParseCellName(const FString& Name, int32& OutRow, int32& OutCol)
{
    if (!Name.StartsWith(TEXT("Cell_R")))
    {
        return false;
    }

    TArray<FString> Parts;
    Name.ParseIntoArray(Parts, TEXT("_"), /*InCullEmpty*/ true);
    if (Parts.Num() != 3)
    {
        return false;
    }
    if (!Parts[1].StartsWith(TEXT("R")) || !Parts[2].StartsWith(TEXT("C")))
    {
        return false;
    }

    OutRow = FCString::Atoi(*Parts[1].Mid(1));
    OutCol = FCString::Atoi(*Parts[2].Mid(1));
    return true;
}

UStaticMeshComponent* AMeshGridBuilder::SpawnCell(int32 Row, int32 Col, const FVector2D& Step)
{
    UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(this, MakeCellName(Row, Col), RF_Transactional);
    SMC->SetStaticMesh(TileMesh);
    SMC->SetMobility(EComponentMobility::Static);
    SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    SMC->SetupAttachment(SceneRoot);
    SMC->SetRelativeLocation(FVector(Row * Step.X, Col * Step.Y, 0.0));

    AddInstanceComponent(SMC);
    SMC->RegisterComponent();
    return SMC;
}

void AMeshGridBuilder::DestroyAllCellComponents()
{
    TArray<UStaticMeshComponent*> AllSMCs;
    GetComponents<UStaticMeshComponent>(AllSMCs);

    int32 Row = 0;
    int32 Col = 0;
    for (UStaticMeshComponent* SMC : AllSMCs)
    {
        if (!SMC)
        {
            continue;
        }
        if (ParseCellName(SMC->GetName(), Row, Col))
        {
            SMC->DestroyComponent();
        }
    }
}

void AMeshGridBuilder::Editor_Generate()
{
    if (!TileMesh)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MeshGridBuilder::Generate: TileMesh is unset; nothing to spawn."));
        return;
    }

    const FVector2D Step = ComputeTileStep();
    if (Step.X <= 0.0 || Step.Y <= 0.0)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MeshGridBuilder::Generate: TileMesh bounds are empty; cannot derive tile step."));
        return;
    }

    FScopedTransaction Tx(NSLOCTEXT("MeshGridBuilder", "Generate", "Mesh Grid: Generate"));
    Modify();

    DestroyAllCellComponents();

    for (int32 R = 0; R < GridSize.X; ++R)
    {
        for (int32 C = 0; C < GridSize.Y; ++C)
        {
            SpawnCell(R, C, Step);
        }
    }

    // Apply correct connectivity-driven materials immediately so the LD sees variants without an extra click.
    Editor_Process();
}

void AMeshGridBuilder::Editor_Process()
{
    FScopedTransaction Tx(NSLOCTEXT("MeshGridBuilder", "Process", "Mesh Grid: Process"));
    Modify();

    TArray<UStaticMeshComponent*> AllSMCs;
    GetComponents<UStaticMeshComponent>(AllSMCs);

    // Collect surviving cells keyed by (row, col); deleted cells simply won't be in the map.
    TMap<TPair<int32, int32>, UStaticMeshComponent*> AliveCells;
    int32 Row = 0;
    int32 Col = 0;
    for (UStaticMeshComponent* SMC : AllSMCs)
    {
        if (!SMC)
        {
            continue;
        }
        if (ParseCellName(SMC->GetName(), Row, Col))
        {
            AliveCells.Add(TPair<int32, int32>(Row, Col), SMC);
        }
    }

    auto IsAlive = [&AliveCells](int32 R, int32 C)
    {
        return AliveCells.Contains(TPair<int32, int32>(R, C));
    };

    for (TPair<TPair<int32, int32>, UStaticMeshComponent*>& Pair : AliveCells)
    {
        const int32 R = Pair.Key.Key;
        const int32 C = Pair.Key.Value;
        const bool bUp    = IsAlive(R - 1, C);
        const bool bDown  = IsAlive(R + 1, C);
        const bool bLeft  = IsAlive(R, C - 1);
        const bool bRight = IsAlive(R, C + 1);
        const int32 Count = (bUp ? 1 : 0) + (bDown ? 1 : 0) + (bLeft ? 1 : 0) + (bRight ? 1 : 0);

        // Both members of either axis -> the two neighbors face opposite sides (I shape).
        // Otherwise one from each axis -> L shape.
        const bool bAxisAligned = (bUp && bDown) || (bLeft && bRight);
        EFloorConnectivity Connectivity;
        switch (Count)
        {
            case 4:  Connectivity = EFloorConnectivity::Fourway; break;
            case 3:  Connectivity = EFloorConnectivity::Threeway; break;
            case 2:  Connectivity = bAxisAligned ? EFloorConnectivity::TwowayOpposite : EFloorConnectivity::TwowayAdjacent; break;
            case 1:  Connectivity = EFloorConnectivity::Single; break;
            default: Connectivity = EFloorConnectivity::None; break;
        }

        if (UMaterialInterface* Mat = ResolveMaterial(Connectivity))
        {
            Pair.Value->Modify();
            Pair.Value->SetMaterial(0, Mat);
        }
    }
}

void AMeshGridBuilder::Editor_Clear()
{
    FScopedTransaction Tx(NSLOCTEXT("MeshGridBuilder", "Clear", "Mesh Grid: Clear"));
    Modify();
    DestroyAllCellComponents();
}

void AMeshGridBuilder::Editor_BakeToISM()
{
    if (!TileMesh)
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MeshGridBuilder::BakeToISM: TileMesh is unset."));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TArray<UStaticMeshComponent*> AllSMCs;
    GetComponents<UStaticMeshComponent>(AllSMCs);

    // Group cell SMCs by their slot-0 material (mesh is fixed) and accumulate world-space bounds for the centroid pivot.
    TMap<UMaterialInterface*, TArray<UStaticMeshComponent*>> Groups;
    FBox WorldBounds(ForceInit);
    int32 Row = 0;
    int32 Col = 0;
    for (UStaticMeshComponent* SMC : AllSMCs)
    {
        if (!SMC || !ParseCellName(SMC->GetName(), Row, Col))
        {
            continue;
        }
        Groups.FindOrAdd(SMC->GetMaterial(0)).Add(SMC);
        WorldBounds += TileMesh->GetBoundingBox().TransformBy(SMC->GetComponentTransform());
    }

    if (Groups.IsEmpty())
    {
        UE_LOG(LogMapUtils, Warning, TEXT("MeshGridBuilder::BakeToISM: no cell components to bake."));
        return;
    }

    ULevel* Level = GetLevel();
    if (!Level)
    {
        return;
    }

    FScopedTransaction Tx(NSLOCTEXT("MeshGridBuilder", "Bake", "Mesh Grid: Bake to ISM"));
    // Level->Modify so the actor list snapshot lets undo remove the spawned actor.
    Level->Modify();

    FActorSpawnParameters SpawnParams;
    SpawnParams.OverrideLevel = Level;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // Rotation always rides on the builder so the baked actor distinguishes Local / World gizmo space.
    // Location is mode-dependent: Default = builder origin, corners anchor on the YZ plane nearest the
    // builder's own pivot (X), BoundCenter = full 3D AABB center.
    const FTransform BuilderXf = GetActorTransform();
    const float PlaneYZ = MeshBuilderPivot::NearestPlaneYZ(WorldBounds, BuilderXf.GetLocation().X);
    const FVector AnchorPivotLoc = MeshBuilderPivot::ResolveBoundsAnchor(WorldBounds, BakedPivotLocation, PlaneYZ);
    const bool bUseBuilderOrigin = (BakedPivotLocation == EBakedPivotLocation::Default);
    const FVector PivotLoc = bUseBuilderOrigin ? BuilderXf.GetLocation() : AnchorPivotLoc;
    const FTransform PivotXf(BuilderXf.GetRotation(), PivotLoc);
    AActor* BakedActor = World->SpawnActor<AActor>(AActor::StaticClass(), PivotXf, SpawnParams);
    if (!BakedActor)
    {
        return;
    }
    BakedActor->Modify();

    UInstancedStaticMeshComponent* RootISM = nullptr;

    int32 InstanceCount = 0;
    for (const TPair<UMaterialInterface*, TArray<UStaticMeshComponent*>>& Pair : Groups)
    {
        // RF_Transactional so post-bake gizmo moves and per-property edits enter the undo buffer.
        UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(BakedActor, NAME_None, RF_Transactional);
        ISM->Modify();
        ISM->bHasPerInstanceHitProxies = true;
        ISM->SetStaticMesh(TileMesh);
        ISM->SetMobility(EComponentMobility::Static);
        if (Pair.Key)
        {
            ISM->SetMaterial(0, Pair.Key);
        }

        if (RootISM == nullptr)
        {
            BakedActor->SetRootComponent(ISM);
            RootISM = ISM;
        }
        else
        {
            ISM->AttachToComponent(RootISM, FAttachmentTransformRules::KeepRelativeTransform);
        }

        // Ownership dance: SetRootComponent / AttachToComponent already added with default CreationMethod;
        // re-add with Instance so AddOwnedComponent funnels into InstanceComponents and the transaction system
        // serializes its transform on snapshot.
        BakedActor->RemoveOwnedComponent(ISM);
        ISM->CreationMethod = EComponentCreationMethod::Instance;
        BakedActor->AddOwnedComponent(ISM);
        ISM->RegisterComponent();

        // SetRootComponent on a fresh ISM (identity transform) snaps the actor to (0,0,0);
        // restore the full pivot transform so actor rotation is preserved and instance-relative
        // transforms resolve correctly against the rotated frame.
        if (RootISM == ISM)
        {
            BakedActor->SetActorTransform(PivotXf);
        }

        for (UStaticMeshComponent* CellSmc : Pair.Value)
        {
            const FTransform InstanceLocalXf = CellSmc->GetComponentTransform().GetRelativeTransform(PivotXf);
            ISM->AddInstance(InstanceLocalXf, /*bWorldSpace*/ false);
            ++InstanceCount;
        }
    }

    if (!RootISM)
    {
        World->EditorDestroyActor(BakedActor, true);
        return;
    }

    MapUtilsIsmBaked::TagAndLabel(BakedActor);
    BakedActor->PostEditChange();

    UE_LOG(LogMapUtils, Log, TEXT("MeshGridBuilder::BakeToISM: %d instance(s) across %d ISMC group(s)"), InstanceCount, Groups.Num());
}

#endif // WITH_EDITOR
