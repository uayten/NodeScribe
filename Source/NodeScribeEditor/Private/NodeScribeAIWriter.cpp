#include "NodeScribeAIWriter.h"

#include "NodeScribeAIReader.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeParser.h"
#include "NodeScribePropertyText.h"

#include "AIGraphTypes.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTreeEditorModule.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_CompositeDecorator.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "BehaviorTreeGraphNode_SubtreeTask.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "DataProviders/AIDataProvider.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace
{
	/**
	 * The key type class by short name: `Object` ->
	 * `UBlackboardKeyType_Object`.
	 *
	 * Sweeps the classes instead of keeping a table, for the same reason as the
	 * reader: any project can write its own key type, and a fixed table would
	 * answer "unknown" for the type the project itself created.
	 */
	UClass* FindKeyTypeClass(const FString& Name, TArray<FString>& OutKnown)
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);
		UClass* Found = nullptr;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UBlackboardKeyType::StaticClass())
				|| Class == UBlackboardKeyType::StaticClass()
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}

			FString Short = Class->GetName();
			Short.RemoveFromStart(TEXT("BlackboardKeyType_"));
			OutKnown.Add(Short);

			if (FNodeScribeCatalog::Normalize(Short) == Wanted)
			{
				Found = Class;
			}
		}

		OutKnown.Sort();
		return Found;
	}

	/**
	 * Sets the detail that qualifies the key -- an Object's base class, an
	 * Enum's enum -- through reflection, mirroring the reader.
	 */
	bool ApplyKeyTypeDetail(UBlackboardKeyType* KeyType, const FString& Detail, FString& OutError)
	{
		static const TCHAR* DetailProperties[] =
		{
			TEXT("BaseClass"),
			TEXT("EnumType"),
			TEXT("EnumName"),
		};

		for (const TCHAR* PropertyName : DetailProperties)
		{
			FProperty* Property = KeyType->GetClass()->FindPropertyByName(FName(PropertyName));
			if (!Property)
			{
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(KeyType);

			// The reader writes the short name (`Actor`), which carries no path.
			// Here it is worth looking the class up by name before giving up.
			FString Text = Detail;
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				if (!Text.StartsWith(TEXT("/")))
				{
					if (UClass* Resolved = UClass::TryFindTypeSlow<UClass>(Text))
					{
						ObjectProperty->SetObjectPropertyValue(ValuePtr, Resolved);
						return true;
					}
					OutError = FString::Printf(TEXT("could not find class `%s`"), *Detail);
					return false;
				}
			}

			return NodeScribePropertyText::TextToValue(Property, ValuePtr, Text, OutError);
		}

		OutError = FString::Printf(
			TEXT("type %s does not take a detail in parentheses"), *KeyType->GetClass()->GetName());
		return false;
	}

}

namespace
{
	// -- Behavior Tree -------------------------------------------------------

	enum class EBTEntryKind : uint8
	{
		Node,
		Decorator,
		Service,
	};

	/** One line of the tree text, already placed in the hierarchy. */
	struct FBTEntry
	{
		EBTEntryKind Kind = EBTEntryKind::Node;
		int32 LineNumber = 0;

		/** The line without indentation, keyword or comment -- what the reader writes. */
		FString Line;

		/** The name given in the editor, from `CombatState = Selector`. */
		FString CustomName;
		FString ClassText;
		TArray<FNodeScribeArg> Args;

		UClass* Class = nullptr;

		/** A fresh instance with the line's parameters, or null if they did not apply. */
		UBTNode* Scratch = nullptr;
		TArray<FString> ParamErrors;

		/** Indices into the entry list. Parent is the node it hangs from, or is a sub-node of. */
		int32 Parent = INDEX_NONE;
		TArray<int32> Decorators;
		TArray<int32> Services;
		TArray<int32> Children;

		UBehaviorTreeGraphNode* GraphNode = nullptr;
	};

