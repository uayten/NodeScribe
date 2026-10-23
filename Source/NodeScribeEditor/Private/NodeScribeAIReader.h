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

	/**
	 * One Behavior Tree node as its line, without indentation or keyword:
	 * `CombatState = Selector`, `Wait (Wait Time = 10.00)`.
	 *
	 * Public because the writer compares lines: a node whose line already
	 * matches the text is kept as it is, with whatever the text cannot say.
	 */
	static FString DescribeBTNode(const UBTNode* Node);

private:
	static FString ReadBlackboard(UBlackboardData* Blackboard);
	static FString ReadBehaviorTree(UBehaviorTree* Tree);
};
