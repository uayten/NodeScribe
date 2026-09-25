#include "NodeScribeReader.h"

#include "NodeScribeAnimGraph.h"

#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "Animation/AnimationAsset.h"
#include "UObject/UnrealType.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GenericToText.h"
#include "K2Node_GetEnumeratorName.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

using namespace NodeScribePropertyText;

namespace
{
	/** Two spaces per level: it is what FNodeScribeParser::MeasureIndent counts. */
	const TCHAR* const IndentUnit = TEXT("  ");

	/**
	 * true for the node Unreal inserts by itself when linking different types.
	 *
	 * Nobody chooses these nodes: they show up in `TryCreateConnection` when the
	 * source pin is not of the target pin's type, and the builder recreates each
	 * of them the same way, by itself, when redoing the link. That is why they
	 * are walked through like a reroute instead of becoming a line.
	 *
	 * Two families, which is what `CreateAutomaticConversionNodeAndConnections`
	 * knows: the function marked `BlueprintAutocast` (`To Integer64 (Integer)`)
	 * and the specialised enum nodes (`Enum to String`).
	 */
	bool IsImplicitConversionNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		if (Node->IsA<UK2Node_GetEnumeratorName>() || Node->IsA<UK2Node_GetEnumeratorNameAsString>())
		{
			return true;
		}

		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			return Function && Function->HasMetaData(TEXT("BlueprintAutocast"));
		}

		return false;
	}

	/**
	 * A pin that carries flow: execution in the EventGraph, pose in the AnimGraph.
	 *
	 * It does not need to ask which graph it is in: pose pins only exist in an
	 * AnimGraph. What it separates is data flow -- and reading a pose as data
	 * would write `A = $idle` on a blend's line, which on pasting would create
	 * again a node that already came out in the tree.
	 */
	bool IsFlowPin(const UEdGraphPin* Pin)
	{
		return IsExecPin(Pin) || NodeScribeAnimGraph::IsPosePin(Pin);
	}

	/** The single data input pin of a conversion node. */
	UEdGraphPin* FindSingleDataInput(UEdGraphNode* Node)
	{
		UEdGraphPin* Found = nullptr;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsFlowPin(Pin) || Pin->bHidden)
			{
				continue;
			}

			if (Found)
			{
				// More than one input: it is not the conversion of a single value.
				return nullptr;
			}

			Found = Pin;
		}

		return Found;
	}

	/**
	 * Reroute nodes do not exist in the text format -- they are layout
	 * decoration. We walk through to the real pin instead of emitting a line the
	 * builder would not know how to recreate. The same goes for the implicit
	 * conversion node, which the builder puts back by itself.
	 *
	 * @param OutTraversed  receives the conversion nodes walked through, so that
	 *                      the orphan count does not accuse them of "feeding
	 *                      nothing" -- they do feed, they just did not become a line.
	 */
	UEdGraphPin* FollowToSourcePin(UEdGraphPin* InputPin, TSet<UEdGraphNode*>* OutTraversed = nullptr)
	{
		UEdGraphPin* Current = InputPin;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Source = Current->LinkedTo[0];
			if (!Source)
			{
				return nullptr;
			}

			UEdGraphNode* SourceNode = Source->GetOwningNode();

			if (const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(SourceNode))
			{
				Current = Knot->GetInputPin();
				continue;
			}

			if (IsImplicitConversionNode(SourceNode))
			{
				if (UEdGraphPin* Inner = FindSingleDataInput(SourceNode))
				{
					if (OutTraversed)
					{
						OutTraversed->Add(SourceNode);
					}
					Current = Inner;
					continue;
				}
			}

			return Source;
		}

		return nullptr;
	}

	/**
	 * Same, in the direction of execution -- and returning the PIN the wire
	 * lands on, not only the node that owns it.
	 *
	 * The node is not enough. An execution wire may arrive at a pin that is not
	 * the node's main input: the `Reset` of a Do Once, the `Stop` of a Timeline,
	 * the `Close` of a Gate. Those do not continue the chain -- they are a side
	 * command to a node that already lives elsewhere in the graph.
	 *
	 * While only the node was followed, both were the same thing to the reader,
	 * and it described the wrong graph with full confidence: two Do Onces that
	 * reset each other came out stacked, one under the other, as if one called
	 * the other.
	 */
	UEdGraphPin* FollowExecTargetPin(UEdGraphPin* ExecOutput)
	{
		UEdGraphPin* Current = ExecOutput;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Next = Current->LinkedTo[0];
			if (!Next)
			{
				return nullptr;
			}

			const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Next->GetOwningNode());
			if (!Knot)
			{
				return Next;
			}

			Current = Knot->GetOutputPin();
		}

		return nullptr;
	}

	/** The pose inputs linked to something, in the order they appear on the node. */
	TArray<UEdGraphPin*> GetLinkedPoseInputs(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Linked;
		for (UEdGraphPin* Pin : NodeScribeAnimGraph::GetPoseInputs(Node))
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				Linked.Add(Pin);
			}
		}
		return Linked;
	}

	TArray<UEdGraphPin*> GetExecOutputs(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Outputs;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) && Pin->Direction == EGPD_Output)
			{
				Outputs.Add(Pin);
			}
		}
		return Outputs;
	}

	UEdGraphPin* FindExecInput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) && Pin->Direction == EGPD_Input)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/**
	 * The execution input through which the chain really continues: the first.
	 *
	 * Any other one is a side command, and the node on the other side does not
	 * belong to this chain -- it has a life of its own elsewhere in the graph.
	 * Writing one under the other would say that one leads to the other.
	 */
	bool IsPrimaryExecInput(UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		return Node && Pin && FindExecInput(Node) == Pin;
	}

	/** The output a bare `$name` reaches. Same rule as the builder. */
	UEdGraphPin* FindPrimaryOutput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
			{
				return Pin;
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsFlowPin(Pin))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	/**
	 * true when a bare `$name`, without `.Pin`, reaches this pin without guessing.
	 *
	 * `FindPrimaryOutput` answers "the first output pin" when there is no
	 * `ReturnValue`, and that is a choice, not a fact: in a struct Break the pins
	 * only exist for the fields marked as visible, so the "first" one changes
	 * from node to node and changes again when someone ticks another field.
	 *
	 * Writing a bare `$x` in that case produced a text that, pasted back, linked
	 * to the first field of the *recreated* node -- which has every field
	 * visible, and so may be another field. A plausible, wrong link, silently.
	 *
	 * That is why the rule here is narrower than the builder's on purpose: only
	 * `ReturnValue`, or a single output. Anything else comes out named.
	 */
	bool IsUnambiguousPrimaryOutput(UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		if (!Node || !Pin)
		{
			return false;
		}

		if (Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
		{
			return true;
		}

		// A struct Break never counts as a single output, even when only one
		// field is visible -- that is exactly where the trap lives. A Break's
		// pins are the fields *ticked* in the node's panel, and the Break
		// recreated on pasting is born with all of them ticked: a bare `$x` would
		// come out of the hidden field and go into the struct's first field,
		// which is another one.
		if (Node->IsA<UK2Node_BreakStruct>())
		{
			return false;
		}

		int32 DataOutputs = 0;
		for (const UEdGraphPin* Other : Node->Pins)
		{
			if (Other->Direction == EGPD_Output && !IsFlowPin(Other) && !Other->bHidden)
			{
				++DataOutputs;
			}

			// A `ReturnValue` anywhere in the list is already the main output,
			// and this pin is not it.
			if (Other->Direction == EGPD_Output && Other->PinName == UEdGraphSchema_K2::PN_ReturnValue)
			{
				return false;
			}
		}

		return DataOutputs == 1;
	}

	/**
	 * true when the value needs to change type to go into the pin.
	 *
	 * An `int32` in an `int64` pin compiles and runs; the difference between
	 * having gone through that conversion or not is the difference between the
	 * right key of a map and a key that collides. Since the conversion node is
	 * walked through, without this mark the reading would be the same in both
	 * cases.
	 *
	 * A different category is enough. Object to object of another class does
	 * not count: passing a `PlayerController` into an `Actor` pin is
	 * inheritance, not conversion, and it would mark almost every line of the
	 * graph while saying nothing.
	 */
	bool NeedsConversionNote(const UEdGraphPin* Source, const UEdGraphPin* Target)
	{
		if (!Source || !Target)
		{
			return false;
		}

		const FEdGraphPinType& From = Source->PinType;
		const FEdGraphPinType& To = Target->PinType;

		// A wildcard is the pin that will still become the type of whatever links
		// to it (`Find` on a TMap, macro body). There is no conversion there.
		if (From.PinCategory == UEdGraphSchema_K2::PC_Wildcard
			|| To.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			return false;
		}

		if (From.PinCategory != To.PinCategory)
		{
			return true;
		}

		// Struct to struct of another type is also a conversion (Vector -> Vector2D).
		if (From.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			return From.PinSubCategoryObject != To.PinSubCategoryObject;
		}

		return false;
	}

	/** true when the node takes part in the flow -- white wire, or pose wire. */
	bool IsExecNode(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsFlowPin(Pin))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * A type name the builder can read back.
	 *
	 * `UEdGraphSchema_K2::TypeToText` returns what the interface shows in a
	 * details panel -- `IntProperty`, `Timer Handle Structure` --, and none of
	 * those come back: the builder expects `Integer` and `TimerHandle`. This
	 * function is the exact mirror of `ResolvePinTypeFromName`, so what comes
	 * out here goes in there.
	 *
	 * It lives in NodeScribePropertyText because the property sheet needs to
	 * write types with the same vocabulary -- two names for the same type would
	 * be two languages for the user to learn.
	 */
	using NodeScribePropertyText::DescribePinType;

	/**
	 * true when splitting the name into words will go wrong.
	 *
	 * `NameToDisplayString` decides where a space fits by looking at case
	 * changes, and an acronym breaks that rule: `JSL4UControllerInfo` comes out
	 * as `JSL4UController Info`, split in the wrong place. An acronym or a digit
	 * in the name is the sign that there is no way to know where the words
	 * start -- and in that case the raw name is better than a guess.
	 */
	bool HasAcronymOrDigit(const FString& Name)
	{
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			if (FChar::IsDigit(Name[Index]))
			{
				return true;
			}

			if (Index > 0 && FChar::IsUpper(Name[Index]) && FChar::IsUpper(Name[Index - 1]))
			{
				return true;
			}
		}

		return false;
	}

	/** The name the user sees and types, which is rarely the internal name. */
	FString GetWrittenPinName(const UEdGraphPin* Pin)
	{
		if (!Pin->PinFriendlyName.IsEmpty())
		{
			return Pin->PinFriendlyName.ToString();
		}

		// Without a display name, what is left is the C++ parameter name:
		// `InString`, `TargetMap`, `bIsChecked`. The screen shows `In String`,
		// `Target Map` and `Is Checked`, and that is how someone writes it when
		// checking the line. The builder's lookup normalises both forms to the
		// same thing, so the text comes back the same -- what changes is it
		// looking like the graph it describes.
		//
		// `bIsBool` drops the `b` from `bAimMode`, the same way the interface
		// does; the builder has the mirror case to find the pin again.
		const FString Name = Pin->PinName.ToString();
		if (HasAcronymOrDigit(Name))
		{
			return Name;
		}

		const bool bIsBool = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
		return FName::NameToDisplayString(Name, bIsBool);
	}

	/** Quotes: same rule for graph and for sheet, defined only once. */
	using NodeScribePropertyText::NeedsQuotes;
	using NodeScribePropertyText::TryQuote;

	/**
	 * true when the builder would resolve this name as a struct, not a function.
	 *
	 * `Break Vector` is the case: there is the function
	 * `UKismetMathLibrary::BreakVector` and there is the Break node of the
	 * FVector struct, with the same display name and different pins. The builder
	 * tries the struct form before the catalog, so writing the bare name would
	 * give back the other node.
	 */
	bool IsShadowedByStructForm(const FString& DisplayName, const UFunction* Function)
	{
		FString StructName;

		if (DisplayName.StartsWith(TEXT("Make "), ESearchCase::IgnoreCase))
		{
			StructName = DisplayName.RightChop(5);
		}
		else if (DisplayName.StartsWith(TEXT("Break "), ESearchCase::IgnoreCase))
		{
			StructName = DisplayName.RightChop(6);
		}
		else
		{
			return false;
		}

		const FString Normalized = FNodeScribeCatalog::Normalize(StructName);
		if (Normalized.IsEmpty())
		{
			return false;
		}

		const bool bWantsBreak = DisplayName.StartsWith(TEXT("Break "), ESearchCase::IgnoreCase);
		const TCHAR* const MetaKey = bWantsBreak ? TEXT("HasNativeBreak") : TEXT("HasNativeMake");

		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			if (FNodeScribeCatalog::Normalize(StructIt->GetName()) != Normalized
				&& FNodeScribeCatalog::Normalize(StructIt->GetDisplayNameText().ToString()) != Normalized)
			{
				continue;
			}

			// A shadow only exists when the two forms give different nodes.
			// `Vector` has a native break, and the builder, reading `Break
			// Vector`, creates exactly this function -- so the display name comes
			// back the same and is what reads best. Writing
			// `KismetMathLibrary.BreakVector` here would be dodging a collision
			// that no longer happens.
			if (Function && StructIt->HasMetaData(MetaKey)
				&& FindObject<UFunction>(nullptr, *StructIt->GetMetaData(MetaKey)) == Function)
			{
				return false;
			}

			return true;
		}

		return false;
	}

	/** Strips the verb from the expression before it becomes a name: `Get Health` -> `Health`. */
	FString MakeNameBase(const FString& Expression)
	{
		static const TCHAR* const Verbs[] = {
			TEXT("event "), TEXT("Get "), TEXT("Set "), TEXT("Cast to "),
			TEXT("Make "), TEXT("Break ")
		};

		FString Base = Expression;
		for (const TCHAR* Verb : Verbs)
		{
			if (Base.StartsWith(Verb, ESearchCase::IgnoreCase))
			{
				Base = Base.RightChop(FCString::Strlen(Verb));
				break;
			}
		}

		Base.RemoveFromEnd(TEXT("__DelegateSignature"));

		// `OnKeySelected of SelectInputKey` becomes `onKeySelected`: the
		// dispatcher's owner is already on the line, repeating it in the name
		// only adds noise.
		FString BeforeOwner;
		FString Ignored;
		if (Base.Split(TEXT(" of "), &BeforeOwner, &Ignored, ESearchCase::IgnoreCase))
		{
			Base = BeforeOwner;
		}

		return Base;
	}

	/** First letter lowercase, alphanumeric only: it becomes a readable `$name`. */
	FString ToIdentifier(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());

		for (const TCHAR C : In)
		{
			if (FChar::IsAlnum(C))
			{
				Out.AppendChar(C);
			}
		}

		if (Out.IsEmpty())
		{
			return TEXT("value");
		}

		if (FChar::IsDigit(Out[0]))
		{
			Out.InsertAt(0, TEXT('v'));
		}

		Out[0] = FChar::ToLower(Out[0]);
		return Out;
	}
}