	/** Spaces do not count when comparing a line with the reader's. */
	FString SquashSpaces(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (!FChar::IsWhitespace(C))
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	/** `BTTask_MoveTo_C` -> `MoveTo`: the class name the way the reader cleans a node's. */
	FString StripNodeClassName(FString Name)
	{
		Name.RemoveFromEnd(TEXT("_C"));
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
	 * The node class by the name the reader writes: `Wait`, `Move To`,
	 * `BTT_SetMovimentSpeed`.
	 *
	 * Goes through the Behavior Tree editor's own class cache, which is the
	 * list its context menu shows -- including the project's Blueprint nodes
	 * that are not loaded yet. Two classes answering to the same name are not a
	 * choice: the caller gets both and refuses.
	 */
	TArray<UClass*> FindNodeClasses(const FString& Text, TArray<UClass*> BaseClasses)
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Text);
		TArray<UClass*> Found;

		FBehaviorTreeEditorModule& EditorModule =
			FModuleManager::LoadModuleChecked<FBehaviorTreeEditorModule>(TEXT("BehaviorTreeEditor"));
		TSharedPtr<FGraphNodeClassHelper> ClassCache = EditorModule.GetClassCache();

		for (UClass* BaseClass : BaseClasses)
		{
			TArray<FGraphNodeClassData> Classes;
			if (ClassCache.IsValid())
			{
				ClassCache->GatherClasses(BaseClass, Classes);
			}

			for (FGraphNodeClassData& Data : Classes)
			{
				const bool bByName =
					FNodeScribeCatalog::Normalize(StripNodeClassName(Data.GetClassName())) == Wanted
					|| FNodeScribeCatalog::Normalize(Data.ToString()) == Wanted;
				if (!bByName)
				{
					continue;
				}

				if (UClass* Class = Data.GetClass(/*bSilent*/ true))
				{
					Found.AddUnique(Class);
				}
			}

			// The name the reader writes comes from the node's CDO -- `Move To`
			// is `BTTask_MoveTo`'s `NodeName`. That needs the class loaded, which
			// the Engine's always are.
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class->IsChildOf(BaseClass)
					|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
					|| Class->GetName().StartsWith(TEXT("SKEL_"))
					|| Class->GetName().StartsWith(TEXT("REINST_")))
				{
					continue;
				}

				const UBTNode* Defaults = Cast<UBTNode>(Class->GetDefaultObject());
				if (Defaults && FNodeScribeCatalog::Normalize(FNodeScribeAIReader::DescribeBTNode(Defaults)) == Wanted)
				{
					Found.AddUnique(Class);
				}
			}
		}

		return Found;
	}

	/** The names a property answers to: the one on screen and the C++ one. */
	bool PropertyMatches(const FProperty* Property, const FString& WantedNormalized)
	{
		return FNodeScribeCatalog::Normalize(NodeScribePropertyText::DisplayName(Property)) == WantedNormalized
			|| FNodeScribeCatalog::Normalize(Property->GetName()) == WantedNormalized;
	}

	/**
	 * The key by name, inherited ones included. A key the blackboard does not
	 * have would be saved and silently never read, so the miss lists the keys.
	 */
	const FBlackboardEntry* FindBlackboardKey(const UBlackboardData* Blackboard, const FString& Name, FString& OutError)
	{
		if (const FBlackboardEntry* Key = Blackboard->GetKey(Blackboard->GetKeyID(FName(*Name))))
		{
			return Key;
		}

		TArray<FString> Keys;
		for (const UBlackboardData* It = Blackboard; It; It = It->Parent)
		{
			for (const FBlackboardEntry& Entry : It->Keys)
			{
				Keys.Add(Entry.EntryName.ToString());
			}
		}
		OutError = FString::Printf(TEXT("blackboard `%s` has no key `%s`. Keys: %s"),
			*Blackboard->GetName(), *Name, *FString::Join(Keys, TEXT(", ")));
		return nullptr;
	}

	/**
	 * Mirrors the reader's two special forms: a key selector written as the
	 * key's name, and a provided value written as the number or `Key: Name`.
	 *
	 * @return true when the property was one of them -- handled or refused,
	 *         with the reason in OutError.
	 */
	bool TryApplySpecialValue(FProperty* Property, void* ValuePtr, const FString& Text,
		const UBlackboardData* Blackboard, bool& bOutOk, FString& OutError)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty)
		{
			return false;
		}

		const FString Bare = Text.TrimStartAndEnd();

		if (StructProperty->Struct == FBlackboardKeySelector::StaticStruct())
		{
			FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(ValuePtr);

			if (Blackboard && !FindBlackboardKey(Blackboard, Bare, OutError))
			{
				bOutOk = false;
				return true;
			}

			Selector->SelectedKeyName = FName(*Bare);
			if (Blackboard)
			{
				Selector->ResolveSelectedKey(*Blackboard);
			}
			bOutOk = true;
			return true;
		}

		const bool bValueOrKey = StructProperty->Struct->IsChildOf(FValueOrBlackboardKeyBase::StaticStruct());
		const bool bProvided = StructProperty->Struct->IsChildOf(FAIDataProviderValue::StaticStruct());
		if (!bValueOrKey && !bProvided)
		{
			return false;
		}

		if (bValueOrKey)
		{
			FValueOrBlackboardKeyBase* Value = static_cast<FValueOrBlackboardKeyBase*>(ValuePtr);

			FString KeyName = Bare;
			if (KeyName.RemoveFromStart(TEXT("Key:")))
			{
				KeyName.TrimStartInline();

				// A key of the wrong type compiles, saves, and at runtime the
				// node quietly falls back to its fixed value.
				if (Blackboard)
				{
					const FBlackboardEntry* Key = FindBlackboardKey(Blackboard, KeyName, OutError);
					if (!Key)
					{
						bOutOk = false;
						return true;
					}
					if (!Key->KeyType || !Value->IsCompatibleType(Key->KeyType))
					{
						OutError = FString::Printf(TEXT("key `%s` is %s, which this value cannot read"),
							*KeyName, Key->KeyType ? *Key->KeyType->GetClass()->GetName() : TEXT("untyped"));
						bOutOk = false;
						return true;
					}
				}

				Value->SetKey(FName(*KeyName));
				bOutOk = true;
				return true;
			}
			Value->SetKey(NAME_None);
		}

		FProperty* DefaultProperty = StructProperty->Struct->FindPropertyByName(TEXT("DefaultValue"));
		if (!DefaultProperty)
		{
			OutError = FString::Printf(TEXT("`%s` has no fixed value to write"), *StructProperty->Struct->GetName());
			bOutOk = false;
			return true;
		}

		void* DefaultPtr = DefaultProperty->ContainerPtrToValuePtr<void>(ValuePtr);

		// An enum value comes out by name, but is kept as a byte next to its
		// enum -- the generic path would read `Jogging` as a number and fail.
		if (bValueOrKey && StructProperty->Struct->IsChildOf(FValueOrBBKey_Enum::StaticStruct()))
		{
			const FProperty* EnumTypeProperty = StructProperty->Struct->FindPropertyByName(TEXT("EnumType"));
			const UEnum* Enum = EnumTypeProperty
				? Cast<UEnum>(CastField<FObjectPropertyBase>(EnumTypeProperty)->GetObjectPropertyValue(
					EnumTypeProperty->ContainerPtrToValuePtr<void>(ValuePtr)))
				: nullptr;

			const int64 Index = Enum ? Enum->GetValueByNameString(Bare) : INDEX_NONE;
			if (Index == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("`%s` is not a value of %s"), *Bare, *GetNameSafe(Enum));
				bOutOk = false;
				return true;
			}

			CastField<FByteProperty>(DefaultProperty)->SetPropertyValue(DefaultPtr, static_cast<uint8>(Index));
			bOutOk = true;
			return true;
		}

		bOutOk = NodeScribePropertyText::TextToValue(DefaultProperty, DefaultPtr, Bare, OutError);
		return true;
	}

	/**
	 * Applies the line's parameters to a fresh node instance, the way the
	 * reader wrote them: by the name shown in the details panel.
	 */
	void ApplyNodeArgs(UBTNode* Node, const TArray<FNodeScribeArg>& Args,
		const UBlackboardData* Blackboard, TArray<FString>& OutErrors)
	{
		for (const FNodeScribeArg& Arg : Args)
		{
			if (Arg.PinName.IsEmpty())
			{
				OutErrors.Add(FString::Printf(
					TEXT("`%s` has no name -- write `Parameter = value`"), *Arg.Value));
				continue;
			}

			if (Arg.bIsReference)
			{
				OutErrors.Add(FString::Printf(
					TEXT("`%s = $%s`: a tree node takes fixed values, not references"),
					*Arg.PinName, *Arg.Value));
				continue;
			}

			const FString Wanted = FNodeScribeCatalog::Normalize(Arg.PinName);
			FProperty* Property = nullptr;
			TArray<FString> Known;

			for (TFieldIterator<FProperty> It(Node->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				if (!NodeScribePropertyText::IsVisible(*It) || It->GetFName() == TEXT("NodeName"))
				{
					continue;
				}
				Known.Add(NodeScribePropertyText::DisplayName(*It));
				if (!Property && PropertyMatches(*It, Wanted))
				{
					Property = *It;
				}
			}

			if (!Property)
			{
				OutErrors.Add(FString::Printf(TEXT("`%s` has no parameter `%s`. It has: %s"),
					*FNodeScribeAIReader::DescribeBTNode(Cast<UBTNode>(Node->GetClass()->GetDefaultObject())),
					*Arg.PinName, *FString::Join(Known, TEXT(", "))));
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);

			FString Error;
			bool bOk = false;
			if (!TryApplySpecialValue(Property, ValuePtr, Arg.Value, Blackboard, bOk, Error))
			{
				bOk = NodeScribePropertyText::TextToValue(Property, ValuePtr, Arg.Value, Error);
			}

			if (!bOk)
			{
				OutErrors.Add(FString::Printf(TEXT("`%s`: %s"), *Arg.PinName, *Error));
			}
		}
	}

	/** The graph node class the editor's menu would use for this instance class. */
	UClass* GraphNodeClassFor(const UClass* InstanceClass)
	{
		if (InstanceClass->IsChildOf(UBTCompositeNode::StaticClass()))
		{
			return UBehaviorTreeGraphNode_Composite::StaticClass();
		}
		if (InstanceClass->IsChildOf(UBTTask_RunBehavior::StaticClass()))
		{
			return UBehaviorTreeGraphNode_SubtreeTask::StaticClass();
		}
		if (InstanceClass->IsChildOf(UBTTaskNode::StaticClass()))
		{
			return UBehaviorTreeGraphNode_Task::StaticClass();
		}
		if (InstanceClass->IsChildOf(UBTDecorator::StaticClass()))
		{
			return UBehaviorTreeGraphNode_Decorator::StaticClass();
		}
		return UBehaviorTreeGraphNode_Service::StaticClass();
	}

	/** Same steps as the editor's "add node" action, without the transaction of its own. */
	UBehaviorTreeGraphNode* SpawnGraphNode(UBehaviorTreeGraph* Graph, UBTNode* Instance, bool bSubNode)
	{
		UBehaviorTreeGraphNode* GraphNode = NewObject<UBehaviorTreeGraphNode>(
			Graph, GraphNodeClassFor(Instance->GetClass()), NAME_None, RF_Transactional);
		GraphNode->ClassData = FGraphNodeClassData(Instance->GetClass(), FString());

		if (!bSubNode)
		{
			Graph->AddNode(GraphNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		}

		GraphNode->CreateNewGuid();
		GraphNode->NodeInstance = Instance;
		GraphNode->InitializeInstance();
		GraphNode->AllocateDefaultPins();
		return GraphNode;
	}

	/** Moves the scratch instance into the asset, where node instances live. */
	void AdoptInstance(UBehaviorTree* Tree, UBTNode* Instance)
	{
		Instance->Rename(nullptr, Tree, REN_DontCreateRedirectors | REN_NonTransactional);
		Instance->SetFlags(RF_Transactional);
	}

	/** The graph node's parent through its input wire, if any. */
	UEdGraphNode* LinkedParent(const UBehaviorTreeGraphNode* GraphNode)
	{
		const UEdGraphPin* Input = GraphNode->GetInputPin();
		return Input && Input->LinkedTo.Num() > 0 && Input->LinkedTo[0]
			? Input->LinkedTo[0]->GetOwningNode() : nullptr;
	}

	/** The children as the runtime tree orders them: left to right. */
	TArray<UEdGraphNode*> LinkedChildren(const UEdGraphNode* GraphNode)
	{
		TArray<UEdGraphNode*> Children;
		const UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode);
		if (const UEdGraphPin* Output = AINode ? AINode->GetOutputPin() : nullptr)
		{
			for (const UEdGraphPin* Linked : Output->LinkedTo)
			{
				if (Linked)
				{
					Children.Add(Linked->GetOwningNode());
				}
			}
		}
		Children.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return A.NodePosX < B.NodePosX;
		});
		return Children;
	}

	/**
	 * Lays the tree out under the root, left to right in the text's order.
	 *
	 * The order is not cosmetic: the runtime tree orders a composite's
	 * children by their X in the graph. That is also why the editor's own Auto
	 * Arrange is not used -- it measures the node widgets, which only exist
	 * with the tree open on screen.
	 */
	int32 LayoutEntry(TArray<FBTEntry>& Entries, int32 Index, int32 Y, int32& NextLeafX)
	{
		static constexpr int32 ColumnWidth = 360;
		static constexpr int32 RowGap = 160;
		static constexpr int32 BodyHeight = 120;
		static constexpr int32 SubNodeHeight = 60;

		FBTEntry& Entry = Entries[Index];
		const int32 Height = BodyHeight
			+ SubNodeHeight * (Entry.Decorators.Num() + Entry.Services.Num());

		int32 X = 0;
		if (Entry.Children.Num() == 0)
		{
			X = NextLeafX;
			NextLeafX += ColumnWidth;
		}
		else
		{
			int32 First = 0;
			int32 Last = 0;
			for (int32 ChildSlot = 0; ChildSlot < Entry.Children.Num(); ++ChildSlot)
			{
				const int32 ChildX = LayoutEntry(Entries, Entry.Children[ChildSlot], Y + Height + RowGap, NextLeafX);
				if (ChildSlot == 0)
				{
					First = ChildX;
				}
				Last = ChildX;
			}
			X = (First + Last) / 2;
		}

		Entry.GraphNode->NodePosX = X;
		Entry.GraphNode->NodePosY = Y;
		return X;
	}

	void ShiftEntry(TArray<FBTEntry>& Entries, int32 Index, int32 DeltaX)
	{
		Entries[Index].GraphNode->NodePosX += DeltaX;
		for (const int32 Child : Entries[Index].Children)
		{
			ShiftEntry(Entries, Child, DeltaX);
		}
	}
}

