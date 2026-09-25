#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * AnimGraph support.
 *
 * An AnimGraph is not an execution graph: nodes do not "continue" into each
 * other, they feed a pose. The flow is reversed compared to the EventGraph --
 * the chain ends at the Output Pose, which already exists in the graph and is
 * never created.
 *
 * The function catalog does not work here: an anim node is a subclass of
 * `UAnimGraphNode_Base`, not a `UFunction`. That is why this file keeps its
 * own index, built the same way -- once, by normalised name.
 */
namespace NodeScribeAnimGraph
{

/** true when the graph is an AnimGraph, a state sub-graph or a transition sub-graph. */
bool IsAnimationGraph(const UEdGraph* Graph);

/** true when the graph is the interior of a state machine. */
bool IsStateMachineGraph(const UEdGraph* Graph);

/** true for a pose pin, local or component space. */
bool IsPosePin(const UEdGraphPin* Pin);

/** The node's pose input pin, if any. The first one, when there are several. */
UEdGraphPin* FindPoseInput(UEdGraphNode* Node);

/**
 * All of the node's pose input pins.
 *
 * A blend has several, and it is through them that the tree branches: in the
 * AnimGraph indentation opens an *input*, not an output as in the EventGraph.
 */
TArray<UEdGraphPin*> GetPoseInputs(UEdGraphNode* Node);

/** The node's pose output pins. */
TArray<UEdGraphPin*> GetPoseOutputs(UEdGraphNode* Node);

/**
 * The graph's Output Pose. It is born with the AnimGraph and is unique; writing
 * `Output Pose` on a line finds this node instead of creating another.
 */
UEdGraphNode* FindOutputPose(UEdGraph* Graph);

/** Result of a lookup by anim node name. */
struct FLookup
{
	UClass* NodeClass = nullptr;
	TArray<FString> Candidates;

	bool IsConfident() const { return NodeClass != nullptr; }
	bool IsAmbiguous() const { return NodeClass == nullptr && Candidates.Num() > 0; }
};

/**
 * Looks up the node class by the name the user wrote.
 * Compares against the title shown in the graph's menu -- `Blend Poses by
 * Bool`, `Layered blend per bone` --, which is the only name the user has a
 * way of knowing.
 */
FLookup FindNodeClass(const FString& Query);

/** Result of a lookup by animation asset. */
struct FAssetLookup
{
	UAnimationAsset* Asset = nullptr;
	TArray<FString> Candidates;

	bool IsConfident() const { return Asset != nullptr; }
	bool IsAmbiguous() const { return Asset == nullptr && Candidates.Num() > 0; }
};

/**
 * An animation asset by short name or full path.
 *
 * Two assets with the same short name do not become a choice: the list of
 * paths comes out, and the user decides. Loading the wrong one is a bug that
 * only shows up at runtime, with the right animation on the wrong character.
 */
FAssetLookup FindAnimationAsset(const FString& Query);

/**
 * The node class that plays a given asset -- Sequence Player for AnimSequence,
 * BlendSpace Player for BlendSpace, and so on. It is the same mapping
 * drag-and-drop uses.
 */
UClass* NodeClassForAsset(const UAnimationAsset* Asset);

/** Forces the index to be rebuilt. */
void Invalidate();

} // namespace NodeScribeAnimGraph