// ---------------------------------------------------------------------------

/**
 * State of one reading. Walks the execution chains depth-first, in the same
 * order the builder would draw them, and keeps stacking lines.
 */
class FNodeScribeReadContext
{
public:
	FNodeScribeReadContext(const TArray<UEdGraphNode*>& InNodes, UBlueprint* InBlueprint, bool bInNested = false);

	void Run();

	FNodeScribeReader::FResult Result;

private:
	void EmitLine(int32 Indent, const FString& Text);

	void AddInfo(const FString& Message);
	void AddWarning(const FString& Message);

	/**
	 * Marks, before emitting any line, which execution nodes have a data output
	 * consumed by someone. Without this pre-pass the line would already have
	 * gone out without the `name =`, and the consumer further ahead would end
	 * up emitting the node again -- which, pasted back, would create two.
	 */
	void CollectConsumedNodes();

	void EmitExecChain(UEdGraphNode* Node, int32 Indent);

	/**
	 * An AnimGraph's pose tree, from the Output Pose backwards.
	 *
	 * Unlike execution, here the text goes from what feeds to what consumes:
	 * that is how the builder reads it back. A single input becomes a straight
	 * chain -- the lines follow each other --, and two or more become indented
	 * labels, one per input.
	 */
	void EmitPoseTree(UEdGraphNode* Node, int32 Indent);

	/** The whole anim graph: the Output Pose tree, and whatever was left loose. */
	void EmitAnimGraph();

	/** `Name = State Machine`, with the states and transitions under it. */
	void EmitStateMachine(UAnimGraphNode_StateMachineBase* Machine, int32 Indent);

	/** A transition's rule: the data chain that ends in the bool. */
	void EmitTransitionRule();

	/**
	 * A sub-graph read by a context of its own, with the lines indented.
	 *
	 * A context of its own for the same reason as the builder: each sub-graph
	 * has its own Output Pose and its own names, and mixing both would make one
	 * state's `$idle` point at a node of the other.
	 */
	void EmitSubGraph(UEdGraph* SubGraph, int32 Indent);

	/**
	 * An execution wire leaving `ExecOutput`: it continues the chain, goes back
	 * to an anchor, or enters sideways into a node that lives elsewhere in the
	 * graph.
	 */
	void EmitExecLink(UEdGraphPin* ExecOutput, int32 Indent);

	/** A node's anchor number, reserving one if it does not have one yet. */
	int32 AnchorFor(UEdGraphNode* Node);

	/** Writes `# anchor N` on the line of each anchored node, at the very end. */
	void StampAnchors();

	void EmitNodeLine(UEdGraphNode* Node, int32 Indent);

	/** Emits the line of a pure data node and returns the name given to it. */
	FString EmitDataNode(UEdGraphNode* Node, int32 Indent);

	/** The token that points at this pin: `$name` or `$name.Pin`. */
	FString MakeReferenceTo(UEdGraphPin* SourcePin, int32 Indent);

	/** Returns empty when the node comes back the same; otherwise, why it does not. */
	FString DescribeNode(UEdGraphNode* Node, FString& OutRoundTripIssue);
	FString BuildArgumentList(UEdGraphNode* Node, int32 Indent);

	/**
	 * The details panel options that differ from a freshly created node.
	 *
	 * They are arguments like any other on the line -- the builder recognises
	 * them by the same display name. They exist because not everything that
	 * changes what a node does is a pin: `Loop Animation` on a Sequence Player
	 * has no wire, is born on, and an `MM_Jump` that should play once kept
	 * looping with nothing in the text.
	 */
	void AppendNodeSettings(UEdGraphNode* Node, TArray<FString>& Args);

	/** ` (A = 1, B = 2)` with what differs from a new node, or empty. */
	FString DescribeSettings(UEdGraphNode* Node);

	FString DescribeLiteral(UEdGraphPin* Pin, bool& bOutRepresentable);

	/** An execution output label that comes back as the same pin when read. */
	FString DescribeExecLabel(UEdGraphPin* Pin);

	FString MakeUniqueName(const FString& Base);

	/** Short, stable name for the node's title, used in diagnostics. */
	FString ShortTitle(UEdGraphNode* Node) const;

	TArray<UEdGraphNode*> Nodes;
	UBlueprint* Blueprint = nullptr;

	TSet<UEdGraphNode*> Scope;
	TSet<UEdGraphNode*> EmittedExec;
	TSet<UEdGraphNode*> NeedsName;
	TMap<UEdGraphNode*, FString> DataNames;
	TSet<FString> UsedNames;

	/** On which line of the text each execution node came out, to anchor the way back. */
	TMap<UEdGraphNode*, int32> ExecLineIndex;

	/** Anchor number of each node that receives execution from more than one place. */
	TMap<UEdGraphNode*, int32> AnchorOf;
	int32 NextAnchor = 1;

