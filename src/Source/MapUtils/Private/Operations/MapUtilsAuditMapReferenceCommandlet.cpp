#include "Operations/MapUtilsAuditMapReferenceCommandlet.h"

// Editor-only by design: drives the editor Asset Registry. Trap any Runtime-type drift early.
static_assert(WITH_EDITOR, "MapUtils commandlets are editor-only; keep MapUtils.uplugin Module Type=Editor.");

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "MapUtilsModule.h"

namespace
{
    /** Strip an object suffix so "/Game/Maps/L_A.L_A" and "/Game/Maps/L_A" both yield the package name. */
    static FString ToPackageName(const FString& Path)
    {
        FString Trimmed = Path;
        Trimmed.TrimStartAndEndInline();
        int32 DotIdx;
        if (Trimmed.FindChar(TEXT('.'), DotIdx))
        {
            Trimmed = Trimmed.Left(DotIdx);
        }
        return Trimmed;
    }
}

UMapUtilsAuditMapReferenceCommandlet::UMapUtilsAuditMapReferenceCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMapUtilsAuditMapReferenceCommandlet::Main(const FString& Params)
{
    UE_LOG(LogMapUtils, Display, TEXT("AuditMapReference commandlet starting..."));

    FOptions Options;
    if (!ParseOptions(Params, Options))
    {
        return 1;
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Force a synchronous full scan; dependency edges and on-disk existence must be current.
    UE_LOG(LogMapUtils, Display, TEXT("AuditMapReference: scanning asset registry..."));
    AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/ true);

    TArray<FName> MapPackages;
    CollectMapPackages(Options, AssetRegistry, MapPackages);

    if (MapPackages.IsEmpty())
    {
        UE_LOG(LogMapUtils, Warning, TEXT("AuditMapReference: no maps to audit (levels=%d scandir=%s)"), Options.LevelPaths.Num(), *Options.ScanDir);
    }

    TArray<FMapResult> Results;
    int32 TotalBroken = 0;

    for (FName MapPackage : MapPackages)
    {
        FMapResult Result;
        Result.MapPath = MapPackage.ToString();
        AuditMap(MapPackage, AssetRegistry, Result);

        if (Result.BrokenRefs.Num() > 0)
        {
            TotalBroken += Result.BrokenRefs.Num();
            Results.Add(MoveTemp(Result));
            UE_LOG(LogMapUtils, Warning, TEXT("AuditMapReference: %s has %d broken ref(s)"), *Results.Last().MapPath, Results.Last().BrokenRefs.Num());
            for (const FString& Ref : Results.Last().BrokenRefs)
            {
                UE_LOG(LogMapUtils, Warning, TEXT("    missing: %s"), *Ref);
            }
        }
    }

    if (!WriteReport(Options.ReportPath, Results, MapPackages.Num()))
    {
        return 1;
    }

    UE_LOG(LogMapUtils, Display, TEXT("AuditMapReference: complete. maps_scanned=%d maps_with_broken_refs=%d total_broken=%d"), MapPackages.Num(), Results.Num(), TotalBroken);

    return TotalBroken > 0 ? 2 : 0;
}

bool UMapUtilsAuditMapReferenceCommandlet::ParseOptions(const FString& Params, FOptions& OutOptions) const
{
    FString LevelsValue;
    if (FParse::Value(*Params, TEXT("-levels="), LevelsValue, false) && !LevelsValue.IsEmpty())
    {
        LevelsValue.TrimQuotesInline();
        TArray<FString> Raw;
        LevelsValue.ParseIntoArray(Raw, TEXT(","), true);
        for (const FString& S : Raw)
        {
            OutOptions.LevelPaths.Add(ToPackageName(S));
        }
    }

    if (!FParse::Value(*Params, TEXT("-scandir="), OutOptions.ScanDir, false) || OutOptions.ScanDir.IsEmpty())
    {
        OutOptions.ScanDir = TEXT("/Game");
    }
    OutOptions.ScanDir.TrimQuotesInline();

    if (!FParse::Value(*Params, TEXT("-report="), OutOptions.ReportPath, false) || OutOptions.ReportPath.IsEmpty())
    {
        const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutOptions.ReportPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("MapUtilsAuditMapReference"), FString::Printf(TEXT("%s.json"), *Stamp));
    }
    OutOptions.ReportPath.TrimQuotesInline();

    return true;
}

