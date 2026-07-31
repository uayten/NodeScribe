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
			// Blackboard e Behavior Tree. Os tipos concretos de chave nao entram
			// por include: sao lidos por reflexao, para que chave criada pelo
			// projeto seja lida igual as da Engine.
			"AIModule",
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
			"MessageLog"
		});
	}
}