	/** Implicit conversions that were walked through: they are not orphans. */
	TSet<UEdGraphNode*> TraversedConversions;

	TArray<FString> Lines;

	/**
	 * A sub-graph read from inside another reading: it comes out without a header.
	 *
	 * The header says where the text came from and declares the Blueprint's
	 * variables. Repeating that in the middle of a state's block helps nobody --
	 * and the declarations would come out indented, where the parser does not
	 * read them as declarations.
	 */
	bool bNested = false;

};

FNodeScribeReadContext::FNodeScribeReadContext(const TArray<UEdGraphNode*>& InNodes, UBlueprint* InBlueprint, bool bInNested)
	: Nodes(InNodes)
	, Blueprint(InBlueprint)
	, bNested(bInNested)
{
	for (UEdGraphNode* Node : Nodes)
	{
		// A reroute does not become a line: it is walked through when following wires.
		if (Node && !Node->IsA<UK2Node_Knot>())
		{
			Scope.Add(Node);
		}
	}
}

void FNodeScribeReadContext::EmitLine(int32 Indent, const FString& Text)
{
	FString Line;
	for (int32 Level = 0; Level < Indent; ++Level)
	{
		Line += IndentUnit;
	}
	Lines.Add(Line + Text);
}

void FNodeScribeReadContext::AddInfo(const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Info, 0, Message);
}

void FNodeScribeReadContext::AddWarning(const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Warning, 0, Message);
	++Result.WarningCount;
}

FString FNodeScribeReadContext::ShortTitle(UEdGraphNode* Node) const
{
	FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	Title.ReplaceInline(TEXT("\n"), TEXT(" "));
	Title.TrimStartAndEndInline();
	return Title;
}

FString FNodeScribeReadContext::MakeUniqueName(const FString& Base)
{
	FString Candidate = ToIdentifier(Base);

	int32 Suffix = 2;
	while (UsedNames.Contains(Candidate))
	{
		Candidate = ToIdentifier(Base) + FString::FromInt(Suffix++);
	}

	UsedNames.Add(Candidate);
	return Candidate;
}

// ---------------------------------------------------------------------------
// Node name
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::DescribeNode(UEdGraphNode* Node, FString& OutRoundTripIssue)
{
	OutRoundTripIssue.Reset();

	// Anim node. The instance's title does not work as a name: it changes with
	// the asset and with the pins. What comes back is the *menu* title, read
	// from the CDO -- which is exactly what the builder's index keeps.
	if (const UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
	{
		UAnimationAsset* Asset = AnimNode->GetAnimationAsset();

		// An asset name only comes back the same when the asset -> node mapping
		// leads back to this same class. `BS_Locomotion` becomes a BlendSpace
		// Player; on a BlendSpace *Evaluator* the same line would come back as a
		// Player, which is another node -- so there the asset name does not work.
		if (Asset && NodeScribeAnimGraph::NodeClassForAsset(Asset) == Node->GetClass())
		{
			// Short name only if it is enough to find this asset, and not another.
			// In a project that retargets there are two `MM_Jump`s -- Manny's and
			// the new one --, and the reader wrote the short name that the writer
			// refuses on the next line: "there is more than one animation called
			// MM_Jump". The round-trip text needs to paste back, and what knows
			// whether the name is enough is the same lookup the builder does.
			const NodeScribeAnimGraph::FAssetLookup Lookup =
				NodeScribeAnimGraph::FindAnimationAsset(Asset->GetName());

			return (Lookup.Asset == Asset) ? Asset->GetName() : Asset->GetPathName();
		}

		const UAnimGraphNode_Base* CDO = Cast<UAnimGraphNode_Base>(Node->GetClass()->GetDefaultObject(false));
		const FString MenuTitle = CDO ? CDO->GetNodeTitle(ENodeTitleType::MenuTitle).ToString() : FString();
		const FString Written = MenuTitle.IsEmpty()
			? Node->GetClass()->GetName().Replace(TEXT("AnimGraphNode_"), TEXT(""))
			: MenuTitle;

		if (Asset)
		{
			OutRoundTripIssue = FString::Printf(
				TEXT("`%s` plays `%s`, and the text does not carry that asset: the line comes back as the empty node, ")
				TEXT("for you to pick the animation again."),
				*Written, *Asset->GetName());
		}

		return Written;
	}

	// Event bound to another object's dispatcher: "On Key Selected
	// (SelectInputKey)" in the graph. Comes before Event because it derives from it.
	if (const UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
	{
		return FString::Printf(TEXT("event %s of %s"),
			*BoundEvent->DelegatePropertyName.ToString(),
			*BoundEvent->GetComponentPropertyName().ToString());
	}

	// CustomEvent before Event: the first derives from the second.
	if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
	{
		return TEXT("event ") + CustomEvent->CustomFunctionName.ToString();
	}

	if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
	{
		const FString EventName = FNodeScribeCatalog::StripEventPrefix(
			Event->EventReference.GetMemberName().ToString());

		// An override of a parent class event comes back the same. An event
		// bound to a dispatcher or a delegate does not: `event X` would recreate a
		// loose Custom Event, without the binding, and the node would look alike
		// and be dead.
		if (!Event->bOverrideFunction)
		{
			OutRoundTripIssue = FString::Printf(
				TEXT("`%s` is bound to a dispatcher/delegate. The format has no form for that binding yet: ")
				TEXT("pasting back would create a loose Custom Event that never fires. Recreate this node by hand."),
				*ShortTitle(Node));
		}

		return TEXT("event ") + EventName;
	}

	if (Node->IsA<UK2Node_IfThenElse>())
	{
		return TEXT("Branch");
	}

	if (Node->IsA<UK2Node_ExecutionSequence>())
	{
		return TEXT("Sequence");
	}

	if (Node->IsA<UK2Node_Select>())
	{
		return TEXT("Select");
	}

	if (Node->IsA<UK2Node_Self>())
	{
		return TEXT("Self");
	}

	if (const UK2Node_BaseMCDelegate* Delegate = Cast<UK2Node_BaseMCDelegate>(Node))
	{
		const FString DelegateName = Delegate->GetPropertyName().ToString();

		if (Node->IsA<UK2Node_CallDelegate>())   { return TEXT("Call ") + DelegateName; }
		if (Node->IsA<UK2Node_AddDelegate>())    { return TEXT("Bind ") + DelegateName; }
		if (Node->IsA<UK2Node_RemoveDelegate>()) { return TEXT("Unbind ") + DelegateName; }
		if (Node->IsA<UK2Node_ClearDelegate>())  { return TEXT("Clear ") + DelegateName; }

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` touches a dispatcher in a way the format does not have yet."), *ShortTitle(Node));

		return ShortTitle(Node);
	}

	if (const UK2Node_SwitchEnum* SwitchEnum = Cast<UK2Node_SwitchEnum>(Node))
	{
		if (const UEnum* Enum = SwitchEnum->GetEnum())
		{
			return TEXT("Switch on ") + Enum->GetName();
		}

		OutRoundTripIssue = TEXT("Enum switch without an enum set.");
		return TEXT("Switch on ?");
	}

	if (Node->IsA<UK2Node_SwitchInteger>())
	{
		return TEXT("Switch on Int");
	}

	if (Node->IsA<UK2Node_SwitchString>())
	{
		return TEXT("Switch on String");
	}

	if (Node->IsA<UK2Node_SwitchName>())
	{
		return TEXT("Switch on Name");
	}

	if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		if (UClass* TargetType = CastNode->TargetType)
		{
			FString ClassName = TargetType->GetName();
			ClassName.RemoveFromEnd(TEXT("_C"));
			return TEXT("Cast to ") + ClassName;
		}

		OutRoundTripIssue = TEXT("Cast without a target class set.");
		return TEXT("Cast to ?");
	}

	if (const UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node))
	{
		return TEXT("Set ") + Setter->VariableReference.GetMemberName().ToString();
	}

	if (const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
	{
		return TEXT("Get ") + Getter->VariableReference.GetMemberName().ToString();
	}

	if (Node->IsA<UK2Node_GetSubsystem>())
	{
		// `CustomClass` is protected on the node, but the output pin's type
		// carries the same information and is public.
		if (const UEdGraphPin* Output = FindPrimaryOutput(Node))
		{
			if (const UClass* SubsystemClass = Cast<UClass>(Output->PinType.PinSubCategoryObject.Get()))
			{
				return TEXT("Get ") + SubsystemClass->GetName();
			}
		}

		OutRoundTripIssue = TEXT("Subsystem node without a class set.");
		return TEXT("Get ?");
	}

	// MakeStruct before BreakStruct: there is no inheritance between them, but
	// both carry StructType and the order makes the reading obvious.
	if (const UK2Node_MakeStruct* MakeStruct = Cast<UK2Node_MakeStruct>(Node))
	{
		if (const UScriptStruct* Struct = MakeStruct->StructType)
		{
			return TEXT("Make ") + Struct->GetName();
		}

		OutRoundTripIssue = TEXT("Struct Make without a struct set.");
		return TEXT("Make ?");
	}

	if (const UK2Node_BreakStruct* BreakStruct = Cast<UK2Node_BreakStruct>(Node))
	{
		if (const UScriptStruct* Struct = BreakStruct->StructType)
		{
			return TEXT("Break ") + Struct->GetName();
		}

		OutRoundTripIssue = TEXT("Struct Break without a struct set.");
		return TEXT("Break ?");
	}

	// Input Action event. The class comes by name because its module is not a
	// dependency of this plugin -- Enhanced Input may not even be installed.
	if (Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
	{
		if (const FObjectProperty* ActionProperty =
			FindFProperty<FObjectProperty>(Node->GetClass(), TEXT("InputAction")))
		{
			if (const UObject* Action = ActionProperty->GetObjectPropertyValue_InContainer(Node))
			{
				// Full path: the short name only resolves if the asset is already
				// loaded, and a text pasted days later does not guarantee that.
				return TEXT("EnhancedInputAction ") + Action->GetPathName();
			}
		}

		OutRoundTripIssue = TEXT("Input Action node without an action set.");
		return TEXT("EnhancedInputAction ?");
	}

	if (Node->IsA<UK2Node_ConstructObjectFromClass>())
	{
		// The title changes with the chosen class ("Create WBP_X Widget"), so
		// it does not work as a name. The node's class is stable and says the
		// same thing; which class to build comes out in the `Class` argument.
		const FString NodeClassName = Node->GetClass()->GetName();

		if (NodeClassName == TEXT("K2Node_CreateWidget"))
		{
			return TEXT("Create Widget");
		}

		if (NodeClassName == TEXT("K2Node_SpawnActorFromClass"))
		{
			return TEXT("Spawn Actor from Class");
		}

		if (NodeClassName == TEXT("K2Node_ConstructObjectFromClass"))
		{
			return TEXT("Construct Object from Class");
		}

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` builds an object from a class, but the format only knows Create Widget, ")
			TEXT("Spawn Actor from Class and Construct Object from Class."),
			*ShortTitle(Node));

		return ShortTitle(Node);
	}

	// Async action node: `Wait For Any Controller Changes` and company.
	//
	// The title was already the right name -- what was missing was the reader
	// knowing that and stopping warning that the node does not come back. A
	// plugin exposing several `UBlueprintAsyncActionBase`s had all its graphs
	// marked as not pasteable because of this line.
	if (const UK2Node_BaseAsyncTask* AsyncNode = Cast<UK2Node_BaseAsyncTask>(Node))
	{
		if (UFunction* Factory = AsyncNode->GetFactoryFunction())
		{
			// A subclass with its own node (a GAS task, for example) does not come
			// back: the builder creates `UK2Node_AsyncAction`, which is another node.
			if (Node->GetClass() != UK2Node_AsyncAction::StaticClass())
			{
				OutRoundTripIssue = FString::Printf(
					TEXT("`%s` uses node `%s`, which has logic of its own. The format recreates async actions ")
					TEXT("as `UK2Node_AsyncAction`: pasting back would give a similar, different node. ")
					TEXT("Recreate this one by hand."),
					*ShortTitle(Node), *Node->GetClass()->GetName());
			}

			const FString DisplayName = Factory->GetDisplayNameText().ToString();

			UClass* SelfClass = Blueprint
				? (Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get())
				: nullptr;

			const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(DisplayName, SelfClass, nullptr);
			if (Lookup.Function == Factory)
			{
				return DisplayName;
			}

			// Ambiguous by display name: qualifying by the factory's class is the
			// same path as `Class.Function` for a regular call.
			if (const UClass* Owner = Factory->GetOwnerClass())
			{
				return Owner->GetName() + TEXT(".") + Factory->GetName();
			}

			return DisplayName;
		}

		OutRoundTripIssue = TEXT("Async action node without a factory function set.");
		return ShortTitle(Node);
	}

	// The `Return Node` of a function graph. The builder already knew how to
	// create it; it was the reader that did not know how to name it, and it came
	// out with an odd-node warning.
	if (Node->IsA<UK2Node_FunctionResult>())
	{
		return TEXT("Return");
	}

	// `To Text` of anything. It is a node of its own, and not the function of
	// the same name -- the function is `BlueprintInternalUseOnly` and does not
	// exist for the catalog, so the line fell into the fallback with a warning.
	if (Node->IsA<UK2Node_GenericToText>())
	{
		return TEXT("To Text");
	}

	// A function's entry is not recreated from text: it is born with the
	// function. What can be done is to state the signature, so whoever reads it
	// creates the right function and pastes the rest inside it.
	if (Node->IsA<UK2Node_FunctionEntry>())
	{
		TArray<FString> Parameters;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && !IsExecPin(Pin) && !Pin->bHidden)
			{
				Parameters.Add(FString::Printf(TEXT("%s : %s"),
					*GetWrittenPinName(Pin), *DescribePinType(Pin->PinType)));
			}
		}

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` is the function's entry, and it is born together with it -- no line creates it. ")
			TEXT("Create the function with the parameters (%s) and paste the rest inside it."),
			*ShortTitle(Node),
			Parameters.Num() > 0 ? *FString::Join(Parameters, TEXT(", ")) : TEXT("none"));

		return TEXT("Function Entry");
	}

	if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
	{
		if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
		{
			return MacroGraph->GetName();
		}

		OutRoundTripIssue = TEXT("Macro without a graph set.");
		return TEXT("Macro ?");
	}

	// Before CallFunction, which it descends from: the state machine getter
	// does not come back through the path of a call. The function it wraps is
	// `BlueprintInternalUseOnly` and so is not in the catalog, so the branch
	// below would fall into the qualified form (`AnimInstance.GetRelevantAnim...`),
	// which finds nothing on pasting. The display name comes back, because the
	// builder has a path of its own for it inside a transition rule.
	if (const UK2Node_AnimGetter* Getter = Cast<UK2Node_AnimGetter>(Node))
	{
		if (UFunction* Function = Getter->GetTargetFunction())
		{
			return Function->HasMetaData(TEXT("DisplayName"))
				? Function->GetMetaData(TEXT("DisplayName"))
				: FName::NameToDisplayString(Function->GetName(), false);
		}
	}

	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = Call->GetTargetFunction())
		{
			const FString DisplayName = Function->GetDisplayNameText().ToString();

			// The short name only works if it leads back to this same function.
			// `Apply Settings` exists in GameUserSettings and in
			// EnhancedInputUserSettings; writing the ambiguous name here would
			// throw away information only we have, for the builder to find out at
			// paste time that it cannot decide.
			UClass* SelfClass = Blueprint
				? (Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get())
				: nullptr;

			const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(DisplayName, SelfClass, nullptr);

			// A display name with parentheses does not come back, and they are
			// common: `SetText (Text)` is in every UI graph, `To Integer64
			// (Integer)` in every conversion. The parser cuts the line at the
			// first `(` to find the arguments, so `SetText (Text) (Target = $x)`
			// would come back as a node called `SetText` getting an argument called
			// `Text`. The qualified form has no parentheses and resolves to the
			// same function.
			const bool bNameBreaksParsing = DisplayName.Contains(TEXT("("))
				|| DisplayName.Contains(TEXT(")"));

			// The catalog finding the function is not enough: the builder tries
			// the special forms before it, and `Break Vector` would fall into the struct.
			if (Lookup.Function == Function
				&& !IsShadowedByStructForm(DisplayName, Function)
				&& !bNameBreaksParsing)
			{
				return DisplayName;
			}

			if (const UClass* Owner = Function->GetOwnerClass())
			{
				// `SKEL_WBP_X_C.FillList` does not come back: that class is a
				// compilation artifact and does not exist for the reader. A
				// function of the Blueprint itself comes out by its bare name,
				// which the builder resolves by looking at the target class.
				const FString OwnerName = Owner->GetName();
				const bool bIsCompilationArtifact =
					OwnerName.StartsWith(TEXT("SKEL_"))
					|| OwnerName.StartsWith(TEXT("REINST_"))
					|| OwnerName.StartsWith(TEXT("TRASHCLASS_"));

				if (!bIsCompilationArtifact && Owner != SelfClass)
				{
					return OwnerName + TEXT(".") + Function->GetName();
				}

				return Function->GetName();
			}

			return DisplayName;
		}
	}

	// What is left is something the builder cannot recreate from the name.
	const FString Title = ShortTitle(Node);
	OutRoundTripIssue = FString::Printf(
		TEXT("I do not know how to write `%s` in a way that comes back the same: the line came out with the node's title, ")
		TEXT("which the builder may not find."),
		*Title);

	return Title;
}

