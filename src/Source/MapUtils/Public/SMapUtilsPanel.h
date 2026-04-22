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
    FReply OnAuditClicked();
    FReply OnConvertClicked();
    FReply OnExportStaticMeshClicked();
    FReply OnExportCollisionClicked();
};
