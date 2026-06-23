#include "Operations/SanitizeMapReferenceCommandlet.h"

// Editor-only by design: drives package load + save. Trap any Runtime-type drift early.
static_assert(WITH_EDITOR, "MapUtils commandlets are editor-only; keep MapUtils.uplugin Module Type=Editor.");

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#include "MapUtilsModule.h"

USanitizeMapReferenceCommandlet::USanitizeMapReferenceCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 USanitizeMapReferenceCommandlet::Main(const FString& Params)
{
    UE_LOG(LogMapUtils, Display, TEXT("SanitizeMapReference commandlet starting..."));

    FOptions Options;
    if (!ParseOptions(Params, Options))
    {
        return 1;
    }
    UE_LOG(LogMapUtils, Display, TEXT("SanitizeMapReference: levels=%d pairs=%d dryRun=%s"), Options.LevelPaths.Num(), Options.Pairs.Num(), Options.bDryRun ? TEXT("true") : TEXT("false"));

    TMap<UObject*, UObject*> ReplaceMap;
    if (!ResolvePairs(Options.Pairs, ReplaceMap))
    {
        return 1;
    }

    TArray<FMapResult> Results;
    Results.Reserve(Options.LevelPaths.Num());
    int32 LevelsFailed = 0;

    for (const FString& LevelPath : Options.LevelPaths)
    {
        FMapResult Result;
        Result.MapPath = LevelPath;

        const bool bOK = ProcessLevel(LevelPath, ReplaceMap, Options.bDryRun, Result);
        if (!bOK)
        {
            ++LevelsFailed;
            if (Result.FailReason.IsEmpty())
            {
                Result.FailReason = TEXT("unknown failure");
            }
        }
        Results.Add(Result);

        UE_LOG(LogMapUtils, Display, TEXT("SanitizeMapReference: %s -> replaced=%d saved=%s reason=%s"), *LevelPath, Result.ReferencesReplaced, Result.bSaved ? TEXT("true") : TEXT("false"), *Result.FailReason);

        // GC between levels so package memory is reclaimed; replacement assets are
        // rooted in ResolvePairs so they survive.
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    // Unroot the replacement assets loaded for the swap.
    for (const TPair<UObject*, UObject*>& Entry : ReplaceMap)
    {
        if (Entry.Key)
        {
            Entry.Key->RemoveFromRoot();
        }
        if (Entry.Value)
        {
            Entry.Value->RemoveFromRoot();
        }
    }

    if (!WriteReport(Options.ReportPath, Results, Options.bDryRun))
    {
        return 1;
    }

    int32 TotalReplaced = 0;
    for (const FMapResult& R : Results)
    {
        TotalReplaced += R.ReferencesReplaced;
    }

    UE_LOG(LogMapUtils, Display, TEXT("SanitizeMapReference: complete. levels=%d failed=%d total_replaced=%d (dryRun=%s)"), Results.Num(), LevelsFailed, TotalReplaced, Options.bDryRun ? TEXT("true") : TEXT("false"));

    return LevelsFailed > 0 ? 2 : 0;
}

bool USanitizeMapReferenceCommandlet::ParseOptions(const FString& Params, FOptions& OutOptions) const
{
    FString LevelsValue;
    if (!FParse::Value(*Params, TEXT("-levels="), LevelsValue, false) || LevelsValue.IsEmpty())
    {
        UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: -levels=<paths> is required"));
        return false;
    }
    LevelsValue.TrimQuotesInline();
    LevelsValue.ParseIntoArray(OutOptions.LevelPaths, TEXT(","), true);
    for (FString& S : OutOptions.LevelPaths)
    {
        S.TrimStartAndEndInline();
    }

    FString ReplaceValue;
    if (!FParse::Value(*Params, TEXT("-replace="), ReplaceValue, false) || ReplaceValue.IsEmpty())
    {
        UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: -replace=<old=new,...> is required"));
        return false;
    }
    ReplaceValue.TrimQuotesInline();
    TArray<FString> PairTokens;
    ReplaceValue.ParseIntoArray(PairTokens, TEXT(","), true);
    for (const FString& Token : PairTokens)
    {
        FString OldPath, NewPath;
        if (!Token.Split(TEXT("="), &OldPath, &NewPath))
        {
            UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: bad replace pair (need old=new): %s"), *Token);
            return false;
        }
        FReplacePair Pair;
        Pair.OldPath = OldPath.TrimStartAndEnd();
        Pair.NewPath = NewPath.TrimStartAndEnd();
        OutOptions.Pairs.Add(Pair);
    }

    if (!FParse::Value(*Params, TEXT("-report="), OutOptions.ReportPath, false) || OutOptions.ReportPath.IsEmpty())
    {
        const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutOptions.ReportPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("SanitizeMapReference"), FString::Printf(TEXT("%s.json"), *Stamp));
    }
    OutOptions.ReportPath.TrimQuotesInline();

    OutOptions.bDryRun = FParse::Param(*Params, TEXT("dryrun"));

    return true;
}