bool FNodeScribeAIWriter::Handles(const UObject* Object)
{
	return Object && (Object->IsA<UBlackboardData>() || Object->IsA<UBehaviorTree>());
}

FNodeScribeAIWriter::FResult FNodeScribeAIWriter::WriteAsset(UObject* Object, const FString& Text)
{
	if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Object))
	{
		return WriteBlackboard(Blackboard, Text);
	}

	if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Object))
	{
		return WriteBehaviorTree(Tree, Text);
	}

	FResult Result;
	Result.Diagnostics.Add(TEXT("[error]: I do not know how to write into this kind of asset."));
	return Result;
}

FNodeScribeAIWriter::FResult FNodeScribeAIWriter::WriteBlackboard(
	UBlackboardData* Blackboard, const FString& Text)
{
	FResult Result;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	const FScopedTransaction Transaction(
		LOCTEXT("WriteBlackboardTransaction", "NodeScribe: write blackboard"));

	bool bChanged = false;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();

		if (Line.IsEmpty() || Line.StartsWith(TEXT("blackboard "), ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Line.RemoveFromStart(TEXT("key "), ESearchCase::IgnoreCase))
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: expected `key Name : Type`, got `%s`."),
				LineNumber, *Line));
			continue;
		}

		// Synced is behaviour, and it comes off the end of the line before the rest.
		bool bInstanceSynced = false;
		if (Line.EndsWith(TEXT(" synced"), ESearchCase::IgnoreCase))
		{
			bInstanceSynced = true;
			Line = Line.LeftChop(7).TrimEnd();
		}

		int32 Colon = INDEX_NONE;
		if (!Line.FindChar(TEXT(':'), Colon))
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: the `:` between the name and the type is missing."), LineNumber));
			continue;
		}

		const FString KeyName = Line.Left(Colon).TrimEnd();
		FString TypeText = Line.RightChop(Colon + 1).TrimStart();

		// `Object (Actor)` -- the detail in parentheses qualifies the type.
		FString Detail;
		int32 Open = INDEX_NONE;
		if (TypeText.FindChar(TEXT('('), Open))
		{
			Detail = TypeText.RightChop(Open + 1);
			Detail.RemoveFromEnd(TEXT(")"));
			Detail.TrimStartAndEndInline();
			TypeText = TypeText.Left(Open).TrimEnd();
		}

		if (KeyName.IsEmpty() || TypeText.IsEmpty())
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: empty name or type."), LineNumber));
			continue;
		}

		TArray<FString> Known;
		UClass* KeyTypeClass = FindKeyTypeClass(TypeText, Known);
		if (!KeyTypeClass)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: unknown type `%s`. Known: %s."),
				LineNumber, *TypeText, *FString::Join(Known, TEXT(", "))));
			continue;
		}

		Blackboard->Modify();

		FBlackboardEntry* Entry = Blackboard->Keys.FindByPredicate(
			[&KeyName](const FBlackboardEntry& Candidate)
			{
				return Candidate.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase);
			});

		if (!Entry)
		{
			FBlackboardEntry NewEntry;
			NewEntry.EntryName = FName(*KeyName);
			Entry = &Blackboard->Keys[Blackboard->Keys.Add(NewEntry)];
		}

		// Changing the type changes the subobject. Reusing the old one would
		// leave the previous key's properties hanging on the new one.
		if (!Entry->KeyType || Entry->KeyType->GetClass() != KeyTypeClass)
		{
			Entry->KeyType = NewObject<UBlackboardKeyType>(Blackboard, KeyTypeClass);
		}

		Entry->bInstanceSynced = bInstanceSynced;

		if (!Detail.IsEmpty())
		{
			FString Error;
			if (!ApplyKeyTypeDetail(Entry->KeyType, Detail, Error))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [warning]: key `%s` created, but %s."),
					LineNumber, *KeyName, *Error));
			}
		}

		bChanged = true;
		++Result.Applied;
	}

	if (bChanged)
	{
		// Without this the asset stays changed in memory and clean on disk: the
		// editor closes and the work vanishes without anyone warning.
		Blackboard->MarkPackageDirty();
	}

	return Result;
}

