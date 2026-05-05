#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UMaterialInterface;
struct FAssetData;

class SMapUtilsPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMapUtilsPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply OnBakeToInstanceClicked();
    FReply OnBakeToMergedInstanceClicked();
    FReply OnCreateBlockingVolumeClicked();
    FReply OnAuditClicked();
    FReply OnReviewModifiedClicked();
    FReply OnExportStaticMeshClicked();
    FReply OnExportCollisionClicked();

    FString GetOverrideMaterialPath() const;
    void OnOverrideMaterialChanged(const FAssetData& AssetData);

    /** Optional material applied to every ISMC slot at bake time. WeakObjectPtr so a deleted asset doesn't dangle. */
    TWeakObjectPtr<UMaterialInterface> OverrideMaterial;
};
