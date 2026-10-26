#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * An object as a sheet: one line per property, and only what differs from the
 * default.
 *
 * The compression comes from not sending what can be resolved on this side.
 * In a typical CDO 95% of the properties are at their factory value, and
 * `Gravity Scale = 1.0` carries no information at all. It is the same move as
 * the catalog, applied to properties instead of node names.
 *
 * The mirror -- writing the sheet back -- is `FNodeScribeObjectWriter`.
 */
class FNodeScribeObjectReader
{
public:
	/**
	 * @param Object  the target. Blueprints and classes become their CDO.
	 * @param Filter  empty returns what differs from the default. With text, it
	 *                returns the properties whose name matches -- including the
	 *                ones at their default, because there the question is "does
	 *                it exist and what is it worth", not "what changed".
	 */
	static FString ReadObject(UObject* Object, const FString& Filter);

	/**
	 * One object's changed properties as sheet lines, compared with its
	 * archetype, aligned and without the `# default` column.
	 *
	 * For the Designer, which writes a sheet per widget. Without the factory
	 * value: there the question is how the screen looks, and the default of
	 * every text and padding would double the reading to answer another one.
	 *
	 * @param Context      prefix for the names in the notes (`IsStillAnswer.Font`).
	 * @param bWhole       long values come out whole instead of being cut and named.
	 * @param OutTooLong   changed, but too long for the overview -- named, not vanished.
	 * @param OutUnreadable changed, with no text form that comes back the same.
	 */
	static void ReadProperties(const UObject* Target, const TSet<FName>& Skip,
		const FString& Context, bool bWhole, TArray<FString>& OutLines,
		TArray<FString>& OutTooLong, TArray<FString>& OutUnreadable);
};
