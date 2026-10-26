#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UObject;

/**
 * A Blueprint's components, created and removed by the sheet.
 *
 * Through the same subsystem the Components panel uses, not by building SCS
 * nodes by hand: the panel's rules -- a scene component goes under the root,
 * an inherited component cannot be deleted, a name must be a valid variable
 * -- come with it, and a component made here is indistinguishable from one
 * added by clicking.
 */
namespace NodeScribeComponents
{
	/**
	 * The component tree as one header line, for the sheet's reader:
	 * `# components: CollisionCylinder, CharacterMesh0 in CollisionCylinder`.
	 *
	 * It names every component, changed or not. The sheet only opens the
	 * changed ones, and without this the parent a new component should go
	 * under could not be named without guessing. Empty when there are none.
	 */
	FString Describe(UBlueprint* Blueprint);

	/**
	 * Adds a component, as `component Name : Class [in Parent]` asks.
	 *
	 * The class is the name shown on screen (`Box Collision`), the class name
	 * (`BoxComponent`) or a path. Without a parent, a scene component goes
	 * under the root and any other one onto the actor, as the panel does.
	 *
	 * @return the new component's template, or null with the reason in OutError.
	 */
	UObject* Add(UBlueprint* Blueprint, const FString& Name, const FString& ClassText,
		const FString& ParentName, FString& OutError);

	/** Removes a component this Blueprint added. Inherited ones are refused. */
	bool Remove(UBlueprint* Blueprint, const FString& Name, FString& OutError);
}
