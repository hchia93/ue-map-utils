using UnrealBuildTool;

public class MapUtils : ModuleRules
{
    public MapUtils(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "UnrealEd",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "MessageLog",
                "ContentBrowser",
                "Json",
                "JsonUtilities",
                "WorkspaceMenuStructure"
            }
        );
    }
}
