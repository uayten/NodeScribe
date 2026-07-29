#include "NodeScribeTarget.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/ToolkitManager.h"

FText FNodeScribeTarget::GetDisplayText() const
{
	if (!IsValid())
	{
		return NSLOCTEXT("NodeScribe", "InvalidTarget", "(grafo fechado)");
	}

	return FText::FromString(FString::Printf(
		TEXT("%s  \x2192  %s"),
		*Blueprint->GetName(),
		*Graph->GetName()));
}

TArray<TSharedPtr<FNodeScribeTarget>> FNodeScribeTarget::FindAllOpen()
{
	TArray<TSharedPtr<FNodeScribeTarget>> Targets;

	if (!GEditor)
	{
		return Targets;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return Targets;
	}

	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint)
		{
			continue;
		}

		TSharedPtr<IToolkit> Toolkit = FToolkitManager::Get().FindEditorForAsset(Asset);
		if (!Toolkit.IsValid() || !Toolkit->IsBlueprintEditor())
		{
			continue;
		}

		FBlueprintEditor* BlueprintEditor = StaticCastSharedPtr<FBlueprintEditor>(Toolkit).Get();
		if (!BlueprintEditor)
		{
			continue;
		}

		UEdGraph* FocusedGraph = BlueprintEditor->GetFocusedGraph();
		if (!FocusedGraph)
		{
			continue;
		}

		TSharedPtr<FNodeScribeTarget> Target = MakeShared<FNodeScribeTarget>();
		Target->Blueprint = Blueprint;
		Target->Graph = FocusedGraph;
		Targets.Add(Target);
	}

	return Targets;
}

FVector2D FNodeScribeTarget::FindFreeOrigin(const UEdGraph* Graph)
{
	if (!Graph || Graph->Nodes.Num() == 0)
	{
		return FVector2D(0.0f, 0.0f);
	}

	int32 MinX = MAX_int32;
	int32 MaxY = MIN_int32;

	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		MinX = FMath::Min(MinX, Node->NodePosX);
		MaxY = FMath::Max(MaxY, Node->NodePosY);
	}

	if (MinX == MAX_int32)
	{
		return FVector2D(0.0f, 0.0f);
	}

	// Uma faixa livre abaixo de tudo que ja' existe.
	return FVector2D(static_cast<float>(MinX), static_cast<float>(MaxY + 500));
}
