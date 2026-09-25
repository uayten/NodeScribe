#pragma once

#include "CoreMinimal.h"

/**
 * Gameplay Tags: read and create.
 *
 * A tag is neither an asset nor a property -- it lives in an ini,
 * `DefaultGameplayTags.ini` or a file under `Config/Tags/` --, so it did not
 * fit in `create_asset` nor in the sheet. It was the last kind of thing that
 * still depended on someone opening a settings window: a new cooldown needs
 * the tag to exist before the Gameplay Effect can grant it.
 *
 * Capability, not savings. There was not even a native tool to compare with.
 */
class FNodeScribeTags
{
public:
	/** The declared tags, filtered by a piece of the name. Empty brings them all. */
	static FString ReadTags(const FString& Filter);

	/**
	 * Creates one tag per line of the text. A tag already declared is skipped
	 * without error -- pasting the same list twice should not hurt.
	 *
	 * It neither deletes nor renames: both break every asset that uses the tag,
	 * and that calls for a decision, not a formatting side effect.
	 *
	 * @param Source  the target file, by the name shown in the editor
	 *                (`BossRush.ini`). Empty lets the Engine choose, which today
	 *                gives `DefaultGameplayTags.ini`.
	 */
	static FString WriteTags(const FString& Text, const FString& Source);
};
