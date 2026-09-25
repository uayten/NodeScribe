#include "NodeScribeAIReader.h"

#include "NodeScribePropertyText.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "DataProviders/AIDataProvider.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * The key type as it shows on screen: `UBlackboardKeyType_Object` -> `Object`.
	 */
	FString DescribeKeyTypeName(const UBlackboardKeyType* KeyType)
	{
		FString Name = KeyType->GetClass()->GetName();
		Name.RemoveFromStart(TEXT("BlackboardKeyType_"));
		return Name;
	}

	/**
	 * The detail that qualifies the key: an Object's base class, an Enum's
	 * enum. Empty when the type has no detail.
	 *
	 * Through reflection, not a cast to each known subclass. There are ten
	 * types in the Engine and anyone can write their own; a `switch` of casts
	 * would answer empty for the key type the project itself created, without
	 * saying it was ignoring something.
	 */
	FString DescribeKeyTypeDetail(const UBlackboardKeyType* KeyType)
	{
		static const TCHAR* DetailProperties[] =
		{
			TEXT("BaseClass"),  // Object, Class
			TEXT("EnumType"),   // Enum
			TEXT("EnumName"),   // NativeEnum
		};

		for (const TCHAR* PropertyName : DetailProperties)
		{
			const FProperty* Property = KeyType->GetClass()->FindPropertyByName(FName(PropertyName));
			if (!Property)
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(KeyType);

			FString Text;
			if (!NodeScribePropertyText::ValueToText(Property, ValuePtr, Text)
				|| Text.IsEmpty() || Text == TEXT("None"))
			{
				continue;
			}

			// The short name is enough here: it is a type qualifier, not a
			// reference to resolve. `Object (Actor)` reads well;
			// `Object (/Script/Engine.Actor)` fills the line without adding anything.
			int32 Dot = INDEX_NONE;
			if (Text.FindLastChar(TEXT('.'), Dot))
			{
				Text = Text.RightChop(Dot + 1);
			}
			Text.RemoveFromEnd(TEXT("_C"));

			return Text;
		}

		return FString();
	}

	// -- Behavior Tree -------------------------------------------------------

	/** Two spaces per level, as in the rest of the format. */
	FString Indent(int32 Level)
	{
		return FString::ChrN(Level * 2, TEXT(' '));
	}

	/**
	 * The blackboard key selector comes out as the key's name, only.
	 *
	 * It is the most common BT parameter, and its canonical form is a struct
	 * with the list of accepted types inside -- a big blob that fills the line
	 * and hides the only thing that matters, which is which key.
	 */
	bool TryDescribeKeySelector(const FProperty* Property, const void* ValuePtr, FString& OutText)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty || StructProperty->Struct != FBlackboardKeySelector::StaticStruct())
		{
			return false;
		}

		const FBlackboardKeySelector* Selector = static_cast<const FBlackboardKeySelector*>(ValuePtr);
		OutText = Selector->SelectedKeyName.ToString();
		return !OutText.IsEmpty() && OutText != TEXT("None");
	}

	/**
	 * A BT value that may come from a provider: `Wait Time`, `Acceptable
	 * Radius` and company.
	 *
	 * Their canonical form is `(DefaultValue=1.000000)`, which fills the line
	 * hiding the `1.0`. The Engine itself knows how to describe them --
	 * `ToString()` gives the number when the value is fixed and the key's name
	 * when it is bound to the blackboard, which is exactly the distinction worth
	 * reading.
	 *
	 * There are two families because 5.8 retired the first: `FAIDataProviderValue`
	 * still exists and old nodes use it, but `Wait Time` is now
	 * `FValueOrBBKey_Float`. Testing only the old one fails silently -- it
	 * compiles, runs, and returns the big blob.
	 */
	bool TryDescribeBTValue(const FProperty* Property, const void* ValuePtr, FString& OutText)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty)
		{
			return false;
		}

		if (StructProperty->Struct->IsChildOf(FValueOrBlackboardKeyBase::StaticStruct()))
		{
			OutText = static_cast<const FValueOrBlackboardKeyBase*>(ValuePtr)->ToString();
			return !OutText.IsEmpty();
		}

		if (StructProperty->Struct->IsChildOf(FAIDataProviderValue::StaticStruct()))
		{
			OutText = static_cast<const FAIDataProviderValue*>(ValuePtr)->ToString();
			return !OutText.IsEmpty();
		}

		return false;
	}

	/**
	 * `BTTask_Patrol` -> `Patrol`.
	 *
	 * `GetNodeName` already resolves the Engine's nodes, but for a class coming
	 * from a Blueprint it only strips the `_C` -- so the task the project itself
	 * created showed up with the technical prefix, next to a clean `Move To`.
	 */
	FString CleanNodeName(const UBTNode* Node)
	{
		FString Name = Node->GetNodeName();

		for (const TCHAR* Prefix : { TEXT("BTTask_"), TEXT("BTService_"),
			TEXT("BTDecorator_"), TEXT("BTComposite_") })
		{
			if (Name.RemoveFromStart(Prefix))
			{
				break;
			}
		}

		return Name;
	}

	/**
	 * The node's parameters that depart from its class's default, in
	 * parentheses.
	 *
	 * Same question as the sheet, same formatter: the CDO of the node's class is
	 * the archetype. A `Move To` with the factory radius gets no parameter at all.
	 */
	FString DescribeNodeParams(const UBTNode* Node)
	{
		const UClass* Class = Node->GetClass();
		const UObject* Defaults = Class->GetDefaultObject();

		TArray<FString> Params;

		for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!NodeScribePropertyText::IsVisible(Property))
			{
				continue;
			}

			// The node's name already is the line. Repeating it as a parameter
			// would say the same thing twice.
			if (Property->GetFName() == TEXT("NodeName"))
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
			const void* DefaultPtr = Defaults
				? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr;

			FString Value;
			if (TryDescribeKeySelector(Property, ValuePtr, Value))
			{
				// A key selector is transient inside, so comparing with the
				// default does not work for it: it comes out whenever it has a key.
				Params.Add(FString::Printf(TEXT("%s = %s"),
					*NodeScribePropertyText::DisplayName(Property), *Value));
				continue;
			}

			if (!NodeScribePropertyText::DiffersFromDefault(Property, ValuePtr, DefaultPtr))
			{
				continue;
			}

			if (!TryDescribeBTValue(Property, ValuePtr, Value)
				&& !NodeScribePropertyText::ValueToText(Property, ValuePtr, Value))
			{
				continue;
			}

			Params.Add(FString::Printf(TEXT("%s = %s"),
				*NodeScribePropertyText::DisplayName(Property), *Value));
		}

		return Params.Num() > 0
			? FString::Printf(TEXT(" (%s)"), *FString::Join(Params, TEXT(", ")))
			: FString();
	}

	/** A node line, with the parameters that depart from the default. */
	FString NodeLine(int32 Level, const TCHAR* Prefix, const UBTNode* Node)
	{
		return Indent(Level) + Prefix + CleanNodeName(Node) + DescribeNodeParams(Node);
	}

	/**
	 * Walks the tree.
	 *
	 * @param Seen  guards against a corrupted asset. A BT does not cycle by
	 *              construction, but if it does this stops and says so, instead
	 *              of writing until memory runs out.
	 */
	void EmitNode(TArray<FString>& OutLines, const UBTNode* Node, int32 Level,
		TSet<const UBTNode*>& Seen)
	{
		if (!Node)
		{
			return;
		}

		if (Seen.Contains(Node))
		{
			OutLines.Add(Indent(Level) + TEXT("# [warning]: cycle at `")
				+ CleanNodeName(Node) + TEXT("` -- stopped here."));
			return;
		}
		Seen.Add(Node);

		OutLines.Add(NodeLine(Level, TEXT(""), Node));

		// A service belongs to the node; a decorator belongs to the link with the
		// parent, and that is why it comes out together with the child, further
		// down. It is how the editor shows it too.
		const TArray<TObjectPtr<UBTService>>* Services = nullptr;
		if (const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
		{
			Services = &Composite->Services;
		}
		else if (const UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
		{
			Services = &Task->Services;
		}

		if (Services)
		{
			for (const UBTService* Service : *Services)
			{
				if (Service)
				{
					OutLines.Add(NodeLine(Level + 1, TEXT("service "), Service));
				}
			}
		}

		const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
		if (!Composite)
		{
			return;
		}

		for (const FBTCompositeChild& Child : Composite->Children)
		{
			for (const UBTDecorator* Decorator : Child.Decorators)
			{
				if (Decorator)
				{
					OutLines.Add(NodeLine(Level + 1, TEXT("decorator "), Decorator));
				}
			}

			const UBTNode* ChildNode = Child.ChildComposite
				? static_cast<const UBTNode*>(Child.ChildComposite)
				: static_cast<const UBTNode*>(Child.ChildTask);

			if (!ChildNode)
			{
				// An empty child exists: it is the branch left half-done in the editor.
				OutLines.Add(Indent(Level + 1) + TEXT("# [warning]: branch without a node."));
				continue;
			}

			EmitNode(OutLines, ChildNode, Level + 1, Seen);
		}
	}
}

bool FNodeScribeAIReader::Handles(const UObject* Object)
{
	return Object && (Object->IsA<UBlackboardData>() || Object->IsA<UBehaviorTree>());
}

FString FNodeScribeAIReader::ReadAsset(UObject* Object)
{
	if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Object))
	{
		return ReadBlackboard(Blackboard);
	}

	if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Object))
	{
		return ReadBehaviorTree(Tree);
	}

	return FString();
}

