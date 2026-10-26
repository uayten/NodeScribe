#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * An Input Mapping Context's mappings as text, both ways: the `mappings`
 * section of `read_object` / `write_object`.
 *
 *     mappings
 *     SpaceBar -> IA_Jump
 *       trigger Pressed
 *     Gamepad_Left2D -> IA_Move
 *       modifier Dead Zone
 *         Lower Threshold = 0.25
 *
 * The generic sheet printed the whole `Default Key Mappings` struct array in
 * one line -- or, with a trigger in it, nothing at all, because the export
 * holds both kinds of quote. Through reflection, so the plugin does not link
 * against Enhanced Input.
 */
namespace NodeScribeInputMapping
{
	/** An Input Mapping Context. */
	bool Handles(const UObject* Object);

	/** The property the section replaces, for the sheet to leave out. */
	FName MappingsPropertyName();

	/** The section, starting with its `mappings` line. */
	FString Read(const UObject* Context);

	/** Splits a text at its `mappings` line. False when there is none. */
	bool SplitText(const FString& Text, FString& OutSheet, FString& OutMappings);

	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	/**
	 * Applies the section as a list of changes, like the sheet.
	 *
	 * `Key -> Action` is an anchor when that mapping exists and adds it at the
	 * end when not. Indented `trigger X` / `modifier X` lines under it are that
	 * mapping's whole set of triggers and modifiers, in order; a mapping line
	 * with nothing under it leaves them as they are. `delete mapping Key ->
	 * Action` removes one.
	 */
	FResult Write(UObject* Context, const FString& Text);
}