FString FNodeScribeReadContext::DescribeExecLabel(UEdGraphPin* Pin)
{
	// The label is compared with the pin's internal name after going through
	// the aliases. `then` accepts `True`, so we prefer the friendly name, which
	// is what shows on screen -- as long as it leads back to the same pin.
	//
	// Only on a Branch. `then` is also the name of the "continue from here" pin
	// of an async action node, and writing `True:` there invents a condition
	// that does not exist: the reader understands there is a test, and the
	// branch below starts to look like its `False`. The `then` label comes back
	// through the format's alias.
	const FString Friendly = GetWrittenPinName(Pin);
	const FString Internal = Pin->PinName.ToString();

	if (Pin->GetOwningNode() && Pin->GetOwningNode()->IsA<UK2Node_IfThenElse>())
	{
		static const TMap<FString, FString> KnownFriendly = {
			{ TEXT("then"), TEXT("True") },
			{ TEXT("else"), TEXT("False") }
		};

		if (const FString* Preferred = KnownFriendly.Find(FNodeScribeCatalog::Normalize(Internal)))
		{
			return *Preferred;
		}
	}

	return Friendly.IsEmpty() ? Internal : Friendly;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::DescribeLiteral(UEdGraphPin* Pin, bool& bOutRepresentable)
{
	bOutRepresentable = true;

	if (IsObjectLikePin(Pin))
	{
		if (const UObject* Asset = Pin->DefaultObject)
		{
			return Asset->GetPathName();
		}

		// An empty object pin is exactly the hole `?` declares.
		return TEXT("?");
	}

	FString Value = Pin->DefaultValue;
	if (Value.IsEmpty() && !Pin->DefaultTextValue.IsEmpty())
	{
		Value = Pin->DefaultTextValue.ToString();
	}

	if (Value.IsEmpty())
	{
		// Empty here is not "I cannot say": it is the value. The caller only
		// reaches this function when the pin differs from its factory default,
		// and in that case the value was erased on purpose. Returning nothing made
		// the line vanish, and the text came back with the default on the pin
		// again -- a different behaviour.
		return TEXT("\"\"");
	}

	// `100.000000` in the middle of a line hides the number in the noise. The
	// sheet already formatted it this way; the graph reader did not, and it was
	// the same decision applied to half of the plugin.
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && Value.IsNumeric())
	{
		Value = NodeScribePropertyText::FormatFloat(FCString::Atod(*Value));
	}

	if (!NeedsQuotes(Value))
	{
		return Value;
	}

	FString Quoted;
	if (!TryQuote(Value, Quoted))
	{
		bOutRepresentable = false;
		return FString();
	}

	return Quoted;
}

