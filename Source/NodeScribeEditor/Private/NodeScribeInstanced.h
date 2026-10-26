#pragma once

#include "CoreMinimal.h"

class FArrayProperty;
class FProperty;
class UClass;
class UObject;

/**
 * Arrays of instanced objects as text: a Gameplay Effect's `Components`, an
 * Input Mapping Context's triggers and modifiers.
 *
 * The Engine exports them as one line of nested parentheses with the class
 * and the subobject's path in quotes -- unreadable, and with both kinds of
 * quote the sheet could not even hold it. Here each element is a line with
 * its class as the Details panel names it, and its changed properties
 * indented under it, as a component in the sheet:
 *
 *     Components:
 *       Grant Tags to Target Actor
 *         Add Tags:
 *           Added = (GameplayTags=((TagName="Cooldown.Golem.Salto")))
 *
 * A block written back is the array's whole value, like any `X = value` line:
 * the elements come out in the text's order, an existing element of the same
 * class is kept (and the lines under it change it), and one the text does not
 * list leaves -- the return names it.
 */
namespace NodeScribeInstanced
{
	/** An array whose elements are instanced objects, owned by the object that holds it. */
	bool IsInstancedObjectArray(const FProperty* Property);

	/**
	 * The class as the text writes it: the Details panel's name (`Grant Tags
	 * to Target Actor`, `Hold`), or the path when that name is not unique.
	 */
	FString ClassText(const UClass* Class, const UClass* Base);

	/** The class by the name on screen, the class name, the name without the base's, or a path. */
	UClass* FindClass(const UClass* Base, const FString& Written, FString& OutError);

	/**
	 * One line per element at `Depth`, prefixed with `Keyword ` when there is
	 * one, and its changed properties one level in.
	 */
	void Read(const FArrayProperty* Property, const void* ArrayPtr, int32 Depth,
		const FString& Keyword, TArray<FString>& OutLines, TArray<FString>& OutUnreadable);

	/** An element as written: its class, and its property lines with the indentation taken off. */
	struct FElementText
	{
		FString Class;
		FString Body;
	};

	/**
	 * Replaces the array with the written elements.
	 *
	 * All or nothing on the classes: one that does not resolve and the array
	 * stays as it was. Property lines that fail are reported and the rest goes in.
	 *
	 * @param Outer the object that owns the elements (the asset or the CDO).
	 * @return false when nothing changed because of an error.
	 */
	bool Write(UObject* Outer, const FArrayProperty* Property, void* ArrayPtr,
		const TArray<FElementText>& Elements, const FString& Context,
		int32& OutApplied, TArray<FString>& OutDiagnostics);
}
