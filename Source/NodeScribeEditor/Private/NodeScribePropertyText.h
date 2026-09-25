#pragma once

#include "CoreMinimal.h"

class FProperty;
class FStructProperty;
class UEdGraphPin;
struct FEdGraphPinType;

/**
 * Property value <-> text, and the vocabulary of names both sides use.
 *
 * It exists because reader and writer are mirrors. If each formatted in its
 * own way, the text would come out of one and not come back through the other
 * -- and for properties the difference shows up as a wrong value saved into
 * an asset, which compiles, runs and only shows itself in playtest. Everything
 * that translates values lives here, once.
 *
 * `DescribePinType` and the quoting came from NodeScribeReader for exactly
 * that reason: the type the sheet writes has to be the same one the graph
 * writes, otherwise the user learns two vocabularies for the same thing.
 */
namespace NodeScribePropertyText
{
	// -- Quotes --------------------------------------------------------------

	/** Characters that would change the meaning of the line if left bare. */
	bool NeedsQuotes(const FString& Value);

	/**
	 * The parser has no escape sequence: a quote inside the value would break
	 * the line. We pick the quote that does not appear in the text; if both
	 * appear, the caller warns instead of emitting something that does not come
	 * back.
	 */
	bool TryQuote(const FString& Value, FString& OutQuoted);

	// -- Names ---------------------------------------------------------------

	/**
	 * `-90.0` instead of `-90.000000`.
	 *
	 * It is not about saving tokens -- that would be ~1% of the cost, which this
	 * project does not chase. It is that six trailing zeros hide the number in
	 * the noise, and seeing the value at a glance is what makes the text worth it.
	 */
	FString FormatFloat(double Value);

	/**
	 * The type as the format writes it: `Float`, `Integer`, `TimerHandle`,
	 * `Array of Name`. Empty when the type has no form in text.
	 */
	FString DescribePinType(const FEdGraphPinType& PinType);

	/** The same, starting from a property. Empty when it has no form. */
	FString DescribeType(const FProperty* Property);

	/** The name shown in the details panel: `bShowMouseCursor` -> `Show Mouse Cursor`. */
	FString DisplayName(const FProperty* Property);

	// -- Pins ----------------------------------------------------------------

	/** The white execution wire, which the format writes as indentation. */
	bool IsExecPin(const UEdGraphPin* Pin);

	/** A pin whose value can only come from an asset choice, not from text. */
	bool IsObjectLikePin(const UEdGraphPin* Pin);

	// -- Visibility ----------------------------------------------------------

	/**
	 * true when the property goes into the sheet.
	 *
	 * What goes in is what shows in the details panel (CPF_Edit) *or* what has
	 * a Get node in the graph (CPF_BlueprintVisible). Most have both flags, but
	 * not all, and the exceptions are precisely the ones that matter:
	 * `ACharacter::bIsCrouched` is BlueprintReadOnly without Edit -- it does not
	 * show in the panel and is read in the graph all the time. A panel-only
	 * filter would answer "not found" for it.
	 *
	 * Transient ones are left out, since they are not even saved to disk:
	 * `APawn::LastHitBy` is runtime state, not configuration, and in a sheet it
	 * would be noise that changes on its own.
	 */
	bool IsVisible(const FProperty* Property);

	// -- Node options --------------------------------------------------------

	/**
	 * true when the property is an adjustable option of a graph node.
	 *
	 * Two exclusions, and both hurt. `CPF_Edit` alone lets through what a
	 * struct keeps inside: `NodeGuid` is not editable, but the `A`, `B`, `C` and
	 * `D` fields of an `FGuid` are -- and the reading started writing
	 * `Print String (In String = "ok", A = 1033722443, B = ...)` on every node of
	 * the graph. And what `UEdGraphNode` declares is editor plumbing --
	 * position, comment, guid --, not configuration of what the node does.
	 */
	bool IsNodeSetting(const FProperty* Property);

	/**
	 * true when the struct is an `FAnimNode_*`.
	 *
	 * It is the only one that opens up to find options: `bLoopAnimation` and
	 * `PlayRate` are its fields, and the `UAnimGraphNode_*` just carries it.
	 * Opening any struct would turn each one into several loose arguments named
	 * after fields, without saying whose they were.
	 */
	bool IsAnimNodeStruct(const FStructProperty* Property);

	// -- Value ---------------------------------------------------------------

	/**
	 * Value -> text ready to go after the `=`, already quoted if needed.
	 *
	 * @return false when the value has no representation that comes back the
	 *         same. The caller warns; nobody emits a text that looks right and
	 *         comes back different.
	 */
	bool ValueToText(const FProperty* Property, const void* ValuePtr, FString& OutText);

	/**
	 * Text -> value, the exact mirror of ValueToText.
	 *
	 * @return false with the reason in OutError. The value is left untouched on error.
	 */
	bool TextToValue(const FProperty* Property, void* ValuePtr, const FString& Text, FString& OutError);

	/**
	 * true when the value differs from the archetype -- the question the sheet
	 * asks of every property, and the reason it fits on one screen.
	 */
	bool DiffersFromDefault(const FProperty* Property, const void* ValuePtr, const void* DefaultPtr);
}
