#pragma once

#include "CoreMinimal.h"

class UEdGraph;

/** Where the new nodes go into the graph. */
struct FNodeScribeTarget
{
	/**
	 * A free spot below what already exists in the graph, so the new nodes do
	 * not land on top of what you already built.
	 */
	static FVector2D FindFreeOrigin(const UEdGraph* Graph);
};
