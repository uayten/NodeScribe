#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

/**
 * Turns the text pasted by the user into a flat list of statements.
 *
 * The parser is deliberately permissive: any line it does not understand
 * becomes a statement whose NodeExpression is the raw line, and the one who
 * complains is the builder -- which has the context to say *why* it failed,
 * and leaves a red comment in the graph. Failing silently here would be worse.
 */
class FNodeScribeParser
{
public:
	static TArray<FNodeScribeStatement> Parse(const FString& Text, TArray<FNodeScribeDiagnostic>& OutDiagnostics);

	/**
	 * Strips `#` (and `//`) up to the end of the line, respecting quotes.
	 *
	 * Public because what counts as a comment belongs to the format, not to the
	 * graph: the sheet and the blackboard read line by line without going
	 * through Parse, and if each decided on its own, a `#` inside quotes would be
	 * a value in one place and a comment in the other.
	 */
	static FString StripComment(const FString& Line);

private:
	/** Counts indentation in levels. Tab = 1 level, every 2 spaces = 1 level. */
	static int32 MeasureIndent(const FString& Line);

	/** Finds the opening `(` of the arguments, ignoring anything inside quotes. */
	static int32 FindArgsOpenParen(const FString& Line);

	/** Splits `A = 1, B = "x, y"` into parts, respecting quotes and parentheses. */
	static TArray<FString> SplitArgs(const FString& Inner);

	static FNodeScribeArg ParseArg(const FString& Raw);

	/** Strips single or double quotes from the ends, if present. */
	static FString Unquote(const FString& In);
};