FString FNodeScribeReadContext::BuildArgumentList(UEdGraphNode* Node, int32 Indent)
{
	TArray<FString> Args;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input || IsFlowPin(Pin) || Pin->bHidden)
		{
			continue;
		}

		if (UEdGraphPin* SourcePin = FollowToSourcePin(Pin, &TraversedConversions))
		{
			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

			if (!Scope.Contains(SourceNode))
			{
				AddWarning(FString::Printf(
					TEXT("`%s` gets `%s` from a node left out of the selection. ")
					TEXT("That pin came out without a value -- select the whole graph or link it by hand."),
					*ShortTitle(Node), *GetWrittenPinName(Pin)));
				continue;
			}

			FString Reference = MakeReferenceTo(SourcePin, Indent);
			if (!Reference.IsEmpty())
			{
				// The conversion between the two types is done by a node Unreal
				// puts in by itself and that does not become a line. Without this
				// mark, a promoted `Device Id` (int32) and a `Connection Id`
				// (int64) write exactly the same line -- and one of them makes
				// every controller collide on the same map key.
				//
				// In parentheses after the reference: it is a reading note, and
				// the parser discards it when pasting back.
				if (NeedsConversionNote(SourcePin, Pin))
				{
					Reference += FString::Printf(TEXT(" (%s -> %s)"),
						*DescribePinType(SourcePin->PinType), *DescribePinType(Pin->PinType));
				}

				Args.Add(FString::Printf(TEXT("%s = %s"), *GetWrittenPinName(Pin), *Reference));
			}
			continue;
		}

		// An unlinked `self` pin is the implicit target: it is not written.
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			continue;
		}

		// A value identical to the pin's default adds nothing to the line.
		if (Pin->DefaultValue == Pin->AutogeneratedDefaultValue && !Pin->DefaultObject)
		{
			continue;
		}

		bool bRepresentable = true;
		const FString Literal = DescribeLiteral(Pin, bRepresentable);

		if (!bRepresentable)
		{
			AddWarning(FString::Printf(
				TEXT("The value of pin `%s` on `%s` has both kinds of quotes and does not fit on a line. ")
				TEXT("Copy that value by hand."),
				*GetWrittenPinName(Pin), *ShortTitle(Node)));
			continue;
		}

		if (!Literal.IsEmpty())
		{
			Args.Add(FString::Printf(TEXT("%s = %s"), *GetWrittenPinName(Pin), *Literal));
		}
	}

	AppendNodeSettings(Node, Args);

	if (Args.Num() == 0)
	{
		return FString();
	}

	return TEXT(" (") + FString::Join(Args, TEXT(", ")) + TEXT(")");
}

FString FNodeScribeReadContext::DescribeSettings(UEdGraphNode* Node)
{
	TArray<FString> Args;
	AppendNodeSettings(Node, Args);

	return Args.Num() > 0
		? TEXT(" (") + FString::Join(Args, TEXT(", ")) + TEXT(")")
		: FString();
}

void FNodeScribeReadContext::AppendNodeSettings(UEdGraphNode* Node, TArray<FString>& Args)
{
	// What the node adjusts outside the pins. `Loop Animation` of a Sequence
	// Player is the case that hurts: it is born on, and an `MM_Jump` that should
	// play once keeps looping -- with nothing in the text saying so, because
	// there is no pin.
	//
	// Only what differs from a freshly created node, as the sheet does: an asset
	// player has dozens of fields, and writing them all would drown the line.
	const UEdGraphNode* Defaults = Node->GetClass()->GetDefaultObject<UEdGraphNode>();
	if (!Defaults)
	{
		return;
	}

	auto Emit = [&](FProperty* Property, const void* ValuePtr, const void* DefaultPtr)
	{
		// The asset already is the line: `MM_Jump` *is* this node's `Sequence`.
		// Writing it again as an argument would give a line that repeats itself,
		// and on pasting back the pin does not even exist.
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Property))
		{
			if (AsObject->PropertyClass && AsObject->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
			{
				return;
			}
		}

		if (!DiffersFromDefault(Property, ValuePtr, DefaultPtr))
		{
			return;
		}

		FString Value;
		if (!ValueToText(Property, ValuePtr, Value))
		{
			AddWarning(FString::Printf(
				TEXT("Option `%s` of `%s` changed, and I do not know how to write its value. ")
				TEXT("Check the details panel."),
				*DisplayName(Property), *ShortTitle(Node)));
			return;
		}

		Args.Add(FString::Printf(TEXT("%s = %s"), *DisplayName(Property), *Value));
	};

	for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
	{
		FProperty* Property = *It;

		if (!IsNodeSetting(Property))
		{
			continue;
		}

		if (FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			if (IsAnimNodeStruct(AsStruct))
			{
				const void* StructPtr = AsStruct->ContainerPtrToValuePtr<void>(Node);
				const void* DefaultStructPtr = AsStruct->ContainerPtrToValuePtr<void>(Defaults);

				for (TFieldIterator<FProperty> Inner(AsStruct->Struct); Inner; ++Inner)
				{
					if (IsNodeSetting(*Inner))
					{
						Emit(*Inner, Inner->ContainerPtrToValuePtr<void>(StructPtr),
							Inner->ContainerPtrToValuePtr<void>(DefaultStructPtr));
					}
				}
				continue;
			}
		}

		Emit(Property, Property->ContainerPtrToValuePtr<void>(Node),
			Property->ContainerPtrToValuePtr<void>(Defaults));
	}
}

// ---------------------------------------------------------------------------
// Data nodes
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::EmitDataNode(UEdGraphNode* Node, int32 Indent)
{
	FString RoundTripIssue;
	const FString Expression = DescribeNode(Node, RoundTripIssue);

	if (!RoundTripIssue.IsEmpty())
	{
		AddWarning(RoundTripIssue);
	}

	// The arguments may emit more data lines, which need to come before this
	// one -- that is why the list is built before emitting.
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Name = MakeUniqueName(MakeNameBase(Expression));
	DataNames.Add(Node, Name);

	EmitLine(Indent, FString::Printf(TEXT("%s = %s%s"), *Name, *Expression, *Arguments));
	++Result.NodeCount;

	return Name;
}

FString FNodeScribeReadContext::MakeReferenceTo(UEdGraphPin* SourcePin, int32 Indent)
{
	UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

	FString Name;

	if (const FString* Existing = DataNames.Find(SourceNode))
	{
		Name = *Existing;
	}
	else if (const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(SourceNode))
	{
		// A Get of an own variable needs no line: a bare `$Health` already makes
		// the builder create the node. One line less per variable used.
		if (Getter->VariableReference.IsSelfContext())
		{
			Name = Getter->VariableReference.GetMemberName().ToString();
			DataNames.Add(SourceNode, Name);
			UsedNames.Add(Name);
		}
	}

	if (Name.IsEmpty())
	{
		// An execution node never becomes a data line: it has its own chain, and
		// emitting it here would create a second node on pasting. The pre-pass
		// should already have named it.
		if (IsExecNode(SourceNode))
		{
			AddWarning(FString::Printf(
				TEXT("`%s` takes part in execution and got no name. ")
				TEXT("That data link left the text -- relink it by hand."),
				*ShortTitle(SourceNode)));
			return FString();
		}

		Name = EmitDataNode(SourceNode, Indent);
	}

	FString Token = TEXT("$") + Name;

	// A node with more than one data output needs to say which: `$name.Pin`.
	if (!IsUnambiguousPrimaryOutput(SourceNode, SourcePin))
	{
		const FString PinName = GetWrittenPinName(SourcePin);

		if (PinName.IsEmpty())
		{
			// There are pins with neither a display name nor an internal name.
			// Saying so is the only honest path: writing a bare `$name` here would
			// look like the main output and link to another pin on the way back.
			AddWarning(FString::Printf(
				TEXT("`%s` feeds `%s` through a pin with no name. I wrote `<unknown pin>` in its place: ")
				TEXT("redo that link by hand."),
				*ShortTitle(SourceNode), *Name));

			return Token + TEXT(".<unknown pin>");
		}

		Token += TEXT(".") + PinName;
	}

	return Token;
}

// ---------------------------------------------------------------------------
// Execution chain
// ---------------------------------------------------------------------------

void FNodeScribeReadContext::EmitNodeLine(UEdGraphNode* Node, int32 Indent)
{
	FString RoundTripIssue;
	const FString Expression = DescribeNode(Node, RoundTripIssue);

	if (!RoundTripIssue.IsEmpty())
	{
		AddWarning(RoundTripIssue);
	}

	// The name was already decided in the pre-pass, when it became known that
	// someone consumes this node's output.
	const FString AssignedName = DataNames.FindRef(Node);

	// The arguments may emit data lines, which need to come before this one.
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Prefix = AssignedName.IsEmpty()
		? FString()
		: AssignedName + TEXT(" = ");

	// Kept before emitting: if another chain lands on this node further ahead,
	// this is the line the anchor will be written on.
	ExecLineIndex.Add(Node, Lines.Num());

	EmitLine(Indent, Prefix + Expression + Arguments);
	++Result.NodeCount;
}

