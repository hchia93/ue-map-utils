#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

/**
 * Migrates LD-authored settings from a source SMC to an ISM-hosting destination.
 * Shared by 1:1 and merged bake paths.
 *
 * BodyInstance is mutated only via setters: raw assignment bypasses SavePackage's
 * archetype-delta tracking and the change won't persist.
 */
namespace MapUtilsComponentSettings
{
    /** Copy materials, collision, shadow, custom-depth, lighting, and draw-distance state from Src to Dst. */
    void Copy(UStaticMeshComponent* Src, UStaticMeshComponent* Dst);

    /**
     * Returns true when A and B agree on every field that decides whether two source
     * components may share one ISMC. Acts as a secondary group key beyond (mesh, reverseCulling);
     * non-groupable settings are inherited from the first representative.
     *
     * Pass bIgnoreMaterials=true when the caller will overwrite ISMC materials post-grouping
     * (e.g. ToolSetup OverrideMaterial), so source-side material divergence stops splitting groups.
     */
    bool AreGroupableSettingsEqual(UStaticMeshComponent* A, UStaticMeshComponent* B, bool bIgnoreMaterials = false);
}
