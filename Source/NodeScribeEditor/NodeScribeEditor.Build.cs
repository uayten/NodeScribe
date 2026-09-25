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
			// Blackboard and Behavior Tree. The concrete key types do not come in
			// by include: they are read through reflection, so that a key type
			// created by the project is read the same as the Engine's.
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
			// AnimGraph: AnimBlueprint nodes (UAnimGraphNode_*), pose pins and
			// state machines. AnimGraphRuntime brings the FAnimNode_* they wrap.
			"AnimGraph",
			"AnimGraphRuntime",
			"KismetCompiler",
			"Kismet",
			"MessageLog",
			// The .mcp.json the assistant reads. Additive: third-party entries
			// have to survive, so it is parse and rewrite, not a template.
			"Json",
			// Asset creation: the native toolset has no creation, only duplicate.
			"AssetTools",
			// Tags: read through the Manager, written to the ini by the editor module.
			"GameplayTags",
			"GameplayTagsEditor"
		});
	}
}