void UMapUtilsAuditMapReferenceCommandlet::CollectMapPackages(const FOptions& Options, IAssetRegistry& AssetRegistry, TArray<FName>& OutMapPackages) const
{
    // Explicit list wins; skip the directory scan entirely.
    if (Options.LevelPaths.Num() > 0)
    {
        for (const FString& Path : Options.LevelPaths)
        {
            OutMapPackages.AddUnique(FName(*Path));
        }
        return;
    }

    FARFilter Filter;
    Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*Options.ScanDir));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> MapAssets;
    AssetRegistry.GetAssets(Filter, MapAssets);

    for (const FAssetData& MapAsset : MapAssets)
    {
        OutMapPackages.AddUnique(MapAsset.PackageName);
    }
}

void UMapUtilsAuditMapReferenceCommandlet::AuditMap(FName MapPackage, IAssetRegistry& AssetRegistry, FMapResult& OutResult) const
{
    TArray<FName> Dependencies;
    AssetRegistry.GetDependencies(MapPackage, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

    const FString MapStr = MapPackage.ToString();

    for (FName Dependency : Dependencies)
    {
        const FString DepStr = Dependency.ToString();

        // /Script/* are code modules, not on-disk packages; self-edge is not a ref.
        if (DepStr.StartsWith(TEXT("/Script/")) || DepStr == MapStr)
        {
            continue;
        }
        if (!FPackageName::IsValidLongPackageName(DepStr))
        {
            continue;
        }

        if (!FPackageName::DoesPackageExist(DepStr))
        {
            OutResult.BrokenRefs.AddUnique(DepStr);
        }
    }
}

bool UMapUtilsAuditMapReferenceCommandlet::WriteReport(const FString& ReportPath, const TArray<FMapResult>& Results, int32 MapsScanned) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("phase"), TEXT("audit-map-reference"));
    Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
    Root->SetNumberField(TEXT("maps_scanned"), MapsScanned);
    Root->SetNumberField(TEXT("maps_with_broken_refs"), Results.Num());

    int32 TotalBroken = 0;
    TArray<TSharedPtr<FJsonValue>> ResultsJson;
    for (const FMapResult& Result : Results)
    {
        TotalBroken += Result.BrokenRefs.Num();

        TSharedRef<FJsonObject> MapJson = MakeShared<FJsonObject>();
        MapJson->SetStringField(TEXT("map"), Result.MapPath);
        MapJson->SetNumberField(TEXT("broken_count"), Result.BrokenRefs.Num());

        TArray<TSharedPtr<FJsonValue>> RefsJson;
        for (const FString& Ref : Result.BrokenRefs)
        {
            RefsJson.Add(MakeShared<FJsonValueString>(Ref));
        }
        MapJson->SetArrayField(TEXT("broken_refs"), RefsJson);

        ResultsJson.Add(MakeShared<FJsonValueObject>(MapJson));
    }

    Root->SetNumberField(TEXT("total_broken_refs"), TotalBroken);
    Root->SetArrayField(TEXT("results"), ResultsJson);

    FString OutString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        UE_LOG(LogMapUtils, Error, TEXT("AuditMapReference: failed to serialize report JSON"));
        return false;
    }

    const FString ReportDir = FPaths::GetPath(ReportPath);
    if (!IFileManager::Get().DirectoryExists(*ReportDir))
    {
        IFileManager::Get().MakeDirectory(*ReportDir, /*Tree=*/ true);
    }

    if (!FFileHelper::SaveStringToFile(OutString, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMapUtils, Error, TEXT("AuditMapReference: failed to write report: %s"), *ReportPath);
        return false;
    }

    UE_LOG(LogMapUtils, Display, TEXT("AuditMapReference: report written: %s"), *ReportPath);
    return true;
}