FString FNodeScribeAIReader::ReadBehaviorTree(UBehaviorTree* Tree)
{
	TArray<FString> Lines;

	FString Header = FString::Printf(TEXT("tree %s"), *Tree->GetName());
	if (const UBlackboardData* Blackboard = Tree->BlackboardAsset)
	{
		// The blackboard goes in the header because every key mentioned below
		// comes from it -- without that the key names are loose words.
		Header += FString::Printf(TEXT("  (blackboard %s)"), *Blackboard->GetName());
	}
	Lines.Add(Header);

	for (const UBTDecorator* Decorator : Tree->RootDecorators)
	{
		if (Decorator)
		{
			Lines.Add(NodeLine(0, TEXT("decorator "), Decorator));
		}
	}

	if (!Tree->RootNode)
	{
		Lines.Add(TEXT("# empty tree"));
		return FString::Join(Lines, TEXT("\n"));
	}

	TSet<const UBTNode*> Seen;
	EmitNode(Lines, Tree->RootNode, 0, Seen);

	return FString::Join(Lines, TEXT("\n"));
}

FString FNodeScribeAIReader::ReadBlackboard(UBlackboardData* Blackboard)
{
	TArray<FString> Lines;

	FString Header = FString::Printf(TEXT("blackboard %s"), *Blackboard->GetName());
	if (const UBlackboardData* Parent = Blackboard->Parent)
	{
		// An inherited key is not repeated: it would be the same lines in every
		// child sheet, and the parent is one call away.
		Header += FString::Printf(TEXT("  (inherits %s)"), *Parent->GetName());
	}
	Lines.Add(Header);

	TArray<FString> Unreadable;

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (!Entry.KeyType)
		{
			// A key without a type exists: it is the line just created in the
			// editor, with no choice made yet. Saying so is better than omitting it.
			Unreadable.Add(Entry.EntryName.ToString());
			continue;
		}

		FString Line = FString::Printf(TEXT("key %s : %s"),
			*Entry.EntryName.ToString(), *DescribeKeyTypeName(Entry.KeyType));

		const FString Detail = DescribeKeyTypeDetail(Entry.KeyType);
		if (!Detail.IsEmpty())
		{
			Line += FString::Printf(TEXT(" (%s)"), *Detail);
		}

		// Synced across instances is behaviour, not type, and it changes what
		// the AI does -- it is absent from the default line and shows up when on.
		//
		// A word, not a comment: the writer discards comments, and a key that
		// comes back unsynced would be a bug that only shows up with two enemies
		// on screen.
		if (Entry.bInstanceSynced)
		{
			Line += TEXT(" synced");
		}

		Lines.Add(Line);
	}

	if (Blackboard->Keys.Num() == 0)
	{
		Lines.Add(TEXT("# no keys"));
	}

	if (Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [warning]: key with no type chosen: %s"),
			*FString::Join(Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}
