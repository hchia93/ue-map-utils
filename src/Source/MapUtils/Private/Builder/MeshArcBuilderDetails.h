#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Styling/SlateTypes.h"

class AMeshArcBuilder;
class IDetailCategoryBuilder;

class FMeshArcBuilderDetails : public IDetailCustomization
{
public:

    static TSharedRef<IDetailCustomization> MakeInstance();

    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

    void BuildCornerProfileRows(IDetailCategoryBuilder& Category);
    void BuildLifecycleRow(IDetailCategoryBuilder& Category);
    void BuildBakeRow(IDetailCategoryBuilder& Category);

    FReply OnRegenerateClicked();
    FReply OnBakeClicked();

    ECheckBoxState IsActiveCorner(FGuid ProfileId) const;
    void OnSetActiveCorner(ECheckBoxState NewState, FGuid ProfileId);

    FText GetStatusText() const;

    TWeakObjectPtr<AMeshArcBuilder> m_Target;
};