void FNodeScribeReadContext::EmitExecChain(UEdGraphNode* Node, int32 Indent)
{
	if (!Node || !Scope.Contains(Node))
	{
		return;
	}

	if (EmittedExec.Contains(Node))
	{
		// Two chains landing on the same node. The format is a tree and cannot
		// say "go back to that one" -- but saying *where* it goes back to is what
		// separates "this branch was not linked" from "this branch continues up
		// there".
		//
		// Without this the branch came out with the label and nothing under it,
		// the way a truly empty branch comes out, and the two were
		// indistinguishable. The anchor goes as a comment on both ends: the
		// parser discards it, so the text stays pasteable exactly as it was.
		const int32 Anchor = AnchorFor(Node);

		EmitLine(Indent, FString::Printf(TEXT("# -> back to anchor %d (`%s`)"),
			Anchor, *ShortTitle(Node)));

		AddWarning(FString::Printf(
			TEXT("Execution goes back to `%s` (anchor %d), which already appeared before. ")
			TEXT("The text format only describes trees: this reconvergence is lost on pasting and you relink it by hand."),
			*ShortTitle(Node), Anchor));
		return;
	}

	EmittedExec.Add(Node);
	EmitNodeLine(Node, Indent);

	const TArray<UEdGraphPin*> ExecOutputs = GetExecOutputs(Node);

	if (ExecOutputs.Num() == 1)
	{
		EmitExecLink(ExecOutputs[0], Indent);
		return;
	}

	for (UEdGraphPin* Output : ExecOutputs)
	{
		if (!FollowExecTargetPin(Output))
		{
			continue;
		}

		EmitLine(Indent, DescribeExecLabel(Output) + TEXT(":"));
		EmitExecLink(Output, Indent + 1);
	}
}

void FNodeScribeReadContext::EmitExecLink(UEdGraphPin* ExecOutput, int32 Indent)
{
	UEdGraphPin* TargetPin = FollowExecTargetPin(ExecOutput);
	if (!TargetPin)
	{
		return;
	}

	UEdGraphNode* Target = TargetPin->GetOwningNode();
	if (!Target || !Scope.Contains(Target))
	{
		return;
	}

	if (IsPrimaryExecInput(Target, TargetPin))
	{
		EmitExecChain(Target, Indent);
		return;
	}

	// Side input: the `Reset` of a Do Once, the `Stop` of a Timeline, the
	// `Close` of a Gate. The node on the other side is not the continuation of
	// this chain -- it has its own line elsewhere in the text, and what this
	// wire does is send a command there. Walking into it would write a chain
	// that does not exist: that is how two Do Onces resetting each other came
	// out stacked, as if the first led to the second.
	//
	// The anchor is the same as the reconvergence one, and here it may point at
	// a line that has not come out yet -- that is why the number is reserved now
	// and written on the node at the very end.
	const int32 Anchor = AnchorFor(Target);
	const FString PinLabel = GetWrittenPinName(TargetPin);

	EmitLine(Indent, FString::Printf(TEXT("# -> enters `%s` through pin `%s` (anchor %d)"),
		*ShortTitle(Target), *PinLabel, Anchor));

	AddWarning(FString::Printf(
		TEXT("Execution enters `%s` (anchor %d) through pin `%s`, which is not its main input. ")
		TEXT("The text format only describes trees: this link is lost on pasting and you relink it by hand."),
		*ShortTitle(Target), Anchor, *PinLabel));
}

int32 FNodeScribeReadContext::AnchorFor(UEdGraphNode* Node)
{
	if (const int32* Existing = AnchorOf.Find(Node))
	{
		return *Existing;
	}

	const int32 Anchor = NextAnchor++;
	AnchorOf.Add(Node, Anchor);
	return Anchor;
}

void FNodeScribeReadContext::StampAnchors()
{
	// At the end, when every line already exists. A side input may point at a
	// node that only gets emitted later -- marking on the spot would lose those.
	for (const TTuple<UEdGraphNode*, int32>& Pair : AnchorOf)
	{
		if (const int32* LineIndex = ExecLineIndex.Find(Pair.Key))
		{
			Lines[*LineIndex] += FString::Printf(TEXT("  # anchor %d"), Pair.Value);
		}
	}
}

// ---------------------------------------------------------------------------

void FNodeScribeReadContext::CollectConsumedNodes()
{
	for (UEdGraphNode* Node : Scope)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsFlowPin(Pin))
			{
				continue;
			}

			UEdGraphPin* SourcePin = FollowToSourcePin(Pin, &TraversedConversions);
			if (!SourcePin)
			{
				continue;
			}

			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

			// An execution node consumed as data gets a name NOW, before any line
			// comes out. Assigning it at emission time was not enough: an event
			// referenced by a chain earlier than its own would not have a name
			// yet, and ended up emitted twice -- once as data, once as a root.
			// Pasting that text back gave "the event already exists".
			if (Scope.Contains(SourceNode) && IsExecNode(SourceNode) && !DataNames.Contains(SourceNode))
			{
				NeedsName.Add(SourceNode);

				FString RoundTripIssue;
				const FString Expression = DescribeNode(SourceNode, RoundTripIssue);
				DataNames.Add(SourceNode, MakeUniqueName(MakeNameBase(Expression)));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// AnimGraph
// ---------------------------------------------------------------------------

void FNodeScribeReadContext::EmitPoseTree(UEdGraphNode* Node, int32 Indent)
{
	if (!Node || !Scope.Contains(Node))
	{
		return;
	}

	if (EmittedExec.Contains(Node))
	{
		// The same pose feeding two places. The format is a tree and cannot say
		// "that one again" -- so it says *which*, the same way as execution
		// reconvergence, and warns that this part is lost on pasting.
		const int32 Anchor = AnchorFor(Node);

		EmitLine(Indent, FString::Printf(TEXT("# -> same pose as `%s` (anchor %d)"),
			*ShortTitle(Node), Anchor));

		AddWarning(FString::Printf(
			TEXT("The pose of `%s` (anchor %d) feeds more than one place. The format only describes trees: ")
			TEXT("this second link is lost on pasting and you relink it by hand."),
			*ShortTitle(Node), Anchor));
		return;
	}

	EmittedExec.Add(Node);

	if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
	{
		EmitStateMachine(Machine, Indent);
		return;
	}

	const TArray<UEdGraphPin*> Inputs = GetLinkedPoseInputs(Node);

	// A single input is a straight chain: what feeds comes before, in the same
	// column. It is the form that reads best, and the one the builder assembles
	// without a label.
	if (Inputs.Num() == 1)
	{
		if (UEdGraphPin* Source = FollowToSourcePin(Inputs[0]))
		{
			EmitPoseTree(Source->GetOwningNode(), Indent);
		}

		EmitNodeLine(Node, Indent);
		return;
	}

	// Two or more: the node comes first and each input opens a label. It is the
	// reverse of the EventGraph, where the label opens an *output* -- here the
	// tree is about what feeds.
	EmitNodeLine(Node, Indent);

	for (UEdGraphPin* Pin : Inputs)
	{
		EmitLine(Indent, GetWrittenPinName(Pin) + TEXT(":"));

		if (UEdGraphPin* Source = FollowToSourcePin(Pin))
		{
			EmitPoseTree(Source->GetOwningNode(), Indent + 1);
		}
	}
}

void FNodeScribeReadContext::EmitAnimGraph()
{
	UEdGraph* SourceGraph = Nodes.Num() > 0 && Nodes[0] ? Nodes[0]->GetGraph() : nullptr;

	if (UEdGraphNode* Root = SourceGraph ? NodeScribeAnimGraph::FindOutputPose(SourceGraph) : nullptr)
	{
		// The Output Pose does not become a line: it already exists in the target
		// graph, and the text's last line links into it by itself. Marking it as
		// emitted is what takes it out of the count of nodes the reading lost.
		EmittedExec.Add(Root);

		if (UEdGraphPin* Pose = NodeScribeAnimGraph::FindPoseInput(Root))
		{
			if (UEdGraphPin* Source = FollowToSourcePin(Pose))
			{
				EmitPoseTree(Source->GetOwningNode(), 0);
			}
		}
	}

	// A branch that does not reach the Output Pose. It comes out anyway:
	// omitting it would be losing a node without saying so, which is the only
	// result worse than an incomplete text.
	//
	// Only the tip of each branch -- whatever feeds something inside the scope
	// comes out by recursion, together with its owner.
	for (UEdGraphNode* Node : Scope)
	{
		if (EmittedExec.Contains(Node) || !Node->IsA<UAnimGraphNode_Base>())
		{
			continue;
		}

		bool bFeedsSomeone = false;
		for (UEdGraphPin* Output : NodeScribeAnimGraph::GetPoseOutputs(Node))
		{
			for (UEdGraphPin* Linked : Output->LinkedTo)
			{
				if (Linked && Scope.Contains(Linked->GetOwningNodeUnchecked()))
				{
					bFeedsSomeone = true;
					break;
				}
			}
		}

		if (bFeedsSomeone)
		{
			continue;
		}

		Lines.Add(FString());
		EmitPoseTree(Node, 0);

		AddWarning(FString::Printf(
			TEXT("`%s` does not reach the Output Pose. It came out in the text, but loose, as it is in the graph."),
			*ShortTitle(Node)));
	}
}

void FNodeScribeReadContext::EmitStateMachine(UAnimGraphNode_StateMachineBase* Machine, int32 Indent)
{
	UAnimationStateMachineGraph* MachineGraph = Machine->EditorStateMachineGraph;

	// A state machine's name is its sub-graph's, and the builder reads it back
	// from the `name =`.
	const FString Name = MachineGraph ? MachineGraph->GetName() : FString();

	EmitLine(Indent, Name.IsEmpty()
		? FString(TEXT("State Machine"))
		: FString::Printf(TEXT("%s = State Machine"), *Name));

	++Result.NodeCount;

	if (!MachineGraph)
	{
		AddWarning(TEXT("This state machine has no sub-graph; its states did not come out."));
		return;
	}

	// The entry state first, because that is how the text declares it: the
	// builder links the Entry to the first `state` that appears. Coming out of
	// order would change where the machine starts -- and that would only show
	// up at runtime.
	// State and conduit in the same list. Both are nodes a transition leaves
	// from and arrives at, and reading only the state left every transition
	// going through a conduit talking about a name the text never declared --
	// the `To Falling -> Jump` of Epic's ABP_Unarmed is exactly that. On pasting,
	// that became a "state that does not exist" error, with the list of the ones
	// that do, and the machine came out missing half its transitions.
	TArray<UAnimStateNodeBase*> States;

	auto IsStateOrConduit = [](UEdGraphNode* Node) -> UAnimStateNodeBase*
	{
		// `UAnimStateNodeBase` is also the parent of the transition and of the
		// Entry. A direct Cast to it would bring both into the list of states.
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			return State;
		}
		if (UAnimStateConduitNode* Conduit = Cast<UAnimStateConduitNode>(Node))
		{
			return Conduit;
		}
		return Cast<UAnimStateAliasNode>(Node);
	};

	if (MachineGraph->EntryNode)
	{
		if (UEdGraphPin* EntryPin = MachineGraph->EntryNode->GetOutputPin())
		{
			for (UEdGraphPin* Linked : EntryPin->LinkedTo)
			{
				if (UAnimStateNodeBase* Entry = IsStateOrConduit(Linked ? Linked->GetOwningNodeUnchecked() : nullptr))
				{
					States.AddUnique(Entry);
				}
			}
		}
	}

	if (States.Num() == 0)
	{
		AddWarning(TEXT("This state machine has no linked entry state; ")
			TEXT("on pasting, the text's first state becomes the entry."));
	}

	for (UEdGraphNode* Node : MachineGraph->Nodes)
	{
		if (UAnimStateNodeBase* State = IsStateOrConduit(Node))
		{
			States.AddUnique(State);
		}
	}

	for (UAnimStateNodeBase* State : States)
	{
		// The alias has no sub-graph: it is a nickname for a handful of states,
		// and it exists for a transition to leave all of them at once without
		// repeating the rule. Its block is the list of those states.
		if (UAnimStateAliasNode* Alias = Cast<UAnimStateAliasNode>(State))
		{
			EmitLine(Indent + 1, FString::Printf(TEXT("alias %s%s:"),
				*Alias->GetStateName(), *DescribeSettings(Alias)));

			// Sorted by name: a TSet has no stable order, and without this two
			// readings of the same graph would give different texts -- enough for
			// a round-trip comparison to flag a difference that does not exist.
			TArray<FString> Aliased;
			for (const TWeakObjectPtr<UAnimStateNodeBase>& Target : Alias->GetAliasedStates())
			{
				if (const UAnimStateNodeBase* Node = Target.Get())
				{
					Aliased.Add(Node->GetStateName());
				}
			}
			Aliased.Sort();

			for (const FString& AliasedName : Aliased)
			{
				EmitLine(Indent + 2, AliasedName);
			}

			continue;
		}

		const TCHAR* Keyword = State->IsA<UAnimStateConduitNode>() ? TEXT("conduit") : TEXT("state");

		EmitLine(Indent + 1, FString::Printf(TEXT("%s %s%s:"),
			Keyword, *State->GetStateName(), *DescribeSettings(State)));

		// Through the virtual: `BoundGraph` is a field of each subclass, not of the base.
		EmitSubGraph(State->GetBoundGraph(), Indent + 2);
	}

	for (UEdGraphNode* Node : MachineGraph->Nodes)
	{
		UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
		if (!Transition)
		{
			continue;
		}

		UAnimStateNodeBase* From = Transition->GetPreviousState();
		UAnimStateNodeBase* To = Transition->GetNextState();

		if (!From || !To)
		{
			AddWarning(TEXT("There is a transition without a source or a target in this machine; it did not come out in the text."));
			continue;
		}

		// The transition's options go out on the line, and the one that matters
		// most is `Automatic Rule Based on Sequence Player in State`. With it on,
		// the transition fires when the source state's animation ends, and that
		// is why it is born with no rule at all -- which looks identical to a
		// transition nobody finished writing. Without this line, Epic's
		// `Land -> Locomotion` came back as an empty transition, and pasting that
		// gave a dead transition with an Engine warning.
		EmitLine(Indent + 1, FString::Printf(TEXT("%s -> %s%s:"),
			*From->GetStateName(), *To->GetStateName(), *DescribeSettings(Transition)));

		EmitSubGraph(Transition->BoundGraph, Indent + 2);
	}
}

