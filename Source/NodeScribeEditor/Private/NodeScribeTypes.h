#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNodeScribe, Log, All);

/** Severity of a message returned to the user after transcribing. */
enum class ENodeScribeSeverity : uint8
{
	/** It worked, but it is worth knowing (e.g. "picked PrintString from KismetSystemLibrary"). */
	Info,
	/** The node went in, but a decision of yours is missing (e.g. empty asset pin). */
	Warning,
	/** It could not be done. A red comment was left in the graph in its place. */
	Error
};

struct FNodeScribeDiagnostic
{
	ENodeScribeSeverity Severity = ENodeScribeSeverity::Info;

	/** Line of the input text (1-based). 0 = general message. */
	int32 Line = 0;

	FString Message;

	FNodeScribeDiagnostic() = default;

	FNodeScribeDiagnostic(ENodeScribeSeverity InSeverity, int32 InLine, FString InMessage)
		: Severity(InSeverity)
		, Line(InLine)
		, Message(MoveTemp(InMessage))
	{
	}
};

/** One argument between parentheses: `In String = "hello"` or `Target = $pc`. */
struct FNodeScribeArg
{
	/** Pin name. Empty = positional argument. */
	FString PinName;

	/** Literal value, or the referenced name when bIsReference. */
	FString Value;

	/** true when the value was written as `$something`. */
	bool bIsReference = false;
};

/** A parameter declared on an event line: `event Hit (Damage : Float)`. */
struct FNodeScribeEventParam
{
	FString Name;

	/** The type as the interface shows it, the same vocabulary as `variable`. */
	FString Type;
};

/**
 * One line of the script, already split into parts.
 * The structure is flat on purpose: nesting is rebuilt by the builder from
 * Indent, which avoids a recursive type and keeps the parser simple.
 */
struct FNodeScribeStatement
{
	/** Original line in the text (1-based), used to point at errors. */
	int32 LineNumber = 0;

	/** Indentation level (count of levels, not of spaces). */
	int32 Indent = 0;

	/** The line as the user wrote it, to reproduce in an error comment. */
	FString RawLine;

	/** true when the line is only an execution output label, e.g. `true:`. */
	bool bIsLabel = false;

	/** Label text, without the colon. */
	FString Label;

	/** true when the line declares a variable: `variable Health : Float = 100`. */
	bool bIsVariable = false;

	FString VariableName;
	FString VariableType;

	/**
	 * true when the line is `after <a line of the reading>:`: it names a node
	 * already in the graph, and the block under it is inserted after that node.
	 * NodeExpression, Args and OutputName then describe the node to find, not
	 * one to create.
	 */
	bool bIsAnchor = false;

	/**
	 * true when the line is `-> anchor N [through Pin]`: the chain goes on into a
	 * node that has its own line. LinkAnchor is N, LinkPin the input it enters
	 * through (empty = the main one).
	 */
	bool bIsExecLink = false;
	int32 LinkAnchor = 0;
	FString LinkPin;

	/** N of a node line ending in `# anchor N`; 0 when it has none. */
	int32 ExecAnchor = 0;

	/** Value after the `=`, if any. Empty = the type's default. */
	FString VariableDefault;

	/** Name given to the main output, from `pc = Get Player Controller`. */
	FString OutputName;

	/** What to create: `Get Player Controller`, `Branch`, `Cast to BP_Boss`... */
	FString NodeExpression;

	TArray<FNodeScribeArg> Args;

	/**
	 * On an `event` line, what the parentheses declare: the event's parameters.
	 * An event has no inputs to fill, so there they are never Args.
	 */
	TArray<FNodeScribeEventParam> EventParams;
};
