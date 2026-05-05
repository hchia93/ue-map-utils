#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class AMeshChainBuilder;

class FMeshChainBuilderDetails : public IDetailCustomization
{
public:

    static TSharedRef<IDetailCustomization> MakeInstance();

    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

    FReply OnAddNodeForwardClicked();
    FReply OnAddNodeTurnClicked(float AngleDeg, bool bRight);
    FReply OnUndoClicked();
    FReply OnClearClicked();
    FReply OnRegenerateClicked();
    FReply OnBakeClicked();

    FText GetStatusText() const;

    TWeakObjectPtr<AMeshChainBuilder> m_Target;
};
