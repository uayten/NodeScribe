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
			// AnimGraph: nodes de AnimBlueprint (UAnimGraphNode_*), pose pins e
			// maquina de estado. AnimGraphRuntime traz os FAnimNode_* que eles
			// embrulham.
			"AnimGraph",
			"AnimGraphRuntime",
			"KismetCompiler",
			"Kismet",
			"MessageLog",
			// Criar asset: o nativo nao tem criacao, so' duplicate.
			"AssetTools",
			// Tag: ler pelo Manager, escrever no ini pelo modulo de editor.
			"GameplayTags",
			"GameplayTagsEditor"
		});
	}
}
