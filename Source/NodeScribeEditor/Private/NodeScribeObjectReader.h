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
};
