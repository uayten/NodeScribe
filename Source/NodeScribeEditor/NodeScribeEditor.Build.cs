using UnrealBuildTool;

public class NodeScribeEditor : ModuleRules
{
	public NodeScribeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ApplicationCore",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"Projects",
			"GraphEditor",
			"BlueprintGraph",
			"KismetCompiler",
			"Kismet",
			"WorkspaceMenuStructure"
		});
	}
}
