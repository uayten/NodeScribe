#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
struct FEdGraphPinType;

/**
 * Hand-written type name -> Unreal pin type.
 *
 * Accepts what shows up in the interface (`Float`, `Timer Handle`,
 * `EPlayerMappableKeySlot`, `Array of Name`), because that is what the user sees.
 *
 * It is the mirror of `NodeScribePropertyText::DescribePinType`: what comes out
 * there goes in here. It lives in the builder because it depends on the struct,
 * enum and class lookups by display name, which are the builder's -- but the
 * sheet needs it too, to create a variable from `variable Name : Type`.
 */
namespace NodeScribeTypeNames
{
	bool ResolvePinTypeFromName(const FString& InTypeName, FEdGraphPinType& OutType);

	/**
	 * Class by the name shown on screen, accepting the Blueprint `_C`.
	 * `BP_Rock` and `BP_Rock_C` are the same.
	 */
	UClass* FindClassByFriendlyName(const FString& Name);
}

/**
 * Turns statements into real nodes inside a UEdGraph.
 *
 * Rule that guides the whole file: when it cannot decide safely, it does not
 * decide. An unresolved node becomes a red comment in the graph; a pin that
 * depends on a choice of yours stays empty and blocks compiling. The acceptable
 * failure mode is the loud one -- never a guessed plausible node.
 */
class FNodeScribeBuilder
{
public:
	struct FResult
	{
		TArray<FNodeScribeDiagnostic> Diagnostics;
		TArray<UEdGraphNode*> CreatedNodes;

		int32 ErrorCount = 0;
		int32 WarningCount = 0;

		bool HasErrors() const { return ErrorCount > 0; }
	};

	/**
	 * @param Statements  parser output.
	 * @param Graph       target graph (may be a temporary graph for exporting).
	 * @param Blueprint   Blueprint that owns the graph, used to find its own variables and functions.
	 * @param Origin      top-left corner where drawing starts.
	 */
	static FResult Build(
		const TArray<FNodeScribeStatement>& Statements,
		UEdGraph* Graph,
		UBlueprint* Blueprint,
		const FVector2D& Origin);
};
