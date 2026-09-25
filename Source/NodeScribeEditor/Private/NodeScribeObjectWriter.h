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
 * nothing. The one thing it creates is a Blueprint variable, from a
 * `variable Name : Type` line, and the one thing it removes is a variable,
 * from `delete variable Name`. It never creates components.
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
};
