#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;
class UBlackboardData;
class UBTNode;

/**
 * AI assets as text: blackboard and Behavior Tree.
 *
 * They are a case apart from the sheet because their information is not in
 * the top object's properties, but in a structure hanging from it. The generic
 * sheet of a BlackboardData sees a `Keys` property and nothing more; that of a
 * BehaviorTree sees a pointer to the root. Whoever wanted to know what the AI
 * does would have to follow pointers object by object, one call per node.
 */
class FNodeScribeAIReader
{
public:
	/** true when ReadAsset can read this object better than the sheet. */
	static bool Handles(const UObject* Object);

	/** The AI asset as text. Empty when it is not one of them. */
	static FString ReadAsset(UObject* Object);

private:
	static FString ReadBlackboard(UBlackboardData* Blackboard);
	static FString ReadBehaviorTree(UBehaviorTree* Tree);
};
