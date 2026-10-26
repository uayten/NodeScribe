#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * The sheet's mirror: text goes in, properties change.
 *
 * Accepts what `FNodeScribeObjectReader` produces, without the comments --
 * component block, indented struct member, and `= default` to go back to the
 * factory value.
 *
 * It only touches what the line asks for. The text is a list of changes, not a
 * description of the final state: pasting a whole sheet back should change
 * nothing. What it creates and removes is said by a word of its own:
 * `variable Name : Type` / `delete variable Name`, and `component Name :
 * Class` / `delete component Name`. A block of instanced objects is the one
 * value that can drop elements, because the block is that array's whole value.
 *
 * Nothing here guesses. A property name that does not resolve becomes a
 * diagnostic with the similar candidates, and the other lines keep being
 * applied -- a property written in the wrong place is the expensive mistake
 * here, because it saves into the asset and only shows up at runtime.
 */
class FNodeScribeObjectWriter
{
public:
	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	static FResult WriteObject(UObject* Object, const FString& Text);

	/**
	 * Property lines straight onto one object: no Blueprint, so no `variable`
	 * lines, and no component blocks -- struct blocks and `= default` work as
	 * in the sheet.
	 *
	 * For the Designer, whose widgets are objects inside the Blueprint and not
	 * its CDO. Marking and compiling the Blueprint is the caller's job.
	 */
	static FResult WriteProperties(UObject* Target, const FString& Text);
};
