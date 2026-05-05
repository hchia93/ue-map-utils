#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class AMeshGridBuilder;

class FMeshGridBuilderDetails : public IDetailCustomization
{
public:

    static TSharedRef<IDetailCustomization> MakeInstance();

    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

    FReply OnGenerateClicked();
    FReply OnProcessClicked();
    FReply OnClearClicked();
    FReply OnBakeClicked();

    TWeakObjectPtr<AMeshGridBuilder> m_Target;
};