void FNodeScribeReadContext::EmitTransitionRule()
{
	UAnimGraphNode_TransitionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : Scope)
	{
		if (UAnimGraphNode_TransitionResult* Found = Cast<UAnimGraphNode_TransitionResult>(Node))
		{
			ResultNode = Found;
			break;
		}
	}

	if (!ResultNode)
	{
		return;
	}

	// The Result does not become a line for the same reason as the Output
	// Pose: it is born with the graph, and the builder links the block's end
	// into it by itself.
	EmittedExec.Add(ResultNode);

	UEdGraphPin* CanEnter = ResultNode->FindPin(TEXT("bCanEnterTransition"));
	UEdGraphPin* Source = CanEnter ? FollowToSourcePin(CanEnter) : nullptr;

	if (!Source)
	{
		// Nothing linked: the rule is the pin's factory value. An empty block is
		// the honest reading of that -- inventing a line here would be writing a
		// condition the graph does not have.
		return;
	}

	EmitDataNode(Source->GetOwningNode(), 0);
}

void FNodeScribeReadContext::EmitSubGraph(UEdGraph* SubGraph, int32 Indent)
{
	if (!SubGraph)
	{
		return;
	}

	FNodeScribeReadContext Nested(SubGraph->Nodes, Blueprint, true);
	Nested.Run();

	for (const FString& Line : Nested.Lines)
	{
		// A blank line separates chains at the top level; inside an indented
		// block it only opens a gap.
		if (!Line.IsEmpty())
		{
			EmitLine(Indent, Line);
		}
	}

	Result.Diagnostics.Append(Nested.Result.Diagnostics);
	Result.NodeCount += Nested.Result.NodeCount;
	Result.ErrorCount += Nested.Result.ErrorCount;
	Result.WarningCount += Nested.Result.WarningCount;
	Result.LostNodeCount += Nested.Result.LostNodeCount;
}

