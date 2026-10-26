#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

/**
 * A Widget Blueprint's Designer as text, both ways.
 *
 * The sheet of a Widget Blueprint reads the generated class's CDO, where each
 * widget is only a variable that says `= None`. What the Designer shows -- the
 * hierarchy, each widget's text and font, its slot's padding -- lives in the
 * Blueprint's WidgetTree, and before this the only way there was the native
 * UMG toolset: a ~62 KB schema plus one property listing per widget.
 *
 * The text is the sheet's own vocabulary, one level down. A widget is written
 * like a component, `Name : Class`, its changed properties go indented under
 * it, and indentation is also the hierarchy:
 *
 *     designer
 *     Root : VerticalBox
 *       IsStillRow : HorizontalBox
 *         IsStillLabel : TextBlock
 *           Text = "Is still?"
 *         IsStillAnswer : TextBlock variable
 *           slot Padding = (Left=8.0,Top=0.0,Right=0.0,Bottom=0.0)
 *
 * Reader and writer live in one file because they are mirrors: the line the
 * reader writes for a widget is exactly the line that changes nothing when
 * written back.
 */
namespace NodeScribeWidgetTree
{
	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	/** The line that opens the Designer section of a Widget Blueprint's text. */
	extern const TCHAR* const SectionKeyword;

	/** Widget names, which the sheet leaves out: the Designer says them better. */
	TSet<FName> CollectWidgetNames(const UWidgetBlueprint* Blueprint);

	/**
	 * The Designer section, starting with its `designer` line.
	 *
	 * @param NormalizedFilter  empty gives the whole tree. With text, it gives
	 *                          the widgets whose name or class matches, under
	 *                          their ancestors as bare lines -- so the answer
	 *                          still says where each one is, and still pastes back.
	 */
	FString Read(const UWidgetBlueprint* Blueprint, const FString& NormalizedFilter);

	/**
	 * Splits a Widget Blueprint's text into sheet and Designer at the
	 * `designer` line. Each half keeps the other's lines as blanks, so the line
	 * numbers in the diagnostics still point at the text that was sent.
	 *
	 * @return false when there is no Designer section.
	 */
	bool SplitText(const FString& Text, FString& OutSheet, FString& OutDesigner);

	/**
	 * Applies the Designer section.
	 *
	 * A list of changes, like the sheet: a widget the text does not mention is
	 * left alone, and pasting the whole reading back changes nothing. A new
	 * name creates a widget; `rename A -> B` and `delete widget X` are the only
	 * ways to rename or remove one, so neither can happen through a slip.
	 *
	 * The structure is all or nothing -- every widget line is resolved before
	 * the tree is touched, because half a row is a row in the wrong place.
	 * Property lines then go through the sheet's writer, line by line, as on
	 * any other object.
	 */
	FResult Write(UWidgetBlueprint* Blueprint, const FString& Text);
}