bool USanitizeMapReferenceCommandlet::ResolvePairs(const TArray<FReplacePair>& Pairs, TMap<UObject*, UObject*>& OutReplaceMap) const
{
    for (const FReplacePair& Pair : Pairs)
    {
        UObject* OldObj = FSoftObjectPath(Pair.OldPath).TryLoad();
        if (!OldObj)
        {
            UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: cannot load old asset (must still exist): %s"), *Pair.OldPath);
            return false;
        }
        UObject* NewObj = FSoftObjectPath(Pair.NewPath).TryLoad();
        if (!NewObj)
        {
            UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: cannot load new asset: %s"), *Pair.NewPath);
            return false;
        }

        // Root so the inter-level GC does not collect them mid-run.
        OldObj->AddToRoot();
        NewObj->AddToRoot();
        OutReplaceMap.Add(OldObj, NewObj);
    }
    return OutReplaceMap.Num() > 0;
}

bool USanitizeMapReferenceCommandlet::ProcessLevel(const FString& LevelPath, const TMap<UObject*, UObject*>& ReplaceMap, bool bDryRun, FMapResult& OutResult) const
{
    UPackage* Package = LoadPackage(nullptr, *LevelPath, LOAD_None);
    if (!Package)
    {
        OutResult.FailReason = TEXT("LoadPackage failed");
        return false;
    }

    UWorld* World = UWorld::FindWorldInPackage(Package);
    if (!World)
    {
        OutResult.FailReason = TEXT("FindWorldInPackage returned null");
        return false;
    }

    World->PersistentLevel->OnLevelLoaded();
    World->PersistentLevel->UpdateLevelComponents(/*bRerunConstructionScripts=*/ false);

    // Replace references across every object that lives in this map package.
    TArray<UObject*> ObjectsInPackage;
    GetObjectsWithPackage(Package, ObjectsInPackage, /*bIncludeNestedObjects=*/ true);

    int32 TotalReplaced = 0;
    for (UObject* Obj : ObjectsInPackage)
    {
        if (!Obj)
        {
            continue;
        }
        FArchiveReplaceObjectRef<UObject> ReplaceAr(Obj, ReplaceMap, EArchiveReplaceObjectFlags::None);
        TotalReplaced += (int32)ReplaceAr.GetCount();
    }

    OutResult.ReferencesReplaced = TotalReplaced;

    if (bDryRun || TotalReplaced == 0)
    {
        OutResult.bSaved = false;
        return true;
    }

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetMapPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    SaveArgs.SaveFlags = SAVE_None;
    SaveArgs.bForceByteSwapping = false;
    SaveArgs.bWarnOfLongFilename = true;

    const bool bSaved = UPackage::SavePackage(Package, World, *PackageFilename, SaveArgs);
    OutResult.bSaved = bSaved;
    if (!bSaved)
    {
        OutResult.FailReason = TEXT("SavePackage failed");
        return false;
    }

    return true;
}

bool USanitizeMapReferenceCommandlet::WriteReport(const FString& ReportPath, const TArray<FMapResult>& Results, bool bDryRun) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("phase"), bDryRun ? TEXT("sanitize-map-reference-dryrun") : TEXT("sanitize-map-reference"));
    Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());

    int32 TotalReplaced = 0;
    int32 LevelsFailed = 0;
    TArray<TSharedPtr<FJsonValue>> LevelsJson;

    for (const FMapResult& R : Results)
    {
        TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
        L->SetStringField(TEXT("level"), R.MapPath);
        L->SetNumberField(TEXT("references_replaced"), R.ReferencesReplaced);
        L->SetBoolField(TEXT("saved"), R.bSaved);
        L->SetStringField(TEXT("fail_reason"), R.FailReason);
        LevelsJson.Add(MakeShared<FJsonValueObject>(L));

        TotalReplaced += R.ReferencesReplaced;
        if (!R.FailReason.IsEmpty())
        {
            ++LevelsFailed;
        }
    }

    Root->SetArrayField(TEXT("levels"), LevelsJson);
    Root->SetNumberField(TEXT("total_replaced"), TotalReplaced);
    Root->SetNumberField(TEXT("levels_failed"), LevelsFailed);

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: failed to serialize report JSON"));
        return false;
    }

    const FString ReportDir = FPaths::GetPath(ReportPath);
    if (!IFileManager::Get().DirectoryExists(*ReportDir))
    {
        IFileManager::Get().MakeDirectory(*ReportDir, /*Tree=*/ true);
    }

    if (!FFileHelper::SaveStringToFile(OutString, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMapUtils, Error, TEXT("SanitizeMapReference: failed to write report: %s"), *ReportPath);
        return false;
    }

    UE_LOG(LogMapUtils, Display, TEXT("SanitizeMapReference: report written: %s"), *ReportPath);
    return true;
}
