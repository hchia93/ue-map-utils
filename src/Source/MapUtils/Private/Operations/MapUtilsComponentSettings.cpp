#include "Operations/MapUtilsComponentSettings.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
    bool MaterialsEqual(const TArray<UMaterialInterface*>& A, const TArray<UMaterialInterface*>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 i = 0; i < A.Num(); ++i)
        {
            if (A[i] != B[i])
            {
                return false;
            }
        }
        return true;
    }
}

namespace MapUtilsComponentSettings
{
    void Copy(UStaticMeshComponent* Src, UStaticMeshComponent* Dst)
    {
        if (!Src || !Dst)
        {
            return;
        }

        // Materials: copy overrides only; nullptr means asset default and leaves Dst untouched.
        const TArray<UMaterialInterface*>& Overrides = Src->OverrideMaterials;
        for (int32 i = 0; i < Overrides.Num(); ++i)
        {
            if (Overrides[i] != nullptr)
            {
                Dst->SetMaterial(i, Overrides[i]);
            }
        }

        // Collision: setters only. Raw BodyInstance assignment skips archetype-delta tracking.
        Dst->SetCollisionProfileName(Src->GetCollisionProfileName());
        Dst->SetCollisionEnabled(Src->GetCollisionEnabled());
        Dst->SetCollisionObjectType(Src->GetCollisionObjectType());
        for (int32 Ch = 0; Ch < ECollisionChannel::ECC_MAX; ++Ch)
        {
            const ECollisionChannel C = static_cast<ECollisionChannel>(Ch);
            Dst->SetCollisionResponseToChannel(C, Src->GetCollisionResponseToChannel(C));
        }
        Dst->SetGenerateOverlapEvents(Src->GetGenerateOverlapEvents());
        Dst->SetNotifyRigidBodyCollision(Src->BodyInstance.bNotifyRigidBodyCollision);

        // Shadow casting.
        Dst->SetCastShadow(Src->CastShadow);
        Dst->bCastDynamicShadow = Src->bCastDynamicShadow;
        Dst->bCastStaticShadow = Src->bCastStaticShadow;
        Dst->bCastFarShadow = Src->bCastFarShadow;
        Dst->bCastVolumetricTranslucentShadow = Src->bCastVolumetricTranslucentShadow;
        Dst->bCastInsetShadow = Src->bCastInsetShadow;
        Dst->bCastShadowAsTwoSided = Src->bCastShadowAsTwoSided;
        Dst->bCastHiddenShadow = Src->bCastHiddenShadow;
        Dst->bSelfShadowOnly = Src->bSelfShadowOnly;

        // Custom depth / stencil (selection outlines, post-process masks).
        Dst->SetRenderCustomDepth(Src->bRenderCustomDepth);
        Dst->SetCustomDepthStencilValue(Src->CustomDepthStencilValue);
        Dst->CustomDepthStencilWriteMask = Src->CustomDepthStencilWriteMask;

        // Render passes / capture.
        Dst->SetRenderInMainPass(Src->bRenderInMainPass);
        Dst->SetRenderInDepthPass(Src->bRenderInDepthPass);
        Dst->bVisibleInRayTracing = Src->bVisibleInRayTracing;
        Dst->bVisibleInSceneCaptureOnly = Src->bVisibleInSceneCaptureOnly;
        Dst->bHiddenInSceneCapture = Src->bHiddenInSceneCapture;

        // Lighting.
        Dst->bAffectDistanceFieldLighting = Src->bAffectDistanceFieldLighting;
        Dst->bAffectDynamicIndirectLighting = Src->bAffectDynamicIndirectLighting;
        Dst->LightingChannels = Src->LightingChannels;
        Dst->LightmassSettings = Src->LightmassSettings;

        // Draw distance / translucency sort.
        Dst->LDMaxDrawDistance = Src->LDMaxDrawDistance;
        Dst->MinDrawDistance = Src->MinDrawDistance;
        Dst->TranslucencySortPriority = Src->TranslucencySortPriority;
    }

    bool AreGroupableSettingsEqual(UStaticMeshComponent* A, UStaticMeshComponent* B)
    {
        if (!A || !B)
        {
            return A == B;
        }

        // Mesh + materials form the primary group identity.
        if (A->GetStaticMesh() != B->GetStaticMesh())
        {
            return false;
        }

        TArray<UMaterialInterface*> MatA;
        TArray<UMaterialInterface*> MatB;
        A->GetUsedMaterials(MatA);
        B->GetUsedMaterials(MatB);
        if (!MaterialsEqual(MatA, MatB))
        {
            return false;
        }

        // Collision: profile, enable state, object type, per-channel response.
        if (A->GetCollisionProfileName() != B->GetCollisionProfileName()) return false;
        if (A->GetCollisionEnabled() != B->GetCollisionEnabled()) return false;
        if (A->GetCollisionObjectType() != B->GetCollisionObjectType()) return false;
        for (int32 Ch = 0; Ch < ECollisionChannel::ECC_MAX; ++Ch)
        {
            const ECollisionChannel C = static_cast<ECollisionChannel>(Ch);
            if (A->GetCollisionResponseToChannel(C) != B->GetCollisionResponseToChannel(C)) return false;
        }
        if (A->GetGenerateOverlapEvents() != B->GetGenerateOverlapEvents()) return false;
        if (A->BodyInstance.bNotifyRigidBodyCollision != B->BodyInstance.bNotifyRigidBodyCollision) return false;

        // Visual flags that decide whether two actors may share an ISMC.
        if (A->CastShadow != B->CastShadow) return false;
        if (A->bRenderCustomDepth != B->bRenderCustomDepth) return false;
        if (A->CustomDepthStencilValue != B->CustomDepthStencilValue) return false;
        if (A->CustomDepthStencilWriteMask != B->CustomDepthStencilWriteMask) return false;
        if (A->LDMaxDrawDistance != B->LDMaxDrawDistance) return false;
        if (A->MinDrawDistance != B->MinDrawDistance) return false;
        if (A->bAffectDistanceFieldLighting != B->bAffectDistanceFieldLighting) return false;
        if (A->TranslucencySortPriority != B->TranslucencySortPriority) return false;

        return true;
    }
}
