#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UClass;
class UObject;

/**
 * Where the sheet reads from and where it writes.
 *
 * It exists for the same reason as `NodeScribePropertyText`: the sheet's
 * reader and writer are mirrors. If each decided on its own which object
 * carries the values, or which components exist, the sheet would come out
 * talking about one object and come back changing another -- and that saves a
 * wrong value into an asset, which compiles, runs and only shows itself in
 * playtest.
 */
namespace NodeScribeObjectTarget
{
	/**
	 * The object that carries the values.
	 *
	 * Blueprints and classes hold no value themselves: what keeps them is the
	 * compiled class's CDO. That is where the details panel reads from, so that
	 * is where the sheet has to read from to say the same thing as the screen.
	 */
	UObject* ResolveTarget(UObject* Object);

	/** The Blueprint behind the target, when there is one. */
	UBlueprint* FindBlueprint(const UObject* Requested, const UClass* Class);

	/**
	 * The target's components, by the name the sheet writes them with.
	 *
	 * Two sources, because a Blueprint keeps them in two places: what came from
	 * the C++ constructor lives in the CDO itself, and what was dragged in the
	 * editor lives as a template in the SimpleConstructionScript. Reading only
	 * one of them hides half the components without warning.
	 */
	TMap<FString, UObject*> CollectComponents(UObject* Target, UBlueprint* Blueprint);
}
