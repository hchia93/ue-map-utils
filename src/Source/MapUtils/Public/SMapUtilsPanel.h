#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMapUtilsPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMapUtilsPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply OnBakeToInstanceClicked();
    FReply OnBakeToMergedInstanceClicked();
    FReply OnFixBakedIsmRotationClicked();
    FReply OnCreateBlockingVolumeClicked();
    FReply OnAuditClicked();
    FReply OnReviewModifiedClicked();
    FReply OnExportStaticMeshClicked();
    FReply OnExportCollisionClicked();
};