void FNodeScribeReadContext::Run()
{
	if (Scope.Num() == 0)
	{
		AddInfo(TEXT("Nothing to read: no node selected."));
		return;
	}

	CollectConsumedNodes();

	// Header: whoever reads this text in a chat has no way to know where it
	// came from, and "which asset is this" is the first question. It comes out
	// as a comment, so the parser ignores it when the text comes back.
	if (UEdGraph* SourceGraph = !bNested && Nodes.Num() > 0 && Nodes[0] ? Nodes[0]->GetGraph() : nullptr)
	{
		FString Header = FString::Printf(TEXT("# %s -> %s"),
			Blueprint ? *Blueprint->GetName() : TEXT("?"),
			*SourceGraph->GetName());

		// Without this, an excerpt looks like a whole graph with missing nodes.
		if (Nodes.Num() < SourceGraph->Nodes.Num())
		{
			Header += TEXT(" (partial selection)");
		}

		Lines.Add(Header);

		// The exact way back, ready to copy.
		//
		// The graph's name cannot be guessed (`EventGraph` in one asset,
		// `Gameplay Ability Graph` in another), and neither can the content root:
		// a plugin asset lives in `/PluginName/`, not in `/Game/`. Getting either
		// one wrong gives the same terse invalid-object message, which comes from
		// the Engine before this plugin runs -- so the only way not to spend turns
		// guessing is for the right path to be here.
		if (const UObject* Outer = SourceGraph->GetOuter())
		{
			Lines.Add(FString::Printf(TEXT("# refPath: %s:%s"),
				*Outer->GetPathName(), *SourceGraph->GetName()));
		}

		// The Blueprint's variables along with it. Without them, whoever reads the
		// text has no way to know whether `$Slot` exists, what its type is, or
		// what others there are -- and ends up writing text that references
		// things that do not exist.
		UClass* OwnClass = nullptr;
		if (Blueprint)
		{
			// The skeleton has the variables created without compiling yet.
			if (Blueprint->SkeletonGeneratedClass)
			{
				OwnClass = Blueprint->SkeletonGeneratedClass;
			}
			else
			{
				OwnClass = Blueprint->GeneratedClass.Get();
			}
		}

		if (OwnClass)
		{
			// A Designer widget is not declared: it comes out as a comment, so the
			// reader knows it exists without the text trying to recreate it on the
			// way back.
			const UClass* WidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.Widget"));
			const UClass* UserWidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.UserWidget"));

			const bool bIsWidgetBlueprint = UserWidgetClass
				&& Blueprint->ParentClass
				&& Blueprint->ParentClass->IsChildOf(UserWidgetClass);

			TArray<FString> Declarations;
			TArray<FString> DesignerWidgets;
			TArray<FString> Components;

			// A component added in the Components panel is also a property of the
			// generated class, and it used to come out as `variable Mesh :
			// StaticMeshComponent`. Pasted into another Blueprint, that line
			// creates a plain variable of that type -- it compiles and never
			// points at any component, the same trap as a Designer widget. So it
			// comes out the same way: as a comment, for the reader to know.
			TSet<FName> ComponentVariables;
			if (const USimpleConstructionScript* SCS = Blueprint ? Blueprint->SimpleConstructionScript : nullptr)
			{
				for (const USCS_Node* Node : SCS->GetAllNodes())
				{
					if (Node)
					{
						ComponentVariables.Add(Node->GetVariableName());
					}
				}
			}

			// ExcludeSuper: only what this Blueprint declares. With the
			// inheritance it would be hundreds of Engine lines, none useful here.
			for (TFieldIterator<FProperty> PropertyIt(OwnClass, EFieldIteratorFlags::ExcludeSuper);
				PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
				{
					continue;
				}

				// GetDefault, not construction on the stack: a UObject class cannot
				// be instantiated that way -- the constructor calls
				// FObjectInitializer::Get(), which is only valid inside a UObject
				// constructor, and brings the editor down on the spot.
				FEdGraphPinType PinType;
				if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
				{
					continue;
				}

				const FString TypeName = DescribePinType(PinType);
				if (TypeName.IsEmpty())
				{
					continue;
				}

				bool bIsDesignerWidget = false;
				if (bIsWidgetBlueprint && WidgetClass)
				{
					if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
					{
						bIsDesignerWidget = ObjectProperty->PropertyClass
							&& ObjectProperty->PropertyClass->IsChildOf(WidgetClass);
					}
				}

				if (bIsDesignerWidget)
				{
					DesignerWidgets.Add(FString::Printf(TEXT("#   %s : %s"), *Property->GetName(), *TypeName));
				}
				else if (ComponentVariables.Contains(Property->GetFName()))
				{
					Components.Add(FString::Printf(TEXT("#   %s : %s"), *Property->GetName(), *TypeName));
				}
				else
				{
					// The default value lives in the compiled class's CDO -- that is
					// where the details panel reads it from. `NewVariables[].DefaultValue`
					// is only a seed at creation and is empty afterwards.
					//
					// Without it, pasting into an empty Blueprint creates everything
					// zeroed and the graph behaves differently without any warning.
					FString DefaultValue;
					if (UClass* CompiledClass = Blueprint->GeneratedClass.Get())
					{
						if (const FProperty* Compiled = CompiledClass->FindPropertyByName(Property->GetFName()))
						{
							if (UObject* DefaultObject = CompiledClass->GetDefaultObject())
							{
								const void* Value = Compiled->ContainerPtrToValuePtr<void>(DefaultObject);

								// The type's zero adds nothing to the line, and
								// would write `= 0` on every untouched variable.
								void* Zero = FMemory::Malloc(Compiled->GetSize(), Compiled->GetMinAlignment());
								Compiled->InitializeValue(Zero);
								const bool bIsZero = Compiled->Identical(Value, Zero);
								Compiled->DestroyValue(Zero);
								FMemory::Free(Zero);

								if (!bIsZero)
								{
									Compiled->ExportTextItem_Direct(DefaultValue, Value, nullptr, nullptr, PPF_None);
								}
							}
						}
					}

					FString Line = FString::Printf(TEXT("variable %s : %s"), *Property->GetName(), *TypeName);

					if (!DefaultValue.IsEmpty())
					{
						FString Quoted;
						if (!NeedsQuotes(DefaultValue))
						{
							Line += TEXT(" = ") + DefaultValue;
						}
						else if (TryQuote(DefaultValue, Quoted))
						{
							Line += TEXT(" = ") + Quoted;
						}
					}

					Declarations.Add(Line);
				}
			}

			if (Components.Num() > 0)
			{
				Lines.Add(TEXT("# components (add them in the Components panel):"));
				Lines.Append(Components);
			}

			if (DesignerWidgets.Num() > 0)
			{
				Lines.Add(TEXT("# from the Designer (create them there, ticking Is Variable):"));
				Lines.Append(DesignerWidgets);
			}

			Lines.Append(Declarations);
		}

		Lines.Add(FString());
	}

	// Comment boxes first: they are context, not an execution step.
	TArray<UEdGraphNode*> Comments;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node && Node->IsA<UEdGraphNode_Comment>() && Scope.Contains(Node))
		{
			Comments.Add(Node);
		}
	}

	Comments.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return A.NodePosY != B.NodePosY ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});

	for (UEdGraphNode* Comment : Comments)
	{
		Scope.Remove(Comment);

		FString Text = Comment->NodeComment;
		Text.ReplaceInline(TEXT("\n"), TEXT(" "));
		Text.TrimStartAndEndInline();

		if (!Text.IsEmpty())
		{
			EmitLine(0, TEXT("Comment ") + Text);
		}
	}

	if (Comments.Num() > 0 && Scope.Num() > 0)
	{
		Lines.Add(FString());
	}

	// An animation graph has no execution root: it has a tree converging on the
	// Output Pose, and it reads backwards -- from what feeds to what consumes. A
	// transition rule does not even have that: it is a data chain that ends in
	// a bool.
	UEdGraph* CurrentGraph = Nodes.Num() > 0 && Nodes[0] ? Nodes[0]->GetGraph() : nullptr;

	// The transition rule does not go through IsAnimationGraph. Its schema is
	// UAnimationTransitionSchema, which descends from UEdGraphSchema_K2 and not
	// from UAnimationGraphSchema -- consistent, because the rule is a data chain
	// ending in a bool, with no pose pin at all. Asking only the schema left the
	// rule out of the text and still counted its nodes as orphans: a transition
	// that was read always came back empty, even with the rule linked in the graph.
	const bool bTransitionRule = CurrentGraph && CurrentGraph->IsA<UAnimationTransitionGraph>();
	const bool bAnimPath = bTransitionRule || NodeScribeAnimGraph::IsAnimationGraph(CurrentGraph);

	if (bTransitionRule)
	{
		EmitTransitionRule();
	}
	else if (bAnimPath)
	{
		EmitAnimGraph();
	}

	// Roots: whatever has a white wire but receives execution from nobody.
	// Events land here naturally, because they have no execution input pin.
	TArray<UEdGraphNode*> Roots;
	for (UEdGraphNode* Node : Scope)
	{
		// On the anim path the lines already came out; the rest here only exists
		// for the orphan count, which applies on both paths.
		if (bAnimPath || !IsExecNode(Node))
		{
			continue;
		}

		// A root is whatever receives no execution from inside the selection.
		//
		// Looking only at "is the pin linked?" broke Copy Selected: in a
		// selection in the middle of the chain, the first node receives execution
		// from outside it, so no node was a root and the text came out empty.
		UEdGraphPin* ExecInput = FindExecInput(Node);
		if (!ExecInput)
		{
			Roots.Add(Node);
			continue;
		}

		UEdGraphPin* ExecSource = FollowToSourcePin(ExecInput);
		if (!ExecSource || !Scope.Contains(ExecSource->GetOwningNode()))
		{
			Roots.Add(Node);
		}
	}

	// An event with nothing linked says nothing, and pasting the text back would
	// run into the duplicate event guard. The disabled stubs every new Blueprint
	// brings -- BeginPlay, Tick, ActorBeginOverlap -- land exactly here.
	//
	// They are recorded because the orphan count, further down, catches
	// everything that did not become a line: without this they showed up as "3
	// data nodes feed nothing" -- which describes them wrong (they are not data)
	// and counts as a loss what was left out on purpose. An empty event carries
	// no behaviour at all.
	TSet<UEdGraphNode*> EmptyEvents;

	Roots.RemoveAll([this, &EmptyEvents](UEdGraphNode* Node)
	{
		if (!Node->IsA<UK2Node_Event>() || NeedsName.Contains(Node))
		{
			return false;
		}

		for (UEdGraphPin* Pin : GetExecOutputs(Node))
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				return false;
			}
		}

		EmptyEvents.Add(Node);
		return true;
	});

	// Whatever is referenced by another chain goes first: `$land` only exists
	// after the line that names that event, so the order in the text has to
	// respect the dependency -- otherwise the text does not come back.
	//
	// After that, events before the rest and top to bottom, which is the
	// reading order of someone looking at the graph.
	Roots.Sort([this](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		const bool bAReferenced = NeedsName.Contains(const_cast<UEdGraphNode*>(&A));
		const bool bBReferenced = NeedsName.Contains(const_cast<UEdGraphNode*>(&B));

		if (bAReferenced != bBReferenced)
		{
			return bAReferenced;
		}

		const bool bAEvent = A.IsA<UK2Node_Event>();
		const bool bBEvent = B.IsA<UK2Node_Event>();

		if (bAEvent != bBEvent)
		{
			return bAEvent;
		}

		return A.NodePosY != B.NodePosY ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});

	for (int32 Index = 0; Index < Roots.Num(); ++Index)
	{
		if (Index > 0)
		{
			Lines.Add(FString());
		}
		EmitExecChain(Roots[Index], 0);
	}

	// Data nodes nobody consumes enter no chain. Saying *which* is what turns
	// the note into a diagnostic: seven widget getters linked to a `Target` pin,
	// which accepts a single link, leave six loose -- and the list of names
	// shows that at once, without a round trip to find out who they were.
	TArray<FString> Orphans;
	for (UEdGraphNode* Node : Scope)
	{
		if (EmittedExec.Contains(Node) || DataNames.Contains(Node))
		{
			continue;
		}

		// A walked-through conversion does feed -- it just did not become a line.
		if (TraversedConversions.Contains(Node))
		{
			continue;
		}

		// An empty event was left out on purpose, and there is nothing to lose in it.
		if (EmptyEvents.Contains(Node))
		{
			continue;
		}

		Orphans.Add(ShortTitle(Node));
	}

	// Counted apart from the note: whoever wants to erase the graph and paste it
	// back needs to know these do not come back, and a note cannot be checked
	// by code.
	Result.LostNodeCount = Orphans.Num();

	if (Orphans.Num() > 0)
	{
		// A screen full of names does not help more than the first ones; what
		// matters is recognising the group.
		TArray<FString> Shown = Orphans;
		if (Shown.Num() > 8)
		{
			Shown.SetNum(8);
		}

		FString Names = FString::Join(Shown, TEXT(", "));
		if (Orphans.Num() > Shown.Num())
		{
			Names += FString::Printf(TEXT(" and %d more"), Orphans.Num() - Shown.Num());
		}

		AddInfo(FString::Printf(
			TEXT("%d data node(s) feed nothing and were left out of the text: %s."),
			Orphans.Num(), *Names));
	}

	StampAnchors();

	Result.Text = FString::Join(Lines, TEXT("\n"));
}

// ---------------------------------------------------------------------------

FNodeScribeReader::FResult FNodeScribeReader::Read(const TArray<UEdGraphNode*>& Nodes, UBlueprint* Blueprint)
{
	FNodeScribeReadContext Context(Nodes, Blueprint);
	Context.Run();
	return MoveTemp(Context.Result);
}

FNodeScribeReader::FResult FNodeScribeReader::ReadGraph(UEdGraph* Graph, UBlueprint* Blueprint)
{
	if (!Graph)
	{
		FResult Empty;
		Empty.Diagnostics.Emplace(ENodeScribeSeverity::Error, 0, TEXT("No graph open."));
		Empty.ErrorCount = 1;
		return Empty;
	}

	return Read(Graph->Nodes, Blueprint);
}
