#include "Widgets/SMapUtilsActorChangesDialog.h"

#include "Operations/MapUtilsDiffOps.h"

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMapUtilsActorChangesDialog"

namespace
{
    TSharedRef<SWidget> BuildObjectSection(const FMapUtilsObjectChange& Change)
    {
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        int32 LineCount = 0;

        auto AddLine = [&Body, &LineCount](const FText& Text)
        {
            Body->AddSlot()
                .AutoHeight()
                .Padding(12, 1)
                [
                    SNew(STextBlock).Text(Text)
                ];
            ++LineCount;
        };

        if (Change.bHasOuterChange)           AddLine(LOCTEXT("OuterChange", "[Outer change] (e.g. moved to another level)"));
        if (Change.bHasNameChange)            AddLine(LOCTEXT("NameChange", "[Name change]"));
        if (Change.bHasExternalPackageChange) AddLine(LOCTEXT("PackageChange", "[External package change]"));
        if (Change.bHasPendingKillChange)     AddLine(LOCTEXT("PendingKillChange", "[Pending-kill change] (destroyed / restored)"));
        if (Change.bHasNonPropertyChanges)    AddLine(LOCTEXT("NonPropertyChanges", "[Non-property changes] (custom serialization)"));

        for (const FName& Prop : Change.ChangedProperties)
        {
            AddLine(FText::FromName(Prop));
        }

        if (LineCount == 0)
        {
            Body->AddSlot()
                .AutoHeight()
                .Padding(12, 1)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("NoProperties", "(no property-level changes recorded)"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ];
        }

        return SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4, 0, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Change.ObjectPath))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                Body
            ];
    }
}

void SMapUtilsActorChangesDialog::Construct(const FArguments& InArgs)
{
    TargetActor = InArgs._Actor;
    ParentWindow = InArgs._ParentWindow;

    AActor* Actor = TargetActor.Get();

    FText HeaderText;
    if (Actor)
    {
        const FText ActorLabel = FText::FromString(Actor->GetActorLabel());
        HeaderText = FText::Format(LOCTEXT("HeaderFmt", "Transaction changes on {0}"), ActorLabel);
    }
    else
    {
        HeaderText = LOCTEXT("ActorInvalid", "Actor no longer valid");
    }

    const FMapUtilsActorChangeSummary Summary = Actor ? FMapUtilsDiffOps::GetActorChanges(Actor) : FMapUtilsActorChangeSummary();

    TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

    if (Summary.Objects.IsEmpty())
    {
        Content->AddSlot()
            .AutoHeight()
            .Padding(0, 6)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("NoChanges", "No transaction entries touched this actor."))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ];
    }
    else
    {
        for (const FMapUtilsObjectChange& Change : Summary.Objects)
        {
            Content->AddSlot().AutoHeight()[BuildObjectSection(Change)];
        }
    }

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(STextBlock).Text(HeaderText)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 4)
            [
                SNew(SSeparator)
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()[Content]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 6, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SSpacer)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Close", "Close"))
                    .OnClicked(this, &SMapUtilsActorChangesDialog::OnCloseClicked)
                ]
            ]
        ]
    ];
}

FReply SMapUtilsActorChangesDialog::OnCloseClicked()
{
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

TSharedRef<SWindow> SMapUtilsActorChangesDialog::OpenWindow(AActor* Actor)
{
    FText Title;
    if (Actor)
    {
        const FText ActorLabel = FText::FromString(Actor->GetActorLabel());
        Title = FText::Format(LOCTEXT("TitleFmt", "Changes: {0}"), ActorLabel);
    }
    else
    {
        Title = LOCTEXT("TitleFallback", "Changes");
    }

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(Title)
        .ClientSize(FVector2D(520, 420))
        .SupportsMaximize(true)
        .SupportsMinimize(false);

    TSharedPtr<SMapUtilsActorChangesDialog> Dialog;
    Window->SetContent(
        SAssignNew(Dialog, SMapUtilsActorChangesDialog)
        .Actor(Actor)
        .ParentWindow(Window)
    );

    FSlateApplication::Get().AddWindow(Window);
    return Window;
}

#undef LOCTEXT_NAMESPACE
