#pragma once

#include "CoreMinimal.h"

class UBlackboardData;
class UObject;

/**
 * The mirror of FNodeScribeAIReader. Only the blackboard for now.
 *
 * Not the Behavior Tree yet: the asset keeps the execution hierarchy *and* an
 * editor graph that has to stay in sync, and writing only the runtime side
 * gives an asset that runs and shows up empty on screen.
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
};