FNodeScribeAIWriter::FResult FNodeScribeAIWriter::WriteBehaviorTree(
	UBehaviorTree* Tree, const FString& Text)
{
	FResult Result;
	TArray<FString> Errors;

	auto LineError = [&Errors](int32 LineNumber, const FString& Message)
	{
		Errors.Add(FString::Printf(TEXT("line %d [error]: %s"), LineNumber, *Message));
	};

	// -- 1. Text -> entries, placed in the hierarchy -------------------------

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(Text, ParseDiagnostics);
	for (const FNodeScribeDiagnostic& Diagnostic : ParseDiagnostics)
	{
		if (Diagnostic.Severity == ENodeScribeSeverity::Error)
		{
			LineError(Diagnostic.Line, Diagnostic.Message);
		}
	}

	TArray<FBTEntry> Entries;
	int32 RootEntry = INDEX_NONE;

	// The last node seen at each indentation level, and the decorators waiting
	// for the node they guard -- in the text a decorator comes right before it.
	TArray<int32> NodeAtLevel;
	TMap<int32, TArray<int32>> PendingDecorators;

	auto FlushPendingFrom = [&](int32 Level)
	{
		for (TPair<int32, TArray<int32>>& Pending : PendingDecorators)
		{
			if (Pending.Key >= Level)
			{
				for (const int32 Orphan : Pending.Value)
				{
					LineError(Entries[Orphan].LineNumber,
						TEXT("a decorator with no node under it -- it guards the node on the next line, at the same indentation."));
				}
				Pending.Value.Reset();
			}
		}
	};

	for (const FNodeScribeStatement& Statement : Statements)
	{
		if (Statement.bIsVariable || Statement.bIsLabel)
		{
			LineError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` is not a tree line. A tree has nodes, `decorator X` and `service X`."),
				*Statement.RawLine));
			continue;
		}

		FString Head = Statement.OutputName.IsEmpty() ? Statement.NodeExpression : Statement.OutputName;

		// The header the reader writes: `tree BT_X  (blackboard BB_X)`.
		if (Statement.Indent == 0 && Head.StartsWith(TEXT("tree "), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FBTEntry Entry;
		Entry.LineNumber = Statement.LineNumber;

		if (Head.RemoveFromStart(TEXT("decorator "), ESearchCase::IgnoreCase))
		{
			Entry.Kind = EBTEntryKind::Decorator;
		}
		else if (Head.RemoveFromStart(TEXT("service "), ESearchCase::IgnoreCase))
		{
			Entry.Kind = EBTEntryKind::Service;
		}

		if (Statement.OutputName.IsEmpty())
		{
			Entry.ClassText = Head.TrimStartAndEnd();
		}
		else
		{
			Entry.CustomName = Head.TrimStartAndEnd();
			Entry.ClassText = Statement.NodeExpression.TrimStartAndEnd();
		}
		Entry.Args = Statement.Args;

		FString Line = FNodeScribeParser::StripComment(Statement.RawLine).TrimStartAndEnd();
		if (Entry.Kind != EBTEntryKind::Node)
		{
			Line.RemoveFromStart(Entry.Kind == EBTEntryKind::Decorator ? TEXT("decorator ") : TEXT("service "),
				ESearchCase::IgnoreCase);
		}
		Entry.Line = Line.TrimStart();

		const int32 Level = Statement.Indent;
		const int32 Index = Entries.Add(MoveTemp(Entry));

		if (Level > NodeAtLevel.Num())
		{
			LineError(Statement.LineNumber, TEXT("indented deeper than the line above allows."));
			continue;
		}

		switch (Entries[Index].Kind)
		{
		case EBTEntryKind::Decorator:
			PendingDecorators.FindOrAdd(Level).Add(Index);
			break;

		case EBTEntryKind::Service:
			if (Level == 0)
			{
				LineError(Statement.LineNumber, TEXT("a service belongs to a node -- indent it under one."));
				break;
			}
			Entries[NodeAtLevel[Level - 1]].Services.Add(Index);
			Entries[Index].Parent = NodeAtLevel[Level - 1];
			break;

		case EBTEntryKind::Node:
			FlushPendingFrom(Level + 1);
			if (TArray<int32>* Pending = PendingDecorators.Find(Level))
			{
				Entries[Index].Decorators = MoveTemp(*Pending);
				Pending->Reset();
				for (const int32 Decorator : Entries[Index].Decorators)
				{
					Entries[Decorator].Parent = Index;
				}
			}

			if (Level == 0)
			{
				if (RootEntry != INDEX_NONE)
				{
					LineError(Statement.LineNumber, FString::Printf(
						TEXT("a tree has a single top node, and `%s` already is it."),
						*Entries[RootEntry].Line));
				}
				else
				{
					RootEntry = Index;
				}
			}
			else
			{
				Entries[NodeAtLevel[Level - 1]].Children.Add(Index);
				Entries[Index].Parent = NodeAtLevel[Level - 1];
			}

			NodeAtLevel.SetNum(Level + 1);
			NodeAtLevel[Level] = Index;
			break;
		}
	}
	FlushPendingFrom(0);

	if (RootEntry == INDEX_NONE && Errors.Num() == 0)
	{
		Errors.Add(TEXT("[error]: the text has no node. Writing an empty tree is not supported."));
	}

	// -- 2. Classes and parameters, on scratch instances ---------------------

	const UBlackboardData* Blackboard = Tree->BlackboardAsset;

	for (FBTEntry& Entry : Entries)
	{
		TArray<UClass*> Bases;
		switch (Entry.Kind)
		{
		case EBTEntryKind::Node:      Bases = { UBTCompositeNode::StaticClass(), UBTTaskNode::StaticClass() }; break;
		case EBTEntryKind::Decorator: Bases = { UBTDecorator::StaticClass() }; break;
		case EBTEntryKind::Service:   Bases = { UBTService::StaticClass() }; break;
		}

		const TArray<UClass*> Classes = FindNodeClasses(Entry.ClassText, Bases);
		if (Classes.Num() != 1)
		{
			TArray<FString> Names;
			for (const UClass* Class : Classes)
			{
				Names.Add(Class->GetPathName());
			}
			LineError(Entry.LineNumber, Classes.Num() == 0
				? FString::Printf(TEXT("no %s called `%s`."),
					Entry.Kind == EBTEntryKind::Decorator ? TEXT("decorator")
					: Entry.Kind == EBTEntryKind::Service ? TEXT("service") : TEXT("task or composite"),
					*Entry.ClassText)
				: FString::Printf(TEXT("`%s` is ambiguous: %s."), *Entry.ClassText, *FString::Join(Names, TEXT(", "))));
			continue;
		}

		Entry.Class = Classes[0];

		// Simple Parallel has two kinds of child -- the main task and the
		// background --, and the text has no way to say which is which yet.
		if (Entry.Class->IsChildOf(UBTComposite_SimpleParallel::StaticClass()))
		{
			LineError(Entry.LineNumber, TEXT("Simple Parallel cannot be written yet: the text does not say which child is the main task."));
			continue;
		}

		const bool bComposite = Entry.Class->IsChildOf(UBTCompositeNode::StaticClass());
		if (!bComposite && Entry.Children.Num() > 0)
		{
			LineError(Entry.LineNumber, FString::Printf(
				TEXT("`%s` is a task; only a composite (Selector, Sequence) has children."), *Entry.ClassText));
		}

		UBTNode* Scratch = NewObject<UBTNode>(GetTransientPackage(), Entry.Class);
		if (!Entry.CustomName.IsEmpty())
		{
			Scratch->NodeName = Entry.CustomName;
		}
		ApplyNodeArgs(Scratch, Entry.Args, Blackboard, Entry.ParamErrors);
		Entry.Scratch = Scratch;
	}

	if (RootEntry != INDEX_NONE && Entries[RootEntry].Class
		&& !Entries[RootEntry].Class->IsChildOf(UBTCompositeNode::StaticClass()))
	{
		LineError(Entries[RootEntry].LineNumber,
			TEXT("the top node has to be a composite (Selector, Sequence) -- the editor does not hang a task from the root."));
	}

	// -- 3. The graph, and what it holds that the text cannot say ------------

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
	UBehaviorTreeGraphNode_Root* RootGraphNode = nullptr;

	if (Graph)
	{
		for (UEdGraphNode* EdNode : Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(EdNode))
			{
				RootGraphNode = Root;
				continue;
			}

			const UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(EdNode);
			if (!BTNode)
			{
				continue;
			}

			// Rebuilding would flatten these into something else, so the write
			// does not start rather than lose them.
			if (BTNode->IsA<UBehaviorTreeGraphNode_SimpleParallel>())
			{
				Errors.Add(TEXT("[error]: the tree has a Simple Parallel, which the writer cannot rebuild yet. Nothing was changed."));
				break;
			}
			const bool bUnsupportedSubNode = BTNode->Decorators.ContainsByPredicate(
				[](const UBehaviorTreeGraphNode* Decorator)
				{
					return Decorator && (Decorator->bInjectedNode || Decorator->IsA<UBehaviorTreeGraphNode_CompositeDecorator>());
				});
			if (bUnsupportedSubNode)
			{
				Errors.Add(TEXT("[error]: the tree has a composite decorator or a decorator injected by a subtree, which the writer cannot rebuild yet. Nothing was changed."));
				break;
			}
		}
	}

	// -- 4. Matching: the existing node that already reads as the line stays -

	TArray<UBehaviorTreeGraphNode*> MainPool;
	TArray<UBehaviorTreeGraphNode*> SubPool;
	if (Graph)
	{
		for (UEdGraphNode* EdNode : Graph->Nodes)
		{
			UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(EdNode);
			if (!BTNode || BTNode == RootGraphNode)
			{
				continue;
			}
			MainPool.Add(BTNode);
			SubPool.Append(BTNode->Decorators);
			SubPool.Append(BTNode->Services);
		}
	}

	auto Describe = [](const UBehaviorTreeGraphNode* GraphNode)
	{
		const UBTNode* Instance = GraphNode ? Cast<UBTNode>(GraphNode->NodeInstance) : nullptr;
		return Instance ? FNodeScribeAIReader::DescribeBTNode(Instance) : FString();
	};

	TSet<UBehaviorTreeGraphNode*> Used;
	TArray<bool> NeedsNewInstance;
	NeedsNewInstance.Init(false, Entries.Num());

	auto PoolFor = [&](const FBTEntry& Entry) -> TArray<UBehaviorTreeGraphNode*>&
	{
		return Entry.Kind == EBTEntryKind::Node ? MainPool : SubPool;
	};

	// Exact first, over the whole text: a node changed in one line must not
	// take the one another line describes exactly.
	for (FBTEntry& Entry : Entries)
	{
		if (!Entry.Class)
		{
			continue;
		}

		const FString WantedText = SquashSpaces(Entry.Line);
		const FString WantedScratch = Entry.Scratch && Entry.ParamErrors.Num() == 0
			? SquashSpaces(FNodeScribeAIReader::DescribeBTNode(Entry.Scratch)) : FString();

		for (UBehaviorTreeGraphNode* Candidate : PoolFor(Entry))
		{
			if (Used.Contains(Candidate) || !Candidate->NodeInstance
				|| Candidate->NodeInstance->GetClass() != Entry.Class)
			{
				continue;
			}

			const FString Current = SquashSpaces(Describe(Candidate));
			if (Current == WantedText || (!WantedScratch.IsEmpty() && Current == WantedScratch))
			{
				Entry.GraphNode = Candidate;
				Used.Add(Candidate);
				break;
			}
		}
	}

	// What did not match exactly takes its parameters from the text, so they
	// have to have applied. A parameter that does not parse back is forgiven
	// only above, when an existing node already reads exactly as the line.
	for (FBTEntry& Entry : Entries)
	{
		if (Entry.GraphNode || !Entry.Class)
		{
			continue;
		}
		for (const FString& ParamError : Entry.ParamErrors)
		{
			LineError(Entry.LineNumber, ParamError);
		}
	}

	if (Errors.Num() > 0)
	{
		Result.Diagnostics = MoveTemp(Errors);
		Result.Diagnostics.Add(TEXT("[note]: nothing was changed -- the tree is written whole or not at all."));
		return Result;
	}

	// Same class, different parameters: the graph node stays (its place, its
	// comment), the instance becomes the text's. With two of the same class,
	// the one already under the same parent is the one being edited -- taking
	// the first in the graph would report the wrong one as removed. Parents
	// come before their children in the text, so theirs is already settled.
	auto CurrentParent = [](const FBTEntry& Entry, const UBehaviorTreeGraphNode* Candidate) -> const UEdGraphNode*
	{
		return Entry.Kind == EBTEntryKind::Node ? LinkedParent(Candidate) : Candidate->ParentNode.Get();
	};

	for (FBTEntry& Entry : Entries)
	{
		if (Entry.GraphNode)
		{
			continue;
		}

		const UEdGraphNode* WantedParent = Entry.Parent != INDEX_NONE ? Entries[Entry.Parent].GraphNode : nullptr;
		UBehaviorTreeGraphNode* Chosen = nullptr;

		for (UBehaviorTreeGraphNode* Candidate : PoolFor(Entry))
		{
			if (Used.Contains(Candidate) || !Candidate->NodeInstance
				|| Candidate->NodeInstance->GetClass() != Entry.Class)
			{
				continue;
			}
			if (!Chosen)
			{
				Chosen = Candidate;
			}
			if (WantedParent && CurrentParent(Entry, Candidate) == WantedParent)
			{
				Chosen = Candidate;
				break;
			}
		}

		if (Chosen)
		{
			Entry.GraphNode = Chosen;
			Used.Add(Chosen);
			NeedsNewInstance[&Entry - Entries.GetData()] = true;
		}
	}

	// -- 5. Writing ----------------------------------------------------------

	const FScopedTransaction Transaction(
		LOCTEXT("WriteBehaviorTreeTransaction", "NodeScribe: write behavior tree"));

	Tree->Modify();

	if (!Graph)
	{
		// A tree that was never opened in the editor has no graph yet -- the
		// common case right after create_asset. Same steps as the editor's.
		Tree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(Tree, NAME_None,
			UBehaviorTreeGraph::StaticClass(), GetDefault<UBehaviorTreeGraph>()->Schema);
		Graph = Cast<UBehaviorTreeGraph>(Tree->BTGraph);
		Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		Graph->OnCreated();
		Graph->Initialize();

		for (UEdGraphNode* EdNode : Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(EdNode))
			{
				RootGraphNode = Root;
			}
		}
	}

	if (!RootGraphNode)
	{
		Result.Diagnostics.Add(TEXT("[error]: the tree's graph has no root node -- open the tree in the editor once and try again."));
		return Result;
	}

	Graph->Modify();
	Graph->LockUpdates();

	int32 Kept = 0;
	int32 Changed = 0;
	int32 Created = 0;

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FBTEntry& Entry = Entries[Index];

		if (Entry.GraphNode && !NeedsNewInstance[Index])
		{
			++Kept;
			Entry.Scratch->MarkAsGarbage();
			continue;
		}

		AdoptInstance(Tree, Entry.Scratch);

		if (Entry.GraphNode)
		{
			Entry.GraphNode->Modify();
			Entry.GraphNode->NodeInstance = Entry.Scratch;
			Entry.GraphNode->InitializeInstance();
			++Changed;
			continue;
		}

		Entry.GraphNode = SpawnGraphNode(Graph, Entry.Scratch, Entry.Kind != EBTEntryKind::Node);
		++Created;
	}

	// Does the shape differ? A tree whose shape and order are already the
	// text's keeps the layout someone arranged by hand.
	bool bShapeChanged = Created > 0;

	auto WantedChildren = [&Entries](const TArray<int32>& Children)
	{
		TArray<UEdGraphNode*> Nodes;
		for (const int32 Child : Children)
		{
			Nodes.Add(Entries[Child].GraphNode);
		}
		return Nodes;
	};

	if (!bShapeChanged)
	{
		bShapeChanged = LinkedChildren(RootGraphNode) != TArray<UEdGraphNode*>{ Entries[RootEntry].GraphNode };
	}
	for (const FBTEntry& Entry : Entries)
	{
		if (bShapeChanged)
		{
			break;
		}
		if (Entry.Kind != EBTEntryKind::Node)
		{
			continue;
		}

		bShapeChanged = LinkedChildren(Entry.GraphNode) != WantedChildren(Entry.Children);

		TArray<UEdGraphNode*> WantedSubs;
		TArray<UEdGraphNode*> CurrentSubs;
		for (const int32 Sub : Entry.Decorators)
		{
			WantedSubs.Add(Entries[Sub].GraphNode);
		}
		for (const int32 Sub : Entry.Services)
		{
			WantedSubs.Add(Entries[Sub].GraphNode);
		}
		CurrentSubs.Append(Entry.GraphNode->Decorators);
		CurrentSubs.Append(Entry.GraphNode->Services);
		bShapeChanged = bShapeChanged || CurrentSubs != WantedSubs;
	}
	for (UBehaviorTreeGraphNode* Leftover : MainPool)
	{
		bShapeChanged = bShapeChanged || !Used.Contains(Leftover);
	}

	TArray<FString> Removed;

	if (bShapeChanged)
	{
		// What the text does not mention leaves -- named in the return, and
		// Ctrl+Z brings it back.
		for (UBehaviorTreeGraphNode* Sub : SubPool)
		{
			if (!Used.Contains(Sub))
			{
				Removed.Add(TEXT("`") + Describe(Sub) + TEXT("`"));
				Sub->Modify();
				Sub->ParentNode = nullptr;
				Sub->DestroyNode();
			}
		}
		for (UBehaviorTreeGraphNode* Main : MainPool)
		{
			if (!Used.Contains(Main))
			{
				Removed.Add(TEXT("`") + Describe(Main) + TEXT("`"));
				Main->Modify();
				Main->BreakAllNodeLinks();
				Main->RemoveAllSubNodes();
				Main->DestroyNode();
			}
		}

		RootGraphNode->Modify();
		RootGraphNode->BreakAllNodeLinks();

		for (FBTEntry& Entry : Entries)
		{
			if (Entry.Kind != EBTEntryKind::Node)
			{
				continue;
			}
			Entry.GraphNode->Modify();
			Entry.GraphNode->BreakAllNodeLinks();
			Entry.GraphNode->RemoveAllSubNodes();
		}

		for (FBTEntry& Entry : Entries)
		{
			if (Entry.Kind != EBTEntryKind::Node)
			{
				continue;
			}

			for (const int32 Sub : Entry.Decorators)
			{
				UBehaviorTreeGraphNode* SubNode = Entries[Sub].GraphNode;
				SubNode->Modify();
				SubNode->ParentNode = Entry.GraphNode;
				Entry.GraphNode->SubNodes.Add(SubNode);
				Entry.GraphNode->OnSubNodeAdded(SubNode);
			}
			for (const int32 Sub : Entry.Services)
			{
				UBehaviorTreeGraphNode* SubNode = Entries[Sub].GraphNode;
				SubNode->Modify();
				SubNode->ParentNode = Entry.GraphNode;
				Entry.GraphNode->SubNodes.Add(SubNode);
				Entry.GraphNode->OnSubNodeAdded(SubNode);
			}

			for (const int32 Child : Entry.Children)
			{
				Entry.GraphNode->GetOutputPin()->MakeLinkTo(Entries[Child].GraphNode->GetInputPin());
			}
		}

		RootGraphNode->GetOutputPin()->MakeLinkTo(Entries[RootEntry].GraphNode->GetInputPin());

		int32 NextLeafX = 0;
		const int32 TopX = LayoutEntry(Entries, RootEntry, RootGraphNode->NodePosY + 200, NextLeafX);
		ShiftEntry(Entries, RootEntry, RootGraphNode->NodePosX - TopX);
	}

	Graph->UnlockUpdates();

	if (bShapeChanged || Changed > 0)
	{
		Graph->UpdateAsset();
		Graph->NotifyGraphChanged();

		// Without this the asset stays changed in memory and clean on disk.
		Tree->MarkPackageDirty();
	}

	Result.Applied = Changed + Created + (bShapeChanged ? 1 : 0);
	Result.Diagnostics.Add(FString::Printf(
		TEXT("[note]: %d node(s) kept as they were, %d with new parameters, %d created%s."),
		Kept, Changed, Created, bShapeChanged ? TEXT("; links and layout rebuilt") : TEXT("")));
	if (Removed.Num() > 0)
	{
		Result.Diagnostics.Add(FString::Printf(
			TEXT("[note]: removed, because the text does not mention them: %s. Ctrl+Z brings them back."),
			*FString::Join(Removed, TEXT(", "))));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
