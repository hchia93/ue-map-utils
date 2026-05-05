#include "Operations/MapUtilsBakePreflight.h"

#include "MapUtilsModule.h"

#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "MapUtilsBakePreflight"

namespace
{
    struct FProfileKey
    {
        FName ProfileName = NAME_None;
        ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;

        bool operator==(const FProfileKey& Other) const
        {
            return ProfileName == Other.ProfileName && CollisionEnabled == Other.CollisionEnabled;
        }
    };

    uint32 GetTypeHash(const FProfileKey& Key)
    {
        return HashCombine(GetTypeHash(Key.ProfileName), static_cast<uint32>(Key.CollisionEnabled));
    }

    const TCHAR* CollisionEnabledToString(ECollisionEnabled::Type Value)
    {
        switch (Value)
        {
        case ECollisionEnabled::NoCollision:        return TEXT("NoCollision");
        case ECollisionEnabled::QueryOnly:          return TEXT("QueryOnly");
        case ECollisionEnabled::PhysicsOnly:        return TEXT("PhysicsOnly");
        case ECollisionEnabled::QueryAndPhysics:    return TEXT("QueryAndPhysics");
        case ECollisionEnabled::ProbeOnly:          return TEXT("ProbeOnly");
        case ECollisionEnabled::QueryAndProbe:      return TEXT("QueryAndProbe");
        default:                                    return TEXT("Unknown");
        }
    }
}

bool FMapUtilsBakePreflight::ConfirmBodyInstanceProfileUniformity(const TArray<FMapUtilsBakeProfileSample>& Samples)
{
    TMap<FProfileKey, TArray<FString>> Groups;
    for (const FMapUtilsBakeProfileSample& Sample : Samples)
    {
        FProfileKey Key;
        Key.ProfileName = Sample.ProfileName;
        Key.CollisionEnabled = Sample.CollisionEnabled;
        Groups.FindOrAdd(Key).Add(Sample.DisplayName);
    }

    if (Groups.Num() <= 1)
    {
        return true;
    }

    // Materialize for stable ordering: largest group first so the dominant cluster is read top-down.
    struct FOrderedGroup
    {
        FProfileKey Key;
        TArray<FString> Names;
    };
    TArray<FOrderedGroup> Ordered;
    Ordered.Reserve(Groups.Num());
    for (TPair<FProfileKey, TArray<FString>>& Pair : Groups)
    {
        FOrderedGroup G;
        G.Key = Pair.Key;
        G.Names = MoveTemp(Pair.Value);
        Ordered.Add(MoveTemp(G));
    }
    Ordered.Sort([](const FOrderedGroup& A, const FOrderedGroup& B)
    {
        return A.Names.Num() > B.Names.Num();
    });

    FString Body;
    Body.Reserve(512);
    Body += FString::Printf(TEXT("Selected sources have %d distinct collision profiles. Baking will keep them in separate ISMC components inside the merged actor, but custom BodyInstance overrides outside the profile (response channels, mass scale, damping) will be lost.\n\n"), Ordered.Num());

    constexpr int32 MaxNamesPerGroup = 8;

    int32 GroupIdx = 0;
    for (const FOrderedGroup& Group : Ordered)
    {
        ++GroupIdx;
        Body += FString::Printf(TEXT("[%d] Profile=%s, Enabled=%s -> %d source(s):\n"), GroupIdx, *Group.Key.ProfileName.ToString(), CollisionEnabledToString(Group.Key.CollisionEnabled), Group.Names.Num());

        const int32 ShowCount = FMath::Min(Group.Names.Num(), MaxNamesPerGroup);
        for (int32 i = 0; i < ShowCount; ++i)
        {
            Body += FString::Printf(TEXT("    - %s\n"), *Group.Names[i]);
        }
        if (Group.Names.Num() > ShowCount)
        {
            Body += FString::Printf(TEXT("    ... and %d more\n"), Group.Names.Num() - ShowCount);
        }
        Body += TEXT("\n");
    }

    Body += TEXT("Continue baking?");

    const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::OkCancel, FText::FromString(Body), LOCTEXT("ProfileMismatchTitle", "Collision Profile Mismatch"));

    UE_LOG(LogMapUtils, Log, TEXT("BakePreflight: %d profile group(s), user choice = %s"), Groups.Num(), Choice == EAppReturnType::Ok ? TEXT("Continue") : TEXT("Cancel"));

    return Choice == EAppReturnType::Ok;
}

#undef LOCTEXT_NAMESPACE
