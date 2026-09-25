#pragma once

#include "CoreMinimal.h"

/**
 * Asset creation.
 *
 * It exists for **capability, not savings**: the Engine's native toolset has
 * `duplicate`, `move` and `delete`, and has no creation. Without this, every
 * new asset -- a cooldown Gameplay Effect, a BTTask, a BTService -- is a
 * request for a person to click, and the rest of the work stops waiting.
 *
 * It saves no tokens at all, and the README says so plainly.
 */
class FNodeScribeAssetMaker
{
public:
	/**
	 * @param Path     where to create it, with its name: `/Game/MyGame/Tests/BTTask_Foo`.
	 * @param Parent   the type, by the name shown on screen: `BTTask_BlueprintBase`,
	 *                 `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 * @param Options  *factory* properties, in sheet format, applied before
	 *                 creating.
	 *
	 * Options exists because some assets cannot be created from the type
	 * alone. An AnimBlueprint needs to know the skeleton, and so does a
	 * BlendSpace; without it the Engine opens a modal dialog asking for it --
	 * which nobody clicks on the other side of a call -- or creates a broken
	 * asset. Fixing it afterwards does not work: `Skeleton` is read-only on the
	 * finished asset, on purpose.
	 *
	 * The factory is a UObject like any other, so what writes into it is the
	 * same `FNodeScribeObjectWriter` as the sheet:
	 *
	 *     CreateAsset("/Game/Anims/ABP_Sophia", "AnimBlueprint",
	 *                 "TargetSkeleton = /Game/MetaHumans/.../metahuman_base_skel")
	 *
	 * @return the path of what was created, or why it failed.
	 *         Never overwrites: a taken path is an error, not a replacement.
	 */
	static FString CreateAsset(const FString& Path, const FString& Parent, const FString& Options = FString());
};
