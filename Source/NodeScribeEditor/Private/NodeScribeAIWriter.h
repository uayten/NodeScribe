#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;
class UBlackboardData;
class UObject;

/**
 * The mirror of FNodeScribeAIReader: blackboard and Behavior Tree.
 *
 * The Behavior Tree is written through its editor graph, and the runtime tree
 * is rebuilt from it -- the asset keeps both, and writing only the runtime
 * side gives an asset that runs and shows up empty on screen.
 */
class FNodeScribeAIWriter
{
public:
	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	/** true when WriteAsset can write into this object. */
	static bool Handles(const UObject* Object);

	static FResult WriteAsset(UObject* Object, const FString& Text);

private:
	static FResult WriteBlackboard(UBlackboardData* Blackboard, const FString& Text);

	/**
	 * Unlike the sheet and the blackboard, the text is the **whole tree**, not
	 * a list of changes: a tree's shape only exists as a whole. A node the
	 * text does not mention leaves the graph, and the return names it.
	 *
	 * All or nothing. Every line is resolved before the graph is touched, and
	 * a single error leaves the asset as it was -- half a tree runs, and it is
	 * the wrong tree.
	 */
	static FResult WriteBehaviorTree(UBehaviorTree* Tree, const FString& Text);
};
