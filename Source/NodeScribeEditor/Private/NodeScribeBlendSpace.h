#pragma once

#include "CoreMinimal.h"

class UBlendSpace;

/**
 * Filling a BlendSpace from text.
 *
 * It exists because the sheet does not reach. `SampleData` is an array of
 * structs and `BlendParameters` is a fixed array of structs: writing both
 * through the sheet would be assembling by hand what the Engine already
 * assembles -- and without its validation, which is what rebuilds the
 * interpolation grid. Without that grid the BlendSpace exists, opens, and
 * interpolates nothing.
 *
 * One line per sample, in the same spirit as the rest of the plugin:
 *
 *     axis X : Speed = 0 .. 600
 *     MM_Idle = 0
 *     MF_Unarmed_Walk_Fwd = 300
 *     MF_Unarmed_Jog_Fwd = 600
 *
 * In two dimensions the Y axis goes in the same way, and the sample gets the
 * second position:
 *
 *     axis X : Direction = -180 .. 180
 *     axis Y : Speed = 0 .. 600
 *     MM_Idle = 0, 0
 *     MF_Unarmed_Walk_Fwd = 0, 300
 *
 * The asset name follows the same rule as the AnimGraph: short name if it is
 * unique, full path if it is not. Two assets with the same short name do not
 * become a choice -- the list comes out, and whoever wrote it decides.
 */
namespace NodeScribeBlendSpace
{

struct FResult
{
	int32 SamplesAdded = 0;
	int32 AxesSet = 0;
	TArray<FString> Diagnostics;
};

/**
 * Applies the text to the BlendSpace.
 *
 * The axes are applied before the samples, whatever order they appear in the
 * text: a sample outside the axis range is refused by the Engine, and setting
 * the range afterwards does not bring it back.
 *
 * It does not erase what is already there. Like every write in the plugin, the
 * text is a list of changes -- to start over from scratch, create another asset.
 */
FResult Write(UBlendSpace* BlendSpace, const FString& Text);

/**
 * The way back: the axes and the samples, in the format Write accepts.
 *
 * It lives here, and not in a tool of its own, because the question is the
 * same one the sheet answers -- `read_object` on a BlendSpace calls this and
 * appends the block at the end. Before, the sheet said `I do not know how to
 * write the value of: Sample Data`, and there was no way to know what was
 * already in the asset without opening the editor. Writing blind into a
 * BlendSpace that already has samples is, by the standard the rest of the
 * plugin applies to any write without a read, guesswork.
 *
 * Empty when the BlendSpace has neither a named axis nor a sample.
 */
FString Read(const UBlendSpace* BlendSpace);

} // namespace NodeScribeBlendSpace
