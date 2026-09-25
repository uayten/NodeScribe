#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * The way back: nodes of a graph -> text in the NodeScribe format.
 *
 * It exists to close the loop. Without it, showing a finished graph to someone
 * (or to an assistant) costs a screenshot, which does not tell the pin values,
 * or Unreal's Ctrl+C, which costs ~1,000 tokens per node.
 *
 * It inherits the builder's principle in reverse: when the graph has something
 * the text format cannot say -- a chain that reconverges, a node without a
 * stable name -- the reader warns instead of emitting a text that looks
 * complete and is not. A text that comes back as a graph different from the
 * original is the worst possible result, because the difference only shows up
 * later.
 */
class FNodeScribeReader
{
public:
	struct FResult
	{
		FString Text;
		TArray<FNodeScribeDiagnostic> Diagnostics;

		int32 NodeCount = 0;
		int32 ErrorCount = 0;
		int32 WarningCount = 0;

		/**
		 * Nodes that exist in the graph and do not exist in the text: the data
		 * ones that feed nobody.
		 *
		 * They are harmless in a reading -- they become a note at the end. They
		 * are fatal for whoever plans to erase the graph and paste it back from
		 * this text, because they do not come back. That is why they are counted
		 * separately, and not only as a note.
		 */
		int32 LostNodeCount = 0;
	};

	/**
	 * @param Nodes      the nodes to transcribe. Nodes linked to something outside
	 *                   this list become a warning, not a silent link.
	 * @param Blueprint  owner of the graph, used to recognise its own variables.
	 */
	static FResult Read(const TArray<UEdGraphNode*>& Nodes, UBlueprint* Blueprint);

	/** Shortcut for the whole graph. */
	static FResult ReadGraph(UEdGraph* Graph, UBlueprint* Blueprint);
};
