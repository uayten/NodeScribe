#include "NodeScribeTarget.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

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

	// A free band below everything that already exists.
	return FVector2D(static_cast<float>(MinX), static_cast<float>(MaxY + 500));
}
