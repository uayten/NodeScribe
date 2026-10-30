#include "NodeScribeBuilder.h"

#include "NodeScribeAnimGraph.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeParser.h"
#include "NodeScribePropertyText.h"
#include "NodeScribeReader.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "Animation/AnimBlueprint.h"
#include "K2Node_AnimGetter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Message.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GenericToText.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableSet.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/Subsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

using namespace NodeScribePropertyText;

namespace
{
	/**
	 * A name someone wrote that the plugin matched to nothing, one per line, in
	 * a single file.
	 *
	 * **It is not an error log.** The error already comes back in the call's
	 * return value, already goes to the Message Log and already stays as a red
	 * comment inside the graph -- which is where it is useful, next to the
	 * problem, and where `read_graph` itself brings it back from. Duplicating
	 * that would be a poorer copy.
	 *
	 * This answers another question, one that cannot be answered any other way
	 * today: **which aliases are missing from the catalog**. After a few weeks of
	 * use, `sort | uniq -c | sort -rn` on this file is a to-do list sorted by
	 * frequency -- "you wrote `Delay` seven times and the catalog found something
	 * else".
	 *
	 * There is no date on the line, on purpose: a repeat has to come out as an
	 * *identical* line, otherwise the count does not exist, and the order of the
	 * file is already chronological. For the same reason, errors that are not
	 * about vocabulary do not go in here -- incompatible type, empty pin,
	 * duplicate event. Those have a known cause, and would become noise in a
	 * count that exists to find patterns.
	 */
	void RecordUnresolvedName(const TCHAR* Kind, const FString& Name, const FString& Context = FString())
	{
		FString Line = FString(Kind) + TEXT("  ") + Name.TrimStartAndEnd();

		if (!Context.IsEmpty())
		{
			Line += TEXT("  in  ") + Context.TrimStartAndEnd();
		}

		Line += LINE_TERMINATOR;

		const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"));
		IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);

		FFileHelper::SaveStringToFile(
			Line,
			*FPaths::Combine(Folder, TEXT("vocabulary.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

	/** Horizontal spacing between nodes of the same execution chain. */
	constexpr int32 ColumnWidth = 620;

	/** Distance from the execution row to the first data node, below it. */
	constexpr int32 DataRowOffsetY = 200;

	/** Each data node gets its own row, stacking downwards. */
	constexpr int32 DataRowHeight = 170;

	/** The data stack sits a little to the left of the execution column. */
	constexpr int32 DataColumnOffsetX = -40;

	/** The deeper in the data chain, the further to the left. */
	constexpr int32 DataDepthIndentX = 30;

	/** Vertical spacing between sibling blocks (the branches of a Branch). */
	constexpr int32 BranchRowHeight = 300;

	/** The Engine's macro libraries, in lookup order. */
	const TCHAR* const MacroLibraryPaths[] = {
		TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"),
		TEXT("/Engine/EditorBlueprintResources/ActorMacros.ActorMacros"),
		TEXT("/Engine/EditorBlueprintResources/ActorComponentMacros.ActorComponentMacros")
	};

	/** true for what shows up in the graph's list of overridable events. */
	bool IsOverridableEvent(const UFunction* Function)
	{
		// A BlueprintNativeEvent with a return value is a function, not an event: it has no exec pin.
		return Function
			&& Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)
			&& Function->GetReturnProperty() == nullptr;
	}

	/**
	 * Looks for a Blueprint event with this name in any loaded class.
	 *
	 * Separates two cases that deserve very different treatment: "you invented
	 * a new event" (legitimate, it is what `event MyAbilityActivated` does) and
	 * "this event exists, but not in this class" -- `BeginPlay` in a Widget, for
	 * example. The second is almost always a mistake, and the resulting Custom
	 * Event compiles without complaint and never fires.
	 */
	UClass* FindClassOwningBlueprintEvent(const FString& EventName)
	{
		const TArray<FString> Attempts = {
			EventName,
			FString(TEXT("Receive")) + EventName,
			FString(TEXT("K2_")) + EventName
		};

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			// SKEL_/REINST_/TRASHCLASS_ are leftovers of Blueprint compilation.
			// Without this filter the warning said things like "`FillList` is an
			// event of `REINST_SKEL_WBP_RemapScreen_C_21`", which is not a name
			// that exists for the user.
			const FString ClassName = ClassIt->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_"))
				|| ClassName.StartsWith(TEXT("REINST_"))
				|| ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

			for (const FString& Attempt : Attempts)
			{
				const UFunction* Found = ClassIt->FindFunctionByName(FName(*Attempt), EIncludeSuperFlag::ExcludeSuper);
				if (IsOverridableEvent(Found))
				{
					return *ClassIt;
				}
			}
		}

		return nullptr;
	}

	/** The events the class really offers, so the user can pick another one. */
	FString ListAvailableEvents(UClass* Class, int32 MaxCount)
	{
		if (!Class)
		{
			return FString();
		}

		TArray<FString> Names;
		for (TFieldIterator<UFunction> FunctionIt(Class); FunctionIt; ++FunctionIt)
		{
			if (IsOverridableEvent(*FunctionIt))
			{
				Names.AddUnique(FNodeScribeCatalog::StripEventPrefix(FunctionIt->GetName()));
			}
		}

		if (Names.Num() == 0)
		{
			return FString();
		}

		Names.Sort();

		const bool bTruncated = Names.Num() > MaxCount;
		if (bTruncated)
		{
			Names.SetNum(MaxCount);
		}

		return FString::Join(Names, TEXT(", ")) + (bTruncated ? TEXT(", ...") : TEXT(""));
	}

	/**
	 * Nodes that build an object from a class.
	 *
	 * Their pins depend on the value of the `Class` pin: fields marked as
	 * *Expose on Spawn* only show up after the class is chosen. That is why this
	 * is the only kind of node where the order of the arguments matters.
	 *
	 * Resolved by path, not by include: `K2Node_CreateWidget` lives in the
	 * Private folder of UMGEditor, and linking that whole module for the sake of
	 * a class pointer would be too expensive.
	 */
	struct FConstructNodeForm
	{
		const TCHAR* Name;
		const TCHAR* NodeClassPath;
	};

	const FConstructNodeForm ConstructNodeForms[] = {
		{ TEXT("createwidget"),            TEXT("/Script/UMGEditor.K2Node_CreateWidget") },
		{ TEXT("spawnactorfromclass"),     TEXT("/Script/BlueprintGraph.K2Node_SpawnActorFromClass") },
		{ TEXT("spawnactor"),              TEXT("/Script/BlueprintGraph.K2Node_SpawnActorFromClass") },
		{ TEXT("constructobjectfromclass"),TEXT("/Script/BlueprintGraph.K2Node_ConstructObjectFromClass") }
	};

	UClass* FindConstructNodeClass(const FString& NormalizedExpression)
	{
		for (const FConstructNodeForm& Form : ConstructNodeForms)
		{
			if (NormalizedExpression == Form.Name)
			{
				return FindObject<UClass>(nullptr, Form.NodeClassPath);
			}
		}

		return nullptr;
	}

	/**
	 * Asset of any type, by full path or by short name.
	 *
	 * The short name only reaches what is already loaded -- enough for the
	 * asset you just opened, and that is why the convenience is worth it. When
	 * it fails, the caller says to use the path, which always works.
	 */
	UObject* FindAssetByPathOrName(const TCHAR* ClassPath, const FString& Value)
	{
		if (Value.StartsWith(TEXT("/")))
		{
			return LoadObject<UObject>(nullptr, *Value);
		}

		UClass* AssetClass = FindObject<UClass>(nullptr, ClassPath);
		if (!AssetClass)
		{
			return nullptr;
		}

		const FString Normalized = FNodeScribeCatalog::Normalize(Value);

		for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
		{
			UObject* Candidate = *ObjectIt;
			if (Candidate->IsA(AssetClass)
				&& FNodeScribeCatalog::Normalize(Candidate->GetName()) == Normalized)
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	/**
	 * Dispatcher by tolerant name.
	 *
	 * A dispatcher's name is typed by hand and accepts spaces -- including at
	 * the end, where nobody sees them. The parser trims the line's spaces, so an
	 * exact lookup would never find `OnHealthChanged ` from `Call OnHealthChanged`.
	 */
	FMulticastDelegateProperty* FindDelegateByFriendlyName(UClass* Class, const FString& Name)
	{
		if (!Class || Name.IsEmpty())
		{
			return nullptr;
		}

		if (FMulticastDelegateProperty* Exact = FindFProperty<FMulticastDelegateProperty>(Class, FName(*Name)))
		{
			return Exact;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		for (TFieldIterator<FMulticastDelegateProperty> It(Class); It; ++It)
		{
			if (FNodeScribeCatalog::Normalize(It->GetName()) == Wanted)
			{
				return *It;
			}
		}

		return nullptr;
	}

	/**
	 * Hand-written type name -> Unreal pin type.
	 *
	 * Accepts what shows up in the interface (`Float`, `Timer Handle`,
	 * `EPlayerMappableKeySlot`), because that is what the user sees -- and it is
	 * also what the reader writes on the way back.
	 */
	UScriptStruct* FindStructByFriendlyName(const FString& Name);
	UEnum* FindEnumByFriendlyName(const FString& Name);
	UClass* FindClassByFriendlyNameInternal(const FString& Name);

	bool ResolvePinTypeFromNameInternal(const FString& InTypeName, FEdGraphPinType& OutType)
	{
		FString TypeName = InTypeName.TrimStartAndEnd();
		bool bIsArray = false;

		// `BP_Rock Class` is the *class*, not an instance of it -- it is what the
		// interface shows and what is used to spawn an actor or point at an
		// ability. Without this, only references to live objects could be
		// declared, and a class variable had to be created by hand.
		bool bIsClassReference = false;
		if (TypeName.EndsWith(TEXT(" Class"), ESearchCase::IgnoreCase))
		{
			TypeName = TypeName.LeftChop(6).TrimEnd();
			bIsClassReference = true;
		}

		// Map: `Map of Int64 to BP_Mirror`. Both halves are resolved by the same
		// function, so the usual type names apply.
		//
		// Comes before the array because a map key is never a container: if the
		// line says `Map of Array of X to Y`, that is an error, and the
		// `return false` down here is what makes it be said out loud.
		//
		// Only `Map of ` opens a map. A bare `Map ` prefix would swallow a struct
		// like `Map Player Key Args` and then fail for lack of ` to `.
		if (TypeName.StartsWith(TEXT("Map of "), ESearchCase::IgnoreCase))
		{
			const FString Rest = TypeName.RightChop(7).TrimStartAndEnd();

			FString KeyName;
			FString ValueName;
			if (!Rest.Split(TEXT(" to "), &KeyName, &ValueName, ESearchCase::IgnoreCase))
			{
				return false;
			}

			FEdGraphPinType KeyType;
			FEdGraphPinType ValueType;
			if (!ResolvePinTypeFromNameInternal(KeyName.TrimStartAndEnd(), KeyType)
				|| !ResolvePinTypeFromNameInternal(ValueName.TrimStartAndEnd(), ValueType))
			{
				return false;
			}

			if (KeyType.ContainerType != EPinContainerType::None
				|| ValueType.ContainerType != EPinContainerType::None)
			{
				return false;
			}

			OutType = KeyType;
			OutType.ContainerType = EPinContainerType::Map;
			OutType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
			return true;
		}

		bool bIsSet = false;
		if (TypeName.StartsWith(TEXT("Set of "), ESearchCase::IgnoreCase))
		{
			TypeName = TypeName.RightChop(7).TrimStartAndEnd();
			bIsSet = true;
		}

		if (TypeName.StartsWith(TEXT("Array of "), ESearchCase::IgnoreCase))
		{
			TypeName = TypeName.RightChop(9).TrimStartAndEnd();
			bIsArray = true;
		}

		static const TMap<FString, FName> Primitives = {
			{ TEXT("bool"),     UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("boolean"),  UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("int"),      UEdGraphSchema_K2::PC_Int },
			{ TEXT("integer"),  UEdGraphSchema_K2::PC_Int },
			{ TEXT("int64"),    UEdGraphSchema_K2::PC_Int64 },
			{ TEXT("byte"),     UEdGraphSchema_K2::PC_Byte },
			{ TEXT("float"),    UEdGraphSchema_K2::PC_Real },
			{ TEXT("double"),   UEdGraphSchema_K2::PC_Real },
			{ TEXT("real"),     UEdGraphSchema_K2::PC_Real },
			{ TEXT("string"),   UEdGraphSchema_K2::PC_String },
			{ TEXT("name"),     UEdGraphSchema_K2::PC_Name },
			{ TEXT("text"),     UEdGraphSchema_K2::PC_Text }
		};

		OutType = FEdGraphPinType();

		const FString Normalized = FNodeScribeCatalog::Normalize(TypeName);

		// A struct that matched only by its *display name* loses to a class with
		// an exact name.
		//
		// `FTypedElementActorTag` is declared `USTRUCT(meta = (DisplayName =
		// "Actor"))`, and the struct lookup comes before the class lookup: without
		// this rule, `variable X : Actor` created that struct. It compiles, looks
		// right in the panel, and is not an Actor -- the worst possible result.
		//
		// Only the display name yields. `Vector` and `TimerHandle` match by the
		// struct's internal name and keep winning over any class of the same
		// name, which is the behaviour that already worked. And the class lookup
		// only knows internal names (and the Blueprint `_C`), so "class with an
		// exact name" does not open a second display-name door.
		UScriptStruct* Struct = FindStructByFriendlyName(TypeName);
		if (Struct
			&& FNodeScribeCatalog::Normalize(Struct->GetName()) != Normalized
			&& FindClassByFriendlyNameInternal(TypeName) != nullptr)
		{
			Struct = nullptr;
		}

		if (const FName* Category = Primitives.Find(Normalized))
		{
			OutType.PinCategory = *Category;

			if (*Category == UEdGraphSchema_K2::PC_Real)
			{
				OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}
		}
		else if (Struct)
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = Struct;
		}
		else if (UEnum* Enum = FindEnumByFriendlyName(TypeName))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutType.PinSubCategoryObject = Enum;
		}
		else if (UClass* Class = FindClassByFriendlyNameInternal(TypeName))
		{
			OutType.PinCategory = bIsClassReference
				? UEdGraphSchema_K2::PC_Class
				: UEdGraphSchema_K2::PC_Object;
			OutType.PinSubCategoryObject = Class;
		}
		else
		{
			return false;
		}

		// `Float Class` means nothing: if the suffix was eaten and what is left
		// is not a class, the whole name was wrong.
		if (bIsClassReference && OutType.PinCategory != UEdGraphSchema_K2::PC_Class)
		{
			return false;
		}

		if (bIsArray)
		{
			OutType.ContainerType = EPinContainerType::Array;
		}
		else if (bIsSet)
		{
			OutType.ContainerType = EPinContainerType::Set;
		}

		return true;
	}

	/** Struct lookup by name, accepting `MapPlayerKeyArgs` and `Map Player Key Args`. */
	UScriptStruct* FindStructByFriendlyName(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		if (Normalized.IsEmpty())
		{
			return nullptr;
		}

		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			UScriptStruct* Struct = *StructIt;

			if (FNodeScribeCatalog::Normalize(Struct->GetName()) == Normalized
				|| FNodeScribeCatalog::Normalize(Struct->GetDisplayNameText().ToString()) == Normalized)
			{
				return Struct;
			}
		}

		return nullptr;
	}

	/**
	 * The Get node changes depending on where the subsystem lives: a LocalPlayer
	 * one needs the PlayerController, an Engine one needs nothing. Picking wrong
	 * gives a node that does not even compile.
	 */
	UClass* ChooseSubsystemNodeClass(UClass* SubsystemClass)
	{
		if (SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
		{
			return UK2Node_GetSubsystemFromPC::StaticClass();
		}

		if (SubsystemClass->IsChildOf(UEngineSubsystem::StaticClass()))
		{
			return UK2Node_GetEngineSubsystem::StaticClass();
		}

		// UEditorSubsystem lives in a module this plugin does not link; the class
		// is looked up by path so the dependency is not created just for this.
		static const UClass* EditorSubsystemClass =
			FindObject<UClass>(nullptr, TEXT("/Script/EditorSubsystem.EditorSubsystem"));

		if (EditorSubsystemClass && SubsystemClass->IsChildOf(EditorSubsystemClass))
		{
			return UK2Node_GetEditorSubsystem::StaticClass();
		}

		return UK2Node_GetSubsystem::StaticClass();
	}

	UEnum* FindEnumByFriendlyName(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		if (Normalized.IsEmpty())
		{
			return nullptr;
		}

		for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
		{
			if (FNodeScribeCatalog::Normalize(EnumIt->GetName()) == Normalized)
			{
				return *EnumIt;
			}
		}

		return nullptr;
	}

	/** Class lookup by short name, accepting both `BP_Boss` and `BP_Boss_C`. */
	UClass* FindClassByFriendlyNameInternal(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		const FString NormalizedWithSuffix = Normalized + TEXT("c");

		UClass* Fallback = nullptr;

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (Class->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				continue;
			}

			const FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")) || ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

			const FString NormalizedClassName = FNodeScribeCatalog::Normalize(ClassName);

			if (NormalizedClassName == Normalized)
			{
				return Class;
			}

			// `BP_Boss` typed by the user points at the generated class `BP_Boss_C`.
			if (NormalizedClassName == NormalizedWithSuffix && !Fallback)
			{
				Fallback = Class;
			}
		}

		return Fallback;
	}

	/**
	 * Finds a macro graph of the Engine's macro libraries (ForEachLoop, DoOnce,
	 * Gate, Switch Has Authority...).
	 *
	 * There are three libraries, not one. `StandardMacros` serves any
	 * Blueprint; `ActorMacros` and `ActorComponentMacros` only serve Blueprints
	 * of that parent -- it is where `Switch Has Authority` lives, and looking only
	 * at the first answered "no node called `Switch Has Authority`" for a macro
	 * the editor's menu offers. The parent check is the same one the menu makes:
	 * a macro from a library the class does not derive from does not compile.
	 */
	UEdGraph* FindStandardMacroGraph(const FString& Query, const UClass* SelfClass)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Query);

		for (const TCHAR* LibraryPath : MacroLibraryPaths)
		{
			UBlueprint* MacroLibrary = LoadObject<UBlueprint>(nullptr, LibraryPath);
			if (!MacroLibrary)
			{
				continue;
			}

			const UClass* LibraryParent = MacroLibrary->ParentClass;
			if (LibraryParent && LibraryParent != UObject::StaticClass()
				&& !(SelfClass && SelfClass->IsChildOf(LibraryParent)))
			{
				continue;
			}

			for (UEdGraph* MacroGraph : MacroLibrary->MacroGraphs)
			{
				if (MacroGraph && FNodeScribeCatalog::Normalize(MacroGraph->GetName()) == Normalized)
				{
					return MacroGraph;
				}
			}
		}

		return nullptr;
	}

	/**
	 * Aliases for execution output labels.
	 * Maps to the pin's internal name, which is almost never what shows on
	 * screen: the "True" pin of a Branch is called, internally, "then".
	 */
	FString ResolveLabelAlias(const FString& Label)
	{
		static const TMap<FString, FString> Aliases = {
			{ TEXT("true"),       TEXT("then") },
			{ TEXT("then"),       TEXT("then") },
			{ TEXT("false"),      TEXT("else") },
			{ TEXT("else"),       TEXT("else") },
			{ TEXT("loop"),       TEXT("loopbody") },
			{ TEXT("loopbody"),   TEXT("loopbody") },
			{ TEXT("completed"),  TEXT("completed") },
		};

		const FString Normalized = FNodeScribeCatalog::Normalize(Label);
		if (const FString* Found = Aliases.Find(Normalized))
		{
			return *Found;
		}

		return Normalized;
	}

	/** Aliases for input pin names. */
	FString ResolvePinAlias(const FString& PinName)
	{
		static const TMap<FString, FString> Aliases = {
			{ TEXT("target"),    TEXT("self") },
			{ TEXT("self"),      TEXT("self") },
			{ TEXT("condition"), TEXT("condition") },
		};

		const FString Normalized = FNodeScribeCatalog::Normalize(PinName);
		if (const FString* Found = Aliases.Find(Normalized))
		{
			return *Found;
		}

		return Normalized;
	}
}

/**
 * Stable reference to a pin.
 *
 * Keeping a raw `UEdGraphPin*` does not work: linking a pin makes the node
 * notify its neighbours, and some K2Nodes (wildcard, cast) rebuild themselves
 * at that moment, destroying the old pins. A pointer kept from an earlier line
 * would become an access to freed memory. We keep node + name and resolve on
 * demand.
 */
struct FPinRef
{
	TWeakObjectPtr<UEdGraphNode> Node;
	FName PinName;
	EEdGraphPinDirection Direction = EGPD_Output;

	FPinRef() = default;

	explicit FPinRef(UEdGraphPin* Pin)
	{
		if (Pin && Pin->GetOwningNodeUnchecked())
		{
			Node = Pin->GetOwningNode();
			PinName = Pin->PinName;
			Direction = Pin->Direction;
		}
	}

	bool IsSet() const { return Node.IsValid() && !PinName.IsNone(); }

	UEdGraphPin* Resolve() const
	{
		UEdGraphNode* OwningNode = Node.Get();
		return OwningNode ? OwningNode->FindPin(PinName, Direction) : nullptr;
	}
};

namespace
{
	const UEdGraphSchema_K2* ResolveK2Schema(const UEdGraph* Graph)
	{
		const UEdGraphSchema_K2* Found = Graph ? Cast<UEdGraphSchema_K2>(Graph->GetSchema()) : nullptr;
		return Found ? Found : GetDefault<UEdGraphSchema_K2>();
	}

	/**
	 * true when the text's name is this struct field.
	 *
	 * The field is written the way the split pin shows it: `Is Supported` for
	 * `bIsSupported`. Comparing only the C++ name refused exactly the name the
	 * reader had just written for the neighbouring field.
	 */
	bool FieldMatches(const FProperty* Field, const FString& Wanted)
	{
		const FString Name = Field->GetName();
		if (FNodeScribeCatalog::Normalize(Name) == Wanted
			|| FNodeScribeCatalog::Normalize(Field->GetAuthoredName()) == Wanted
			|| FNodeScribeCatalog::Normalize(Field->GetDisplayNameText().ToString()) == Wanted)
		{
			return true;
		}

		return Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1])
			&& FNodeScribeCatalog::Normalize(Name.RightChop(1)) == Wanted;
	}

	/**
	 * true when this sub-pin of a split struct is the field the text names.
	 *
	 * A sub-pin is born as `<parent>_<field>` -- `ReturnValue_bIsSupported` --,
	 * so the field is what follows the parent's name, and the struct says what
	 * that field is called on screen.
	 */
	bool SubPinMatches(const UEdGraphPin* Parent, const UEdGraphPin* Sub, const FString& Wanted)
	{
		FString FieldName = Sub->PinName.ToString();
		if (!FieldName.RemoveFromStart(Parent->PinName.ToString() + TEXT("_")))
		{
			return FNodeScribeCatalog::Normalize(FieldName) == Wanted;
		}

		const UScriptStruct* Struct = Cast<UScriptStruct>(Parent->PinType.PinSubCategoryObject.Get());
		const FProperty* Field = Struct ? Struct->FindPropertyByName(FName(*FieldName)) : nullptr;

		return Field ? FieldMatches(Field, Wanted)
			: FNodeScribeCatalog::Normalize(FieldName) == Wanted;
	}
}

/** State of one transcription. Lives only during Build(). */
class FNodeScribeBuildContext
{
public:
	FNodeScribeBuildContext(UEdGraph* InGraph, UBlueprint* InBlueprint, const FVector2D& InOrigin)
		: Graph(InGraph)
		, Blueprint(InBlueprint)
		, Origin(InOrigin)
		// The graph's own schema, not the generic K2 one: it is the
		// `UAnimationGraphSchema` that knows how to link poses and inserts the
		// local/component space conversion by itself. For EventGraph and
		// functions the graph's schema is already K2, so nothing changes there.
		, Schema(ResolveK2Schema(InGraph))
		, bAnimGraph(NodeScribeAnimGraph::IsAnimationGraph(InGraph))
		// A transition rule goes through the same schema, but has no pose at
		// all: it is boolean logic. Without this distinction the anim vocabulary
		// would compete there with the name of a regular function, and win.
		, bPoseGraph(NodeScribeAnimGraph::IsAnimationGraph(InGraph)
			&& !(InGraph && InGraph->IsA<UAnimationTransitionGraph>()))
		// A transition rule has a vocabulary that only exists there: the state
		// machine getters (`Time Remaining`, `Get Transition Time Elapsed`).
		// They are not function calls -- see TryCreateAnimGetter.
		, bTransitionGraph(InGraph && InGraph->IsA<UAnimationTransitionGraph>())
	{
	}

	void Run(const TArray<FNodeScribeStatement>& Statements);

	FNodeScribeBuilder::FResult Result;

private:
	/** One nesting level: the body of a branch, or the root level. */
	struct FFrame
	{
		int32 Indent = 0;

		/** Where the next flow link comes out of. Invalid = interrupted chain. */
		FPinRef PendingExec;

		/**
		 * The pose input pin this block feeds, in the AnimGraph.
		 *
		 * Indentation there opens an *input* -- `True Pose:` of a blend --, and
		 * what fills that input is the result of the block, that is, its last
		 * node. Instead of keeping the link for the end, each node links over
		 * the previous one: the pin only accepts one wire, so what is left at the
		 * end is exactly the last one. Invalid in the EventGraph and at the root.
		 */
		FPinRef FlowSink;

		/** Last node created at this level, owner of the labels that follow. */
		UEdGraphNode* LastNode = nullptr;

		int32 BaseX = 0;
		int32 BaseY = 0;
		int32 Column = 0;

		/** How many branches were already opened from LastNode, to stack in Y. */
		int32 BranchesOpened = 0;

		/**
		 * The block starts on a pin of a node already in the graph -- an `after`,
		 * or a label under one -- and that pin already led somewhere.
		 *
		 * Linking the block's first node takes that wire's place, so whatever
		 * followed would be cut loose. Kept here, it is linked again to the
		 * block's last node when the block closes: that is what makes the block
		 * an insertion instead of a replacement.
		 */
		FPinRef OriginPin;
		TArray<FPinRef> Displaced;

		/** The line that opened the block, for the message when it cannot be relinked. */
		int32 OpenedAtLine = 0;
	};

	/** Keeps what OriginPin leads to now, before the block takes its place. */
	static void CaptureDisplaced(FFrame& Frame, UEdGraphPin* Origin, int32 Line);

	/** Closes the innermost block, relinking what the insertion displaced. */
	void PopFrame();

	/**
	 * The first Y below everything this text placed so far, data stacks included.
	 * MIN_int32 when nothing was placed yet: callers keep the larger of this and
	 * their own row.
	 */
	int32 NextFreeRowY();

	/** Moves the frame to a row of its own when the node cannot stay on the current one. */
	void OpenRowIfNeeded(FFrame& Frame, UEdGraphNode* Node);

	// --- Insertion into an existing chain (`after`) ---------------------------

	/**
	 * Finds every `after` node before anything is created.
	 *
	 * All of them or none: when one does not resolve, the write stops before
	 * its first node. Half an insertion -- the nodes in, the wire nowhere -- is
	 * a graph nobody wrote and nobody would notice until playtest.
	 */
	bool ResolveAnchors(const TArray<FNodeScribeStatement>& Statements);

	/** The node each `after` line found, by line number. */
	TMap<int32, UEdGraphNode*> AnchorNodes;

	/**
	 * The names the graph's own reading gives its nodes, for `$name` in the
	 * lines of an `after` block. Only filled when the text has one: those
	 * names come from a reading the assistant has just seen. A name the text
	 * defines itself wins over these.
	 */
	TMap<FString, UEdGraphNode*> ExistingNames;

	// --- Node creation ----------------------------------------------------

	/**
	 * RF_Transactional is what makes the node enter the undo system.
	 *
	 * Without it, `FBlueprintEditorUtils::UpdateTransactionalFlags` fixes the
	 * node every time the Blueprint opens and marks the asset dirty -- that is
	 * where the "was updated to fix issues detected on load. Please resave."
	 * that kept coming back after compiling and saving came from.
	 */
	template <typename TNode>
	TNode* AllocateNode(UEdGraph* Target = nullptr)
	{
		// The destination is almost always the paste's graph; the parameter
		// exists for the state machine, whose states are born in its sub-graph.
		UEdGraph* Destination = Target ? Target : Graph;

		TNode* Node = NewObject<TNode>(Destination, NAME_None, RF_Transactional);
		Destination->AddNode(Node, false, false);
		Node->CreateNewGuid();
		return Node;
	}

	/**
	 * Pins first, then the placement hook -- the order of the Blueprint
	 * action menu (`UBlueprintNodeSpawner`), not the generic schema's. In 5.8
	 * Spawn Actor from Class looks up its Scale Method pin inside
	 * PostPlacedNewNode, and the other order brought the editor down.
	 */
	static void FinalizeNode(UEdGraphNode* Node)
	{
		Node->AllocateDefaultPins();
		Node->PostPlacedNewNode();
	}

	UEdGraphNode* CreateNodeForStatement(const FNodeScribeStatement& Statement);
	UEdGraphNode* TryCreateSpecialNode(const FNodeScribeStatement& Statement, bool& bOutHandled);
	UEdGraphNode* CreateErrorComment(const FNodeScribeStatement& Statement, const FString& Reason);

	/** Creates the variable of a `variable Name : Type = value` line. */
	void CreateDeclaredVariable(const FNodeScribeStatement& Statement);

	/**
	 * Creates every Custom Event before processing any line.
	 *
	 * Without this, calling an event defined further down in the text fails:
	 * the class only knows the function after the node exists and the skeleton
	 * is regenerated. In the graph the order does not matter, and in the text it
	 * should not matter either.
	 */
	void PreCreateCustomEvents(const TArray<FNodeScribeStatement>& Statements);

	/** Events already created by the pre-pass, by name. */
	TMap<FString, UEdGraphNode*> PreCreatedEvents;

	/** Arguments applied while creating the node, which ApplyArguments skips. */
	TSet<const FNodeScribeArg*> ArgsAppliedEarly;

	/**
	 * An event with the same identity already in the graph.
	 *
	 * Unreal allows one node per event: two `BeginPlay`s, or two events of the
	 * same dispatcher, leave the Blueprint unable to compile. Pasting over a
	 * graph that already has the event is the easiest way to fall into that.
	 */
	UEdGraphNode* FindExistingEvent(const TFunctionRef<bool(UEdGraphNode*)>& Matches) const;

	UEdGraphNode* CreateDuplicateEventComment(const FNodeScribeStatement& Statement, const FString& EventLabel);

	/**
	 * An event the text asks for that is already in the graph.
	 *
	 * With no wire on any pin it is reused: the reader leaves exactly those out
	 * (the disabled BeginPlay a new Blueprint is born with is the common case),
	 * so refusing them would make a reading that shows no BeginPlay unable to
	 * take one, with nothing in the text saying why. With a wire, the existing
	 * event carries behaviour, and the duplicate guard stands.
	 */
	UEdGraphNode* ReuseEmptyEventOrRefuse(const FNodeScribeStatement& Statement, UEdGraphNode* Existing, const FString& EventLabel);

	/**
	 * The function an `event X` line implements when it is not a Custom Event:
	 * an event of the parent class, or of an interface this Blueprint
	 * implements. Null when there is none -- or when two interfaces offer the
	 * name: then OutCandidates lists them, and none is chosen.
	 */
	UFunction* FindEventToImplement(const FString& EventName, TArray<FString>& OutCandidates) const;

	/**
	 * The types of the parameters an event line declares. false when a type is
	 * unknown or a name repeats; the error is said only with bReportErrors, so
	 * the pre-pass can leave the saying to the line itself.
	 */
	bool ResolveDeclaredEventParams(const FNodeScribeStatement& Statement,
		TArray<TPair<FName, FEdGraphPinType>>& OutParams, bool bReportErrors);

	/** A new Custom Event with the parameters its line declares. */
	UK2Node_CustomEvent* CreateCustomEvent(const FName& Name, const TArray<TPair<FName, FEdGraphPinType>>& Params);

	/**
	 * Whether the event's data outputs are the ones the line declares, in
	 * order. OutActual is the node's own list, as the format writes it.
	 */
	bool EventSignatureMatches(const FNodeScribeStatement& Statement, UEdGraphNode* EventNode, FString& OutActual);

	/**
	 * An event whose signature comes from a class, an interface or a
	 * dispatcher: the line may repeat it, and a different one is said out loud.
	 */
	void WarnOnForeignSignature(const FNodeScribeStatement& Statement, UEdGraphNode* EventNode, const FString& EventLabel);

	// --- Linking ----------------------------------------------------------

	void ApplyArguments(UEdGraphNode* Node, const FNodeScribeStatement& Statement);
	void ApplyLiteral(UEdGraphPin* Pin, const FString& Value, int32 Line);

	/**
	 * An argument of a Make Array, kept until the array has its type.
	 *
	 * The node is born a wildcard. A value written on a wildcard pin is thrown away
	 * when the type arrives, and the first element linked would set the type: `$self`
	 * first makes an array of this class, and the `$owner` after it no longer fits.
	 * What the array feeds is what knows the type.
	 */
	struct FDeferredElement
	{
		FPinRef Pin;
		bool bIsReference = false;
		FString Value;
		int32 Line = 0;
	};

	TArray<FDeferredElement> DeferredElements;

	/** Applies what waits on an array that got its consumer; with bFinal, all that is left. */
	void ApplyDeferredElements(bool bFinal);

	/** @param ConsumerPin  where the value goes in; a new variable's type comes from it. */
	UEdGraphPin* ResolveReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin);

	/** The part before the dot of `$name.Pin`: resolves the node, not the final pin. */
	UEdGraphPin* ResolveBaseReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin);

	void RegisterOutput(const FString& Name, UEdGraphNode* Node, int32 Line);

	UEdGraphPin* FindPinByFuzzyName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction) const;

	/**
	 * Splits a struct pin when the text asks for a part of it.
	 *
	 * In the graph, `Selected Key` may show up split into `Selected Key Key`,
	 * `Selected Key Shift` and so on. The node is always born whole; without
	 * splitting, the requested half simply does not exist and the link is lost.
	 */
	UEdGraphPin* TrySplitToFindPin(UEdGraphNode* Node, const FString& PinPath);

	/**
	 * An input named after a field of a struct pin -- `Spawn Transform
	 * Location`, `In Color And Opacity Specified Color R`, `B X` -- splits that
	 * pin (Split Struct Pin), as many levels as the name goes down. Only by
	 * prefix: the reader always writes the parent's name, and a bare field
	 * would have to choose between struct pins. Splits that found nothing are
	 * undone, so a typo leaves the node as it was.
	 */
	UEdGraphPin* SplitInputToFindPin(UEdGraphNode* Node, const FString& PinPath);
	static UEdGraphPin* FindExecInput(UEdGraphNode* Node);
	static TArray<UEdGraphPin*> GetExecOutputs(UEdGraphNode* Node);
	static UEdGraphPin* FindPrimaryOutput(UEdGraphNode* Node);

	/**
	 * Flow: execution in the EventGraph, pose in the AnimGraph.
	 *
	 * They have the same shape -- an output of the previous node goes into the
	 * next node -- and that is why there is a single walk. What changes is which
	 * pin carries the flow, and the graph decides that, not the line.
	 */
	bool IsFlowPin(const UEdGraphPin* Pin) const;
	UEdGraphPin* FindFlowInput(UEdGraphNode* Node) const;
	TArray<UEdGraphPin*> GetFlowOutputs(UEdGraphNode* Node) const;

	/** Anim nodes created in this paste, to check for empty pose inputs at the end. */
	TArray<TWeakObjectPtr<UEdGraphNode>> AnimNodes;

	/**
	 * Nodes that were already in the graph and the paste only reused.
	 *
	 * The Output Pose is the case: it is born with the AnimGraph, and writing
	 * its line finds the existing one. It does not count as created and is not
	 * repositioned -- moving the node the user already arranged would be odd.
	 */
	TSet<UEdGraphNode*> AdoptedNodes;

	/**
	 * Empty events that were already in the graph and that a line took over.
	 *
	 * Unlike an adopted node, a reused event is moved next to its new chain --
	 * nobody arranged a factory stub --, but it is not counted as created.
	 */
	TSet<UEdGraphNode*> ReusedEvents;

	UEdGraphNode* TryCreateAnimNode(const FNodeScribeStatement& Statement, bool& bOutHandled);

	/**
	 * The state machine getters, inside a transition rule.
	 *
	 * `Time Remaining`, `Get Transition Time Elapsed`, `Get Relevant Anim Time
	 * Remaining`: in the editor menu they show up as functions, but they are not
	 * function calls. They are `UK2Node_AnimGetter`, and what makes them work is
	 * not on any pin -- it is the source state, kept in a property the menu
	 * fills in when creating the node.
	 *
	 * Without this path, the name fell into the catalog and found the function
	 * of the same name in `UAnimationStateMachineLibrary`, which exists, is
	 * public, goes into the graph, and asks for two pins (`UpdateContext` and
	 * `Node`) that have nowhere to come from in a transition rule. The result
	 * was a plausible node that does not compile -- exactly what the plugin
	 * promises not to do.
	 */
	UEdGraphNode* TryCreateAnimGetter(const FNodeScribeStatement& Statement, bool& bOutHandled);

	/**
	 * The indented block under a state machine.
	 *
	 * Returns the index of the first statement that is *not* part of the block.
	 * It consumes the whole block at once, not line by line like the rest of the
	 * walk, because states and transitions do not live in this graph: each has
	 * its own, and each of those is built by a context of its own.
	 */
	int32 BuildStateMachine(UAnimGraphNode_StateMachineBase* Machine, const TArray<FNodeScribeStatement>& Statements, int32 First);

	/**
	 * Builds a sub-graph with a new context and brings the diagnostics back.
	 *
	 * Returns the node of the block's last line -- its result, by the same
	 * criterion as a pose branch. The caller decides what to do with it: a
	 * transition's rule links that result into `Can Enter Transition`.
	 */
	UEdGraphNode* BuildSubGraph(UEdGraph* SubGraph, const TArray<FNodeScribeStatement>& Statements, int32 First, int32 End);

	/** Nodes created inside sub-graphs. They join the result only at the end, outside the layout. */
	TArray<UEdGraphNode*> NestedNodes;

	void Connect(UEdGraphPin* From, UEdGraphPin* To, int32 Line);

	// --- Diagnostics ------------------------------------------------------

	void AddInfo(int32 Line, const FString& Message);
	void AddWarning(int32 Line, const FString& Message);
	void AddError(int32 Line, const FString& Message);

	UClass* GetSelfClass() const;

	/** true if the Blueprint (or one of its parents) has a visible variable with this name. */
	bool IsBlueprintVariable(const FString& Name) const;

	/** Class of the object pointed at by `Target = $something`, when the line has one. */
	UClass* FindTargetClassFromArgs(const FNodeScribeStatement& Statement) const;

	/** Property by tolerant name: `Show Mouse Cursor` finds `bShowMouseCursor`. */
	static FProperty* FindPropertyByFriendlyName(UClass* Class, const FString& Name);

	/** An option from a node's details panel, and where its value lives. */
	struct FNodeSetting
	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
	};

	/**
	 * The option named `Name`, on the node or inside the struct it wraps.
	 *
	 * `OutAvailable` is filled in any case, with the display names of every
	 * option -- it is what the error message needs to say when the node has no
	 * pins at all.
	 */
	static bool FindNodeSetting(UEdGraphNode* Node, const FString& Name, FNodeSetting& Out, TArray<FString>& OutAvailable);

	/** Applies a label's arguments as details panel options. */
	void ApplyLabelSettings(UEdGraphNode* Node, const FNodeScribeStatement& Statement);

	/**
	 * Links the end of the block into the rule graph's `Can Enter Transition`.
	 *
	 * Serves transitions and conduits: both carry a `TransitionResult`, and in
	 * both the text only writes the condition -- linking into it is the
	 * plugin's job, like every link the format does not write.
	 */
	void WireRule(UEdGraph* RuleGraph, UEdGraphNode* RuleResult,
		const FNodeScribeStatement& Statement, bool bHasBody, bool bRuleOptional);

	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	FVector2D Origin = FVector2D::ZeroVector;
	const UEdGraphSchema_K2* Schema = nullptr;

	/** Animation graph: AnimGraph, state interior or transition rule. */
	bool bAnimGraph = false;

	/** Of the three above, the two that actually have a pose. */
	bool bPoseGraph = false;

	/** The third one: the transition rule, which is boolean logic. */
	bool bTransitionGraph = false;

	TArray<FFrame> Frames;

	/** Outputs named with `name = ...`, available for `$name`. */
	TMap<FString, FPinRef> NamedOutputs;

	/** Names whose line did not resolve, and on which line that happened. */
	TMap<FString, int32> FailedOutputs;

	/**
	 * Lines that created a node and did not name its output.
	 *
	 * Exists because of a mistake made all the time: writing `event X` and then
	 * `$X`, forgetting that only `name = event X` creates the reference. Without
	 * this the plugin created a variable called X, which is not even close to
	 * what the person meant.
	 */
	struct FUnnamedLine
	{
		int32 Line = 0;
		FString Expression;
	};

	TArray<FUnnamedLine> UnnamedLines;

	/**
	 * Gets the plugin created by itself because of a `$Variable`.
	 *
	 * If the link that justified them did not happen -- wrong type, pin that
	 * does not exist --, they stay in the graph serving no purpose. Since no
	 * line of the text asked for them, they vanish at the end instead of
	 * becoming clutter.
	 */
	TArray<UEdGraphNode*> AutoCreatedGets;

	/**
	 * Conversion nodes the schema inserted by itself inside Connect().
	 *
	 * Nobody wrote them, so they do not go into CreatedNodes -- the count the
	 * user sees is of lines they sent. But they need to be positioned like any
	 * data node, otherwise they stay where both sides were at link time, which
	 * the layout later abandons.
	 */
	TArray<UEdGraphNode*> ConversionNodes;

	/**
	 * Nodes with several execution outputs still without a label.
	 *
	 * The warning can only come out at the end: when the node is born, the
	 * label that resolves it is usually on the next line, and warning there
	 * would be a false alarm on every well-written Branch and loop.
	 */
	struct FUnbranchedNode
	{
		UEdGraphNode* Node = nullptr;
		int32 Line = 0;
		FString Outputs;
	};

	TArray<FUnbranchedNode> UnbranchedNodes;

	/** true when the node takes no part in the execution flow: it is only a value. */
	bool IsPureDataNode(UEdGraphNode* Node) const;

	/**
	 * Follows the data wires forward until it finds who consumes the value.
	 * @param OutDepth  hops until there. 1 = feeds the execution node directly.
	 */
	UEdGraphNode* FindConsumingExecNode(UEdGraphNode* Node, int32& OutDepth) const;

	/** How many data hops are still left until the end of the chain. 0 = nobody consumes. */
	int32 DataChainRemaining(UEdGraphNode* Node) const;

	/**
	 * Positions every data node, after the whole graph exists.
	 *
	 * It has to be at the end: a Get created by `$Variable` is born during
	 * linking, and the node that consumes it may not have a position yet. That
	 * was what threw that Get far away -- it was anchored at (0,0).
	 */
	void LayoutDataNodes();

	/**
	 * Positions the AnimGraph's pose tree.
	 *
	 * The EventGraph's column layout does not work here: there the chain is a
	 * queue, and here it is a tree converging on the Output Pose. A branch's
	 * width is only known after all of it exists, so this runs at the end,
	 * starting at the root, walking backwards through the input pins.
	 */
	void LayoutAnimNodes();

	/** Places `Node` and what feeds it. Returns the node's Y. */
	int32 PlaceAnimNode(UEdGraphNode* Node, int32 Depth, int32 RootX, int32 RootY, int32& NextRow, TSet<UEdGraphNode*>& Visited);

	/**
	 * Links the end of the chain into the Output Pose, when the text did not write it.
	 *
	 * An AnimGraph that only says `Idle` meant "Idle is the pose". Writing
	 * `Output Pose` on a second line is noise: linking the output at the end of
	 * the chain is the plugin's job, like every link the format does not write.
	 */
	void ConnectOutputPose();

	/**
	 * Links the start of the chain into the Function Entry, when the text did not write it.
	 *
	 * It is the mirror of ConnectOutputPose. In a function graph -- and in the
	 * Construction Script -- the entry already exists and is not created by a
	 * line, just like the Output Pose. Without this link the whole chain goes in,
	 * compiles without a warning, and never runs: nothing on screen tells that
	 * apart from a correct graph, and reading it back only shows the
	 * `Function Entry` alone at the end.
	 */
	void ConnectFunctionEntry();

	/** Does the property accept a Set coming from Blueprint? */
	bool IsVariableWritable(const FString& Name) const;

	/** Complains about empty pose inputs: an empty pose breaks nothing, it just stands still. */
	void ReportEmptyPoseInputs();
};

bool FNodeScribeBuildContext::IsPureDataNode(UEdGraphNode* Node) const
{
	return Node
		&& !Node->IsA<UEdGraphNode_Comment>()
		&& !FindFlowInput(Node)
		&& GetFlowOutputs(Node).Num() == 0;
}

UEdGraphNode* FNodeScribeBuildContext::FindConsumingExecNode(UEdGraphNode* Node, int32& OutDepth) const
{
	OutDepth = 0;
	UEdGraphNode* Current = Node;

	// The data graph is acyclic, but crooked text can close a cycle; the cap
	// keeps the editor from hanging when that happens.
	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		UEdGraphNode* Next = nullptr;

		for (UEdGraphPin* Pin : Current->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsFlowPin(Pin) && Pin->LinkedTo.Num() > 0)
			{
				Next = Pin->LinkedTo[0]->GetOwningNodeUnchecked();
				break;
			}
		}

		if (!Next)
		{
			return nullptr;
		}

		++OutDepth;

		if (!IsPureDataNode(Next))
		{
			return Next;
		}

		Current = Next;
	}

	return nullptr;
}

int32 FNodeScribeBuildContext::DataChainRemaining(UEdGraphNode* Node) const
{
	int32 Hops = 0;
	UEdGraphNode* Current = Node;

	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		UEdGraphNode* Next = nullptr;

		for (UEdGraphPin* Pin : Current->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsFlowPin(Pin) && Pin->LinkedTo.Num() > 0)
			{
				Next = Pin->LinkedTo[0]->GetOwningNodeUnchecked();
				break;
			}
		}

		if (!Next)
		{
			return Hops;
		}

		++Hops;
		Current = Next;
	}

	return Hops;
}

int32 FNodeScribeBuildContext::NextFreeRowY()
{
	int32 FreeY = MIN_int32;

	TArray<UEdGraphNode*> Placed = Result.CreatedNodes;
	Placed.Append(ConversionNodes);

	// A data stack hangs below its consumer and is only positioned at the end,
	// but its size is already known: the arguments of a line are applied before
	// the next line opens a row.
	TMap<UEdGraphNode*, int32> StackSizes;

	for (UEdGraphNode* Node : Placed)
	{
		if (!IsPureDataNode(Node))
		{
			FreeY = FMath::Max(FreeY, Node->NodePosY + BranchRowHeight);
			continue;
		}

		int32 Depth = 0;
		if (UEdGraphNode* Consumer = FindConsumingExecNode(Node, Depth))
		{
			++StackSizes.FindOrAdd(Consumer);
		}
	}

	for (const TPair<UEdGraphNode*, int32>& Stack : StackSizes)
	{
		FreeY = FMath::Max(FreeY,
			Stack.Key->NodePosY + DataRowOffsetY + (Stack.Value * DataRowHeight));
	}

	return FreeY;
}

void FNodeScribeBuildContext::OpenRowIfNeeded(FFrame& Frame, UEdGraphNode* Node)
{
	// There LayoutAnimNodes places what matters, and it walks the tree, not the text.
	if (bPoseGraph)
	{
		return;
	}

	// An event, or a node nothing leads to: what follows has no wire tying it to
	// this row, and the next column of a node with branches is where they went.
	// A comment takes no part in a chain, so it does not start one.
	const bool bStartsChain = !Node->IsA<UEdGraphNode_Comment>()
		&& (!FindFlowInput(Node) || !Frame.PendingExec.Resolve());

	if (bStartsChain && Frame.Column > 0)
	{
		Frame.Column = 0;
	}
	else if (Frame.BranchesOpened == 0)
	{
		return;
	}

	Frame.BaseY = FMath::Max(Frame.BaseY + BranchRowHeight, NextFreeRowY());
	Frame.BranchesOpened = 0;
}

void FNodeScribeBuildContext::LayoutDataNodes()
{
	struct FDataNode
	{
		UEdGraphNode* Node = nullptr;
		int32 Depth = 0;
	};

	TMap<UEdGraphNode*, TArray<FDataNode>> ByConsumer;
	TArray<UEdGraphNode*> Orphans;

	// Conversion nodes go in together with the ones the text asked for: for
	// the layout they are data nodes like any other, and their consumer is
	// found by the same walk. The list is explicit, and not a sweep of the graph
	// looking for data nodes without a position, because a sweep would also
	// drag along what was in the graph before this paste.
	TArray<UEdGraphNode*> DataCandidates = Result.CreatedNodes;
	DataCandidates.Append(ConversionNodes);

	for (UEdGraphNode* Node : DataCandidates)
	{
		if (!IsPureDataNode(Node))
		{
			continue;
		}

		int32 Depth = 0;
		if (UEdGraphNode* Consumer = FindConsumingExecNode(Node, Depth))
		{
			ByConsumer.FindOrAdd(Consumer).Add({ Node, Depth });
		}
		else
		{
			Orphans.Add(Node);
		}
	}

	for (TPair<UEdGraphNode*, TArray<FDataNode>>& Pair : ByConsumer)
	{
		TArray<FDataNode>& Group = Pair.Value;

		// What feeds the execution directly sits right below it, and the
		// dependencies go down from there. Reading top to bottom you go from the
		// finished value to where it came from.
		Group.Sort([](const FDataNode& A, const FDataNode& B) { return A.Depth < B.Depth; });

		for (int32 Row = 0; Row < Group.Num(); ++Row)
		{
			Group[Row].Node->NodePosX =
				Pair.Key->NodePosX + DataColumnOffsetX - (Group[Row].Depth * DataDepthIndentX);

			Group[Row].Node->NodePosY =
				Pair.Key->NodePosY + DataRowOffsetY + (Row * DataRowHeight);
		}
	}

	// A value nobody consumes has nobody to sit under. It goes to a column of
	// its own after the end of the chain, instead of piling up at (0,0).
	//
	// There is no execution node here to measure distance from, so the order
	// comes from the data chain itself: the node nobody consumes goes on top and
	// the dependencies go down -- the same reading as the execution case.
	// Without this the order was the creation order, which is the text's, and it
	// came out upside down.
	if (Orphans.Num() > 0 && Frames.Num() > 0)
	{
		Orphans.Sort([this](UEdGraphNode& A, UEdGraphNode& B)
		{
			return DataChainRemaining(&A) < DataChainRemaining(&B);
		});

		const FFrame& Frame = Frames[0];

		// After the end of the chain is where its branches went, if it opened any.
		const int32 TopY = !bPoseGraph && Frame.BranchesOpened > 0
			? FMath::Max(Frame.BaseY, NextFreeRowY())
			: Frame.BaseY;

		for (int32 Index = 0; Index < Orphans.Num(); ++Index)
		{
			Orphans[Index]->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Orphans[Index]->NodePosY = TopY + (Index * DataRowHeight);
		}
	}
}

int32 FNodeScribeBuildContext::PlaceAnimNode(
	UEdGraphNode* Node, int32 Depth, int32 RootX, int32 RootY, int32& NextRow, TSet<UEdGraphNode*>& Visited)
{
	if (!Node || Visited.Contains(Node))
	{
		return RootY;
	}
	Visited.Add(Node);

	// One branch per input pin, in the order the pins show up on the node --
	// which is the order the user sees on screen.
	TArray<int32> ChildRows;
	for (UEdGraphPin* Pin : NodeScribeAnimGraph::GetPoseInputs(Node))
	{
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			UEdGraphNode* Child = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
			if (Child && Result.CreatedNodes.Contains(Child))
			{
				ChildRows.Add(PlaceAnimNode(Child, Depth + 1, RootX, RootY, NextRow, Visited));
			}
		}
	}

	int32 Y = 0;
	if (ChildRows.Num() == 0)
	{
		// Tip of the tree: gets the next free row. The height of the whole graph
		// comes from here.
		Y = RootY + (NextRow * BranchRowHeight);
		++NextRow;
	}
	else
	{
		// In the middle, it sits at the average height of what feeds it -- the
		// wire comes in straight when there is a single branch, and centred when
		// there are several.
		int32 Sum = 0;
		for (int32 Row : ChildRows)
		{
			Sum += Row;
		}
		Y = Sum / ChildRows.Num();
	}

	// The adopted node (the Output Pose) stays where it is: it is the origin.
	if (!AdoptedNodes.Contains(Node))
	{
		Node->NodePosX = RootX - (Depth * ColumnWidth);
		Node->NodePosY = Y;
	}

	return Y;
}

void FNodeScribeBuildContext::LayoutAnimNodes()
{
	if (!bAnimGraph)
	{
		return;
	}

	UEdGraphNode* Root = NodeScribeAnimGraph::FindOutputPose(Graph);
	if (!Root)
	{
		return;
	}

	int32 NextRow = 0;
	TSet<UEdGraphNode*> Visited;
	PlaceAnimNode(Root, 0, Root->NodePosX, Root->NodePosY, NextRow, Visited);

	// What did not reach the Output Pose is not in the tree -- a branch the
	// text created and linked to nothing. It goes to a band of its own below,
	// visible, instead of piled on top of the tree.
	int32 LooseRow = NextRow + 1;
	for (const TWeakObjectPtr<UEdGraphNode>& Weak : AnimNodes)
	{
		UEdGraphNode* Node = Weak.Get();
		if (!Node || Visited.Contains(Node))
		{
			continue;
		}

		Node->NodePosX = Root->NodePosX - ColumnWidth;
		Node->NodePosY = Root->NodePosY + (LooseRow * BranchRowHeight);
		++LooseRow;
	}
}

void FNodeScribeBuildContext::ConnectOutputPose()
{
	if (!bAnimGraph || Frames.Num() == 0)
	{
		return;
	}

	UEdGraphNode* Root = NodeScribeAnimGraph::FindOutputPose(Graph);
	UEdGraphPin* RootPose = Root ? NodeScribeAnimGraph::FindPoseInput(Root) : nullptr;
	if (!RootPose)
	{
		return;
	}

	if (RootPose->LinkedTo.Num() == 0)
	{
		// Frames[0] is the root level: what is left there is the output of the
		// last node the text put on the main wire.
		if (UEdGraphPin* End = Frames[0].PendingExec.Resolve())
		{
			if (NodeScribeAnimGraph::IsPosePin(End))
			{
				Connect(End, RootPose, 0);
			}
		}
	}

	if (RootPose->LinkedTo.Num() == 0 && AnimNodes.Num() > 0)
	{
		// The graph has anim nodes and still nothing reaches the output. Without
		// this the warning would not come out: the Output Pose is not a node
		// created by this paste, and ReportEmptyPoseInputs only looks at those.
		AddWarning(0, TEXT("Nothing reached the Output Pose. The character stays in the reference pose."));
	}
}

void FNodeScribeBuildContext::ConnectFunctionEntry()
{
	if (bAnimGraph || !Graph)
	{
		return;
	}

	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Found = Cast<UK2Node_FunctionEntry>(Node))
		{
			Entry = Found;
			break;
		}
	}

	if (!Entry)
	{
		// An EventGraph has no entry: there, what starts the chain is the event,
		// and the text writes it.
		return;
	}

	UEdGraphPin* EntryOutput = nullptr;
	for (UEdGraphPin* Pin : Entry->Pins)
	{
		if (Pin->Direction == EGPD_Output && IsExecPin(Pin))
		{
			EntryOutput = Pin;
			break;
		}
	}

	if (!EntryOutput)
	{
		return;
	}

	// The first created node that still has a free execution input is the top
	// of the chain: creation order is the text's order, and everything after it
	// was already linked by what precedes it.
	UEdGraphNode* FirstNode = nullptr;
	UEdGraphPin* FirstInput = nullptr;

	for (UEdGraphNode* Node : Result.CreatedNodes)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Input && IsExecPin(Pin) && Pin->LinkedTo.Num() == 0)
			{
				FirstNode = Node;
				FirstInput = Pin;
				break;
			}
		}

		if (FirstNode)
		{
			break;
		}
	}

	if (!FirstNode)
	{
		return;
	}

	if (EntryOutput->LinkedTo.Num() > 0)
	{
		// Appending to a function that already has a chain: hijacking the entry
		// would erase what was there. The new chain stays loose, and the warning
		// says so.
		AddWarning(0, FString::Printf(
			TEXT("`%s` went in loose: the Function Entry already points at another chain. ")
			TEXT("Link it by hand, or rewrite the graph with `replace`."),
			*FirstNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		return;
	}

	Connect(EntryOutput, FirstInput, 0);
}

void FNodeScribeBuildContext::ReportEmptyPoseInputs()
{
	if (!bAnimGraph)
	{
		return;
	}

	TArray<FString> Empty;

	for (const TWeakObjectPtr<UEdGraphNode>& Weak : AnimNodes)
	{
		UEdGraphNode* Node = Weak.Get();
		if (!Node)
		{
			continue;
		}

		for (UEdGraphPin* Pin : NodeScribeAnimGraph::GetPoseInputs(Node))
		{
			if (Pin->LinkedTo.Num() == 0)
			{
				Empty.Add(FString::Printf(TEXT("%s.%s"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*Pin->PinName.ToString()));
			}
		}
	}

	if (Empty.Num() == 0)
	{
		return;
	}

	// An empty pose is the silent hole of this kind of graph: it compiles, runs,
	// and the character simply stays in the reference pose -- arms spread.
	AddWarning(0, FString::Printf(
		TEXT("Pose input with nothing linked: %s. What comes out of there is the reference pose."),
		*FString::Join(Empty, TEXT(", "))));
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::AddInfo(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Info, Line, Message);
}

void FNodeScribeBuildContext::AddWarning(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Warning, Line, Message);
	++Result.WarningCount;
}

void FNodeScribeBuildContext::AddError(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Error, Line, Message);
	++Result.ErrorCount;
}

UClass* FNodeScribeBuildContext::GetSelfClass() const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// The skeleton reflects the Blueprint as it is now, with the variables and
	// dispatchers you just created. GeneratedClass only reaches what was
	// already compiled, and nobody compiles before pasting -- the skeleton is
	// what the editor itself uses to build the graph.
	if (Blueprint->SkeletonGeneratedClass)
	{
		return Blueprint->SkeletonGeneratedClass;
	}

	return Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get();
}

bool FNodeScribeBuildContext::IsVariableWritable(const FString& Name) const
{
	UClass* SelfClass = GetSelfClass();
	const FProperty* Property = SelfClass ? SelfClass->FindPropertyByName(FName(*Name)) : nullptr;

	return Property != nullptr
		&& !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst);
}

bool FNodeScribeBuildContext::IsBlueprintVariable(const FString& Name) const
{
	if (Name.IsEmpty())
	{
		return false;
	}

	// Looking at the class (and not only at the Blueprint's variable list)
	// makes variables inherited from a C++ parent be recognised too.
	UClass* SelfClass = GetSelfClass();
	if (!SelfClass)
	{
		return false;
	}

	const FProperty* Property = SelfClass->FindPropertyByName(FName(*Name));
	return Property != nullptr && Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
}

UClass* FNodeScribeBuildContext::FindTargetClassFromArgs(const FNodeScribeStatement& Statement) const
{
	for (const FNodeScribeArg& Arg : Statement.Args)
	{
		if (!Arg.bIsReference)
		{
			continue;
		}

		const FString PinName = FNodeScribeCatalog::Normalize(Arg.PinName);
		if (PinName != TEXT("target") && PinName != TEXT("self"))
		{
			continue;
		}

		// A query with no side effects: `ResolveReference` would create nodes,
		// and here we are still deciding whether this node exists.
		FString BaseName = Arg.Value;
		FString Ignored;
		BaseName.Split(TEXT("."), &BaseName, &Ignored);

		const FPinRef* Ref = NamedOutputs.Find(BaseName);
		if (!Ref)
		{
			// `$Enemy` with no line behind it is an implicit Get of the
			// Blueprint's variable (see ResolveBaseReference), which the reader
			// writes that way. Its class comes from the property, since the Get
			// does not exist yet.
			if (BaseName == Arg.Value && IsBlueprintVariable(BaseName))
			{
				const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(
					GetSelfClass()->FindPropertyByName(FName(*BaseName)));

				if (ObjectProperty && ObjectProperty->PropertyClass)
				{
					return ObjectProperty->PropertyClass;
				}
			}

			continue;
		}

		UEdGraphPin* Pin = const_cast<FPinRef*>(Ref)->Resolve();
		if (!Pin)
		{
			continue;
		}

		if (UClass* Class = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			return Class;
		}
	}

	return nullptr;
}

FProperty* FNodeScribeBuildContext::FindPropertyByFriendlyName(UClass* Class, const FString& Name)
{
	if (!Class)
	{
		return nullptr;
	}

	const FString Wanted = FNodeScribeCatalog::Normalize(Name);

	for (TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			continue;
		}

		// A bool's internal name has a `b` in front, and the name shown on
		// screen does not. `Show Mouse Cursor` has to find `bShowMouseCursor`.
		if (FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted
			|| FNodeScribeCatalog::Normalize(Property->GetDisplayNameText().ToString()) == Wanted)
		{
			return Property;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------

UEdGraphPin* FNodeScribeBuildContext::FindExecInput(UEdGraphNode* Node)
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

TArray<UEdGraphPin*> FNodeScribeBuildContext::GetExecOutputs(UEdGraphNode* Node)
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

bool FNodeScribeBuildContext::IsFlowPin(const UEdGraphPin* Pin) const
{
	return IsExecPin(Pin) || (bAnimGraph && NodeScribeAnimGraph::IsPosePin(Pin));
}

UEdGraphPin* FNodeScribeBuildContext::FindFlowInput(UEdGraphNode* Node) const
{
	if (UEdGraphPin* Exec = FindExecInput(Node))
	{
		return Exec;
	}
	return bAnimGraph ? NodeScribeAnimGraph::FindPoseInput(Node) : nullptr;
}

TArray<UEdGraphPin*> FNodeScribeBuildContext::GetFlowOutputs(UEdGraphNode* Node) const
{
	TArray<UEdGraphPin*> Outputs = GetExecOutputs(Node);
	if (Outputs.Num() == 0 && bAnimGraph)
	{
		Outputs = NodeScribeAnimGraph::GetPoseOutputs(Node);
	}
	return Outputs;
}

UEdGraphPin* FNodeScribeBuildContext::FindPrimaryOutput(UEdGraphNode* Node)
{
	// `ReturnValue` is the canonical name of a function call's result.
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
		{
			return Pin;
		}
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
		{
			return Pin;
		}
	}

	return nullptr;
}

UEdGraphPin* FNodeScribeBuildContext::FindPinByFuzzyName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction) const
{
	// The name as written first, the alias second. `Target` is an alias for the
	// self pin, but a static function may have a parameter literally called
	// `Target` -- `Get Blackboard (Target = $controller)` is one. Trying only the
	// alias looked for `self`, found nothing, and answered "the node has no pin
	// `Target`. Input pins: Target", which is what the reader had just written.
	TArray<FString, TInlineAllocator<2>> Attempts;
	Attempts.Add(FNodeScribeCatalog::Normalize(Name));
	Attempts.AddUnique(ResolvePinAlias(Name));

	// Internal names before display names, over all the pins: the self pin is
	// displayed as `Target`, and on `BP_ApplyGameplayEffectToTarget`, whose
	// parameter is called `Target`, the first pin in the list won -- the
	// parameter's value went into self.
	for (const FString& Wanted : Attempts)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && !Pin->bHidden
				&& FNodeScribeCatalog::Normalize(Pin->PinName.ToString()) == Wanted)
			{
				return Pin;
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction && !Pin->bHidden && !Pin->PinFriendlyName.IsEmpty()
				&& FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString()) == Wanted)
			{
				return Pin;
			}
		}
	}

	const FString Wanted = ResolvePinAlias(Name);

	// Unreal code convention: a bool is called `bXYOverride`, and the interface
	// shows "XY Override". Nobody types the `b`, and the pin does not always
	// have a friendly name to cover that.
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction != Direction || Pin->bHidden)
		{
			continue;
		}

		const FString PinName = Pin->PinName.ToString();
		if (PinName.Len() > 1 && PinName[0] == TEXT('b') && FChar::IsUpper(PinName[1])
			&& FNodeScribeCatalog::Normalize(PinName.RightChop(1)) == Wanted)
		{
			return Pin;
		}
	}

	return nullptr;
}

UEdGraphPin* FNodeScribeBuildContext::TrySplitToFindPin(UEdGraphNode* Node, const FString& PinPath)
{
	const FString Wanted = FNodeScribeCatalog::Normalize(PinPath);

	// A copy: splitting a pin changes Node->Pins during the iteration.
	TArray<UEdGraphPin*> Candidates = Node->Pins;

	auto IsSplittableStruct = [this](UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Output && !IsExecPin(Pin) && Pin->SubPins.Num() == 0
			&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
			&& Schema->CanSplitStructPin(*Pin);
	};

	// --- Already split: the field is a sub-pin -----------------------------
	//
	// A struct split by an earlier line keeps its sub-pins, and the parent is no
	// longer splittable. `$s.Battery Volts` split `ReturnValue`, and the next
	// line's `$s.Is Supported` then found nothing: only the raw
	// `ReturnValue_bIsSupported` resolved.
	TArray<UEdGraphPin*> SplitMatches;
	for (UEdGraphPin* Parent : Candidates)
	{
		if (!Parent || Parent->Direction != EGPD_Output || Parent->SubPins.Num() == 0)
		{
			continue;
		}

		for (UEdGraphPin* Sub : Parent->SubPins)
		{
			if (Sub && SubPinMatches(Parent, Sub, Wanted))
			{
				SplitMatches.Add(Sub);
			}
		}
	}

	// Two split structs with the same field: answering would be choosing.
	if (SplitMatches.Num() == 1)
	{
		return SplitMatches[0];
	}
	if (SplitMatches.Num() > 1)
	{
		return nullptr;
	}

	// --- 1st attempt: the requested name starts with the pin's name -------
	//
	// `Selected Key Key` starts with `Selected Key`: it is part of that struct.
	for (UEdGraphPin* Pin : Candidates)
	{
		if (!IsSplittableStruct(Pin))
		{
			continue;
		}

		const FString PinName = FNodeScribeCatalog::Normalize(Pin->PinName.ToString());
		const FString FriendlyName = Pin->PinFriendlyName.IsEmpty()
			? PinName
			: FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString());

		if (!Wanted.StartsWith(PinName) && !Wanted.StartsWith(FriendlyName))
		{
			continue;
		}

		Schema->SplitPin(Pin, false);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Output))
		{
			return Found;
		}
	}

	// --- 2nd attempt: the requested name is a field of the struct ---------
	//
	// `$velocity.Z` with the pin called `ReturnValue`. The prefix does not help
	// -- "z" does not start with "returnvalue" --, and an actor's `Get Velocity`
	// is the most common case there is: a function's return pin almost never
	// has a name of its own.
	//
	// We ask the struct before splitting. Splitting to find out would change the
	// graph while searching, and a pin split for nothing looks visibly
	// different from what the text asked for.
	TArray<UEdGraphPin*> WithField;
	for (UEdGraphPin* Pin : Candidates)
	{
		if (!IsSplittableStruct(Pin))
		{
			continue;
		}

		const UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
		if (!Struct)
		{
			continue;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (FieldMatches(*It, Wanted))
			{
				WithField.Add(Pin);
				break;
			}
		}
	}

	// Two struct pins with a `Z` field each: splitting one of them would be
	// choosing, and the chosen node compiles and runs with the other's value.
	if (WithField.Num() > 1)
	{
		return nullptr;
	}

	if (WithField.Num() == 1)
	{
		UEdGraphPin* Parent = WithField[0];
		const FString ParentName = FNodeScribeCatalog::Normalize(Parent->PinName.ToString());

		Schema->SplitPin(Parent, false);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Output))
		{
			return Found;
		}

		// The sub-pin is born with the parent's name glued to the field's:
		// splitting `ReturnValue` gives `ReturnValue_X`, `ReturnValue_Y`,
		// `ReturnValue_Z`. Looking for a bare `Z` finds none of them -- and the
		// search used to stop here, after having already split the pin, which
		// left the graph changed and the link undone.
		for (UEdGraphPin* Sub : Parent->SubPins)
		{
			if (Sub && SubPinMatches(Parent, Sub, Wanted))
			{
				return Sub;
			}
		}

		for (UEdGraphPin* Sub : Parent->SubPins)
		{
			if (!Sub)
			{
				continue;
			}

			const FString SubName = FNodeScribeCatalog::Normalize(Sub->PinName.ToString());
			if (SubName == ParentName + Wanted || SubName.EndsWith(Wanted))
			{
				return Sub;
			}
		}
	}

	return nullptr;
}

UEdGraphPin* FNodeScribeBuildContext::SplitInputToFindPin(UEdGraphNode* Node, const FString& PinPath)
{
	const FString Wanted = FNodeScribeCatalog::Normalize(PinPath);
	TArray<UEdGraphPin*> SplitHere;

	// Each round splits one level: an FSlateColor holds an FLinearColor, and
	// `... Specified Color R` is two splits down.
	for (int32 Depth = 0; Depth < 4; ++Depth)
	{
		UEdGraphPin* Prefix = nullptr;
		int32 PrefixLength = 0;
		bool bTied = false;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || Pin->bHidden || Pin->SubPins.Num() > 0
				|| IsExecPin(Pin) || Pin->LinkedTo.Num() > 0
				|| Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct
				|| !Schema->CanSplitStructPin(*Pin))
			{
				continue;
			}

			TArray<FString, TInlineAllocator<2>> Names;
			Names.Add(FNodeScribeCatalog::Normalize(Pin->PinName.ToString()));
			if (!Pin->PinFriendlyName.IsEmpty())
			{
				Names.AddUnique(FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString()));
			}

			for (const FString& Name : Names)
			{
				if (Name.IsEmpty() || Name.Len() >= Wanted.Len() || !Wanted.StartsWith(Name))
				{
					continue;
				}

				if (Name.Len() > PrefixLength)
				{
					Prefix = Pin;
					PrefixLength = Name.Len();
					bTied = false;
				}
				else if (Name.Len() == PrefixLength && Pin != Prefix)
				{
					bTied = true;
				}
			}
		}

		// Two struct pins the name could belong to: splitting one is choosing.
		if (!Prefix || bTied)
		{
			break;
		}

		Schema->SplitPin(Prefix, false);
		SplitHere.Add(Prefix);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Input))
		{
			return Found;
		}
	}

	// Nothing under those splits: put the node back, deepest first.
	for (int32 Index = SplitHere.Num() - 1; Index >= 0; --Index)
	{
		if (SplitHere[Index]->SubPins.Num() > 0)
		{
			Schema->RecombinePin(SplitHere[Index]->SubPins[0]);
		}
	}

	return nullptr;
}

void FNodeScribeBuildContext::Connect(UEdGraphPin* From, UEdGraphPin* To, int32 Line)
{
	if (!From || !To)
	{
		return;
	}

	// `Bind X (Event = $done)` with `done = event Done` declaring nothing: the
	// event takes the dispatcher's signature, as the editor's "create matching
	// event" does. Without it the schema refused the wire -- no parameters is
	// not OnDestroyed's signature --, and the Blueprint did not compile. An
	// event that declared parameters keeps them, and a mismatch is said below.
	if (From->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
		&& To->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
	{
		UK2Node_CustomEvent* BoundEvent = Cast<UK2Node_CustomEvent>(From->GetOwningNode());
		const UFunction* Signature = FMemberReference::ResolveSimpleMemberReference<UFunction>(
			To->PinType.PinSubCategoryMemberReference);

		if (BoundEvent && Signature && BoundEvent->UserDefinedPins.Num() == 0)
		{
			for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FProperty* Param = *It;
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm)
					|| (Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ReferenceParm)))
				{
					continue;
				}

				FEdGraphPinType ParamType;
				if (Schema->ConvertPropertyToPinType(Param, ParamType))
				{
					BoundEvent->CreateUserDefinedPin(Param->GetFName(), ParamType, EGPD_Output, /*bUseUniqueName*/ false);
				}
			}

			// The schema compares the dispatcher with the event's function in the
			// skeleton, which still has none of these. Regenerating may rebuild
			// pins, so they are found again by name.
			if (BoundEvent->UserDefinedPins.Num() > 0)
			{
				UEdGraphNode* ToNode = To->GetOwningNode();
				const FName FromName = From->PinName;
				const FName ToName = To->PinName;

				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				FKismetEditorUtilities::GenerateBlueprintSkeleton(Blueprint, true);

				From = BoundEvent->FindPin(FromName);
				To = ToNode->FindPin(ToName);
				if (!From || !To)
				{
					AddWarning(Line, FString::Printf(
						TEXT("`%s` lost a pin when it took the dispatcher's parameters. The link was not made."),
						*BoundEvent->CustomFunctionName.ToString()));
					return;
				}
			}
		}
	}

	// The K2 schema's TryCreateConnection inserts a conversion node by itself
	// when the types are compatible through an implicit cast (int -> float, etc).
	if (Schema->TryCreateConnection(From, To))
	{
		// When that happens, the two pins are not linked to each other: what
		// now feeds `To` is the conversion node's output. That is how it can be
		// recognised -- and it needs to be recognised here, because after the
		// link nothing else tells that node apart from one the text asked for.
		// Without this it stays at the position both sides had right now, which
		// LayoutDataNodes() soon abandons, and the result is a loose node in the
		// middle of nowhere with two wires crossing the whole graph.
		if (!From->LinkedTo.Contains(To))
		{
			const UEdGraphNode* SourceNode = From->GetOwningNodeUnchecked();
			for (UEdGraphPin* Feeding : To->LinkedTo)
			{
				UEdGraphNode* Inserted = Feeding ? Feeding->GetOwningNodeUnchecked() : nullptr;
				if (Inserted && Inserted != SourceNode && !Result.CreatedNodes.Contains(Inserted))
				{
					ConversionNodes.AddUnique(Inserted);
				}
			}
		}
	}
	else
	{
		// Stating both types is what solves the most confusing case: a name that
		// exists, but is not what you meant. `$Slot` finds the `Slot` every
		// UWidget inherits, not the variable you were about to create.
		AddWarning(Line, FString::Printf(
			TEXT("`%s` is %s, and pin `%s` expects %s. The link was not made."),
			*From->PinName.ToString(),
			*UEdGraphSchema_K2::TypeToText(From->PinType).ToString(),
			*To->PinName.ToString(),
			*UEdGraphSchema_K2::TypeToText(To->PinType).ToString()));
	}
}

// ---------------------------------------------------------------------------
// Values and references
// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::ApplyLiteral(UEdGraphPin* Pin, const FString& Value, int32 Line)
{
	// `?` is a declared hole: "this choice is yours, not mine".
	// We leave the pin empty on purpose -- if it is required, the Blueprint does
	// not compile, and the pending item shows up by itself instead of becoming a
	// silent bug.
	if (Value == TEXT("?") || Value.StartsWith(TEXT("?")))
	{
		AddWarning(Line, FString::Printf(
			TEXT("Pin `%s` was left empty, waiting for your choice."), *Pin->PinName.ToString()));
		return;
	}

	if (IsObjectLikePin(Pin))
	{
		if (Value.StartsWith(TEXT("/")))
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *Value))
			{
				Schema->TrySetDefaultObject(*Pin, Asset);
				return;
			}

			AddWarning(Line, FString::Printf(
				TEXT("Could not find asset `%s`. Pick it on pin `%s`."), *Value, *Pin->PinName.ToString()));
			return;
		}

		// A class pin accepts a short name: `WBP_RemapRow` finds the generated
		// class `WBP_RemapRow_C`. Same shortcut `Cast to BP_Boss` uses.
		const FName Category = Pin->PinType.PinCategory;
		if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			if (UClass* Found = FindClassByFriendlyNameInternal(Value))
			{
				Schema->TrySetDefaultObject(*Pin, Found);
				return;
			}
		}

		// Without a full path there is no way to know which asset it is. We do not guess.
		AddWarning(Line, FString::Printf(
			TEXT("`%s` is not an asset path. Pick it on the node's `%s` pin."),
			*Value, *Pin->PinName.ToString()));
		return;
	}

	Schema->TrySetDefaultValue(*Pin, Value);
}

UEdGraphPin* FNodeScribeBuildContext::ResolveReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin)
{
	// `$name` alone takes the node's main output. When the node has several
	// data outputs -- an event with parameters, a struct Break, a function with
	// out params -- `$name.Pin` says which one.
	FString BaseName = Name;
	FString PinPath;

	if (Name.Split(TEXT("."), &BaseName, &PinPath))
	{
		BaseName.TrimStartAndEndInline();
		PinPath.TrimStartAndEndInline();
	}

	UEdGraphPin* BasePin = ResolveBaseReference(BaseName, Line, ConsumerPin);
	if (!BasePin || PinPath.IsEmpty())
	{
		return BasePin;
	}

	UEdGraphNode* SourceNode = BasePin->GetOwningNodeUnchecked();
	if (!SourceNode)
	{
		return nullptr;
	}

	if (UEdGraphPin* Chosen = FindPinByFuzzyName(SourceNode, PinPath, EGPD_Output))
	{
		return Chosen;
	}

	if (UEdGraphPin* Split = TrySplitToFindPin(SourceNode, PinPath))
	{
		return Split;
	}

	TArray<FString> Available;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !IsExecPin(Pin) && !Pin->bHidden)
		{
			Available.Add(Pin->PinName.ToString());
		}
	}

	RecordUnresolvedName(TEXT("output"), PinPath, TEXT("$") + BaseName);

	AddError(Line, FString::Printf(
		TEXT("`$%s` has no output `%s`. Data outputs: %s"),
		*BaseName, *PinPath, *FString::Join(Available, TEXT(", "))));

	return nullptr;
}

UEdGraphPin* FNodeScribeBuildContext::ResolveBaseReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin)
{
	if (const FPinRef* Existing = NamedOutputs.Find(Name))
	{
		if (UEdGraphPin* Pin = Existing->Resolve())
		{
			return Pin;
		}
	}

	// A node already in the graph, by the name its reading gave it -- the
	// value an `after` block came to use is usually computed up there. Not for
	// a variable: the reading writes `$Health` for a Get it does not name, and
	// a fresh Get beside the new node beats a wire across the graph.
	if (!IsBlueprintVariable(Name))
	{
		if (UEdGraphNode* const* ExistingNode = ExistingNames.Find(Name))
		{
			if (UEdGraphPin* Pin = FindPrimaryOutput(*ExistingNode))
			{
				return Pin;
			}
		}
	}

	// Convenience: `$Health` without a prior declaration becomes a Get of the
	// Blueprint's variable, created and positioned automatically.
	if (IsBlueprintVariable(Name))
	{
		UK2Node_VariableGet* GetNode = AllocateNode<UK2Node_VariableGet>();
		GetNode->VariableReference.SetSelfMember(FName(*Name));
		FinalizeNode(GetNode);

		// The position is left to LayoutDataNodes(): here the node that
		// consumes this Get may not even have been positioned yet.
		Result.CreatedNodes.Add(GetNode);
		AutoCreatedGets.Add(GetNode);

		UEdGraphPin* OutputPin = FindPrimaryOutput(GetNode);
		if (OutputPin)
		{
			NamedOutputs.Add(Name, FPinRef(OutputPin));
		}

		return OutputPin;
	}

	// The cause was already reported further back; repeating "does not exist"
	// here would send the user looking for the problem in the wrong place.
	if (const int32* FailedLine = FailedOutputs.Find(Name))
	{
		AddError(Line, FString::Printf(
			TEXT("`$%s` comes from line %d, which did not resolve. Fix that line first."),
			*Name, *FailedLine));

		return nullptr;
	}

	// Before treating it as a variable: the name may belong to an earlier line
	// that simply was not named. Creating a variable in that case would obey
	// the letter and ignore the intent -- and the text usually comes from
	// someone who wrote `event X` and right below it `$X`.
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		// A short name matches anything by chance; requiring three letters keeps
		// a coincidence from turning into a diagnostic.
		if (Wanted.Len() >= 3)
		{
			for (const FUnnamedLine& Candidate : UnnamedLines)
			{
				if (FNodeScribeCatalog::Normalize(Candidate.Expression).Contains(Wanted))
				{
					AddError(Line, FString::Printf(
						TEXT("`$%s` does not exist, but line %d looks like what you meant. ")
						TEXT("To reference it, give it a name: write `%s = ` at its start."),
						*Name, Candidate.Line, *Name));

					return nullptr;
				}
			}
		}
	}

	// The variable does not exist yet. Instead of dropping the link, we create
	// the unresolved Get -- it is what Unreal does when pasting nodes between
	// different Blueprints. The graph flags the error and right-clicking the
	// node offers to create the variable, which is one click against rebuilding
	// the chain by hand.
	//
	// This does not loosen the no-guessing rule: the Blueprint still does not
	// compile until you act. It only changes where the pending item is visible.
	if (ConsumerPin)
	{
		UK2Node_VariableGet* GetNode = AllocateNode<UK2Node_VariableGet>();
		GetNode->VariableReference.SetSelfMember(FName(*Name));
		FinalizeNode(GetNode);

		// Without the property, AllocateDefaultPins creates no pin at all. The
		// type comes from whoever will consume the value: it is the only source
		// available here, and it is exactly the type the variable needs to have.
		UEdGraphPin* OutputPin = FindPrimaryOutput(GetNode);
		if (!OutputPin)
		{
			OutputPin = GetNode->CreatePin(EGPD_Output, ConsumerPin->PinType, FName(*Name));
		}

		Result.CreatedNodes.Add(GetNode);
		AutoCreatedGets.Add(GetNode);

		AddWarning(Line, FString::Printf(
			TEXT("`%s` does not exist in this Blueprint. I created the Get anyway: the graph will flag the error, ")
			TEXT("and right-clicking the node offers to create the variable."), *Name));

		if (OutputPin)
		{
			NamedOutputs.Add(Name, FPinRef(OutputPin));
		}

		return OutputPin;
	}

	AddError(Line, FString::Printf(
		TEXT("`$%s` does not exist: it is neither the output of an earlier line nor a variable of this Blueprint."), *Name));

	return nullptr;
}

void FNodeScribeBuildContext::RegisterOutput(const FString& Name, UEdGraphNode* Node, int32 Line)
{
	UEdGraphPin* OutputPin = FindPrimaryOutput(Node);
	if (!OutputPin)
	{
		AddWarning(Line, FString::Printf(
			TEXT("`%s` was not registered: this node has no data output."), *Name));
		return;
	}

	if (const FPinRef* Existing = NamedOutputs.Find(Name))
	{
		// The pre-pass already registered this event's name, so lines above it
		// could bind it. Meeting its own line is not a redefinition.
		if (Existing->Resolve() == OutputPin)
		{
			return;
		}

		AddWarning(Line, FString::Printf(TEXT("`%s` was defined again; the last one wins."), *Name));
	}

	NamedOutputs.Add(Name, FPinRef(OutputPin));
}

bool FNodeScribeBuildContext::FindNodeSetting(UEdGraphNode* Node, const FString& Name, FNodeSetting& Out, TArray<FString>& OutAvailable)
{
	const FString Wanted = FNodeScribeCatalog::Normalize(Name);

	// Two layers: the node's properties, and the ones inside the struct it
	// wraps. In a `UAnimGraphNode_SequencePlayer` what matters lives in the
	// second -- `bLoopAnimation` and `PlayRate` are fields of the `FAnimNode_*`,
	// and the node just carries it.
	TArray<FNodeSetting> Found;

	auto Consider = [&](FProperty* Property, void* Container)
	{
		if (!IsNodeSetting(Property))
		{
			return;
		}

		const FString Display = DisplayName(Property);
		OutAvailable.AddUnique(Display);

		if (FNodeScribeCatalog::Normalize(Display) == Wanted
			|| FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted)
		{
			Found.Add({ Property, Property->ContainerPtrToValuePtr<void>(Container) });
		}
	};

	for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
	{
		FProperty* Property = *It;

		if (FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			// Only the anim struct opens up -- it is where `Loop Animation`
			// lives. See IsAnimNodeStruct: opening any of them would turn the
			// fields of an `FGuid` into options called `A`, `B`, `C` and `D`.
			if (IsNodeSetting(Property) && IsAnimNodeStruct(AsStruct))
			{
				void* StructPtr = AsStruct->ContainerPtrToValuePtr<void>(Node);
				for (TFieldIterator<FProperty> Inner(AsStruct->Struct); Inner; ++Inner)
				{
					Consider(*Inner, StructPtr);
				}
				continue;
			}
		}

		Consider(Property, Node);
	}

	// Two properties with the same display name, in different layers: picking
	// one is right half the time, and the wrong half writes a value into an asset.
	if (Found.Num() != 1)
	{
		return false;
	}

	Out = Found[0];
	return true;
}

namespace
{
	/**
	 * Tells the node which option changed, as the details panel does.
	 *
	 * A bare PostEditChange says nothing changed in particular, and a node that
	 * rebuilds its pins on one option ignores it: `Switch on String (Pin Names
	 * = ...)` kept only its Default output, and every label under it failed.
	 * An option inside a struct the node wraps is not the node's own property,
	 * so there the general form stays.
	 */
	void NotifyNodeOptionChanged(UEdGraphNode* Node, FProperty* Property)
	{
		const UClass* Owner = Property ? Property->GetOwnerClass() : nullptr;
		if (Owner && Node->GetClass()->IsChildOf(Owner))
		{
			FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
			Node->PostEditChangeProperty(ChangedEvent);
			return;
		}

		Node->PostEditChange();
	}
}

void FNodeScribeBuildContext::ApplyLabelSettings(UEdGraphNode* Node, const FNodeScribeStatement& Statement)
{
	// A label has no pins: states, conduits and transitions are set up only
	// through the details panel. That is why this is only the option half of
	// ApplyArguments -- there is nowhere to send a positional argument, and a
	// `$reference` would have no wire to follow.
	for (const FNodeScribeArg& Arg : Statement.Args)
	{
		if (Arg.PinName.IsEmpty())
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` in `%s:` has no name. A label only accepts named options: ")
				TEXT("`(Name = value)`."), *Arg.Value, *Statement.Label));
			continue;
		}

		FNodeSetting Setting;
		TArray<FString> Available;

		if (!FindNodeSetting(Node, Arg.PinName, Setting, Available))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` is not an option of `%s:`. Options: %s"),
				*Arg.PinName, *Statement.Label,
				Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("none")));
			continue;
		}

		if (Arg.bIsReference)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` is a details panel option: it takes a fixed value, not `$%s`."),
				*Arg.PinName, *Arg.Value));
			continue;
		}

		Node->Modify();

		FString Error;
		if (!TextToValue(Setting.Property, Setting.ValuePtr, Arg.Value, Error))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s = %s` did not go in: %s"), *Arg.PinName, *Arg.Value, *Error));
			continue;
		}

		NotifyNodeOptionChanged(Node, Setting.Property);
	}
}

namespace
{
	/**
	 * Grows a node with an "Add pin" button until it has the pin the text names.
	 *
	 * `Append (A = ..., B = ..., C = ...)` is how the reader writes an Append
	 * with three inputs, and the node is born with two: `C` only exists after
	 * the button. Pins added for a name that never showed up are taken off
	 * again, so a typo does not leave the node wider than anyone wrote.
	 */
	UEdGraphPin* GrowToFindPin(UEdGraphNode* Node, TFunctionRef<UEdGraphPin*()> Find)
	{
		IK2Node_AddPinInterface* Growable = Cast<IK2Node_AddPinInterface>(Node);
		if (!Growable)
		{
			return nullptr;
		}

		TArray<UEdGraphPin*> Added;
		while (Growable->CanAddPin() && Added.Num() < IK2Node_AddPinInterface::GetMaxInputPinsNum())
		{
			const TSet<UEdGraphPin*> Before(Node->Pins);
			Growable->AddInputPin();

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Before.Contains(Pin))
				{
					Added.Add(Pin);
				}
			}

			if (UEdGraphPin* Found = Find())
			{
				return Found;
			}

			if (Node->Pins.Num() == Before.Num())
			{
				break;
			}
		}

		for (int32 Index = Added.Num() - 1; Index >= 0; --Index)
		{
			if (Growable->CanRemovePin(Added[Index]))
			{
				Growable->RemoveInputPin(Added[Index]);
			}
		}
		return nullptr;
	}
}

void FNodeScribeBuildContext::ApplyArguments(UEdGraphNode* Node, const FNodeScribeStatement& Statement)
{
	// Eligible input pins, in order, to resolve positional arguments.
	TArray<UEdGraphPin*> PositionalPins;
	auto CollectPositional = [this, Node, &PositionalPins]()
	{
		PositionalPins.Reset();
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Input && !IsExecPin(Pin) && !Pin->bHidden)
			{
				PositionalPins.Add(Pin);
			}
		}
	};
	CollectPositional();

	int32 NextPositional = 0;

	for (const FNodeScribeArg& Arg : Statement.Args)
	{
		UEdGraphPin* Pin = nullptr;

		// Already wired when the node was made (a class from a variable).
		if (ArgsAppliedEarly.Contains(&Arg))
		{
			if (Arg.PinName.IsEmpty())
			{
				++NextPositional;
			}
			continue;
		}

		if (!Arg.PinName.IsEmpty())
		{
			Pin = FindPinByFuzzyName(Node, Arg.PinName, EGPD_Input);

			// Before growing: `B X` is a field of `B`, not a pin an Add button
			// would ever make.
			if (!Pin)
			{
				Pin = SplitInputToFindPin(Node, Arg.PinName);
				if (Pin)
				{
					CollectPositional();
				}
			}

			if (!Pin)
			{
				Pin = GrowToFindPin(Node, [this, Node, &Arg]()
				{
					return FindPinByFuzzyName(Node, Arg.PinName, EGPD_Input);
				});
				if (Pin)
				{
					CollectPositional();
				}
			}

			if (!Pin)
			{
				// Not everything you set on a node is a pin. `Loop Animation` and
				// `Play Rate` of an asset player, `Blend Time` of a transition:
				// they live in the details panel, and before this the answer was
				// "the node has no pin `Loop Animation`. Input pins:" -- with an
				// empty list, because a Sequence Player has none. Anyone reading
				// that would conclude the Engine does not have that option.
				FNodeSetting Setting;
				TArray<FString> Settings;

				if (FindNodeSetting(Node, Arg.PinName, Setting, Settings))
				{
					if (Arg.bIsReference)
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` is a details panel option, not a pin: it takes a fixed value, ")
							TEXT("not `$%s`."), *Arg.PinName, *Arg.Value));
						continue;
					}

					Node->Modify();

					FString Error;
					if (!TextToValue(Setting.Property, Setting.ValuePtr, Arg.Value, Error))
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s = %s` did not go in: %s"), *Arg.PinName, *Arg.Value, *Error));
						continue;
					}

					NotifyNodeOptionChanged(Node, Setting.Property);

					AddInfo(Statement.LineNumber, FString::Printf(
						TEXT("`%s` is not a pin: it went in as a node option."), *Arg.PinName));
					continue;
				}

				TArray<FString> Available;
				for (UEdGraphPin* Candidate : PositionalPins)
				{
					Available.Add(Candidate->PinName.ToString());
				}

				// A wrong pin name is the same family: that is how it came out that
				// the documentation's `Break Vector (In Vec = ...)` example never
				// worked -- the pin is named after the struct.
				RecordUnresolvedName(TEXT("pin"), Arg.PinName, Statement.NodeExpression);

				// With no pins at all, saying "input pins: (nothing)" does not help.
				// What answers the next question is the list of options.
				const FString Where = Available.Num() > 0
					? FString::Printf(TEXT("Input pins: %s"), *FString::Join(Available, TEXT(", ")))
					: (Settings.Num() > 0
						? FString::Printf(TEXT("This node has no input pins. Panel options: %s"),
							*FString::Join(Settings, TEXT(", ")))
						: TEXT("This node has no input pins and no adjustable options."));

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("The node has no pin `%s`. %s"), *Arg.PinName, *Where));
				continue;
			}
		}
		else
		{
			if (!PositionalPins.IsValidIndex(NextPositional))
			{
				// One more value than pins on an Append is one more pin.
				GrowToFindPin(Node, [&CollectPositional, &PositionalPins, NextPositional]()
				{
					CollectPositional();
					return PositionalPins.IsValidIndex(NextPositional) ? PositionalPins[NextPositional] : nullptr;
				});
				CollectPositional();
			}

			if (!PositionalPins.IsValidIndex(NextPositional))
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("One argument too many (`%s`): the node does not have that many input pins."), *Arg.Value));
				continue;
			}

			Pin = PositionalPins[NextPositional++];
		}

		if (Node->IsA<UK2Node_MakeArray>())
		{
			DeferredElements.Add({ FPinRef(Pin), Arg.bIsReference, Arg.Value, Statement.LineNumber });
		}
		else if (Arg.bIsReference)
		{
			if (UEdGraphPin* Source = ResolveReference(Arg.Value, Statement.LineNumber, Pin))
			{
				Connect(Source, Pin, Statement.LineNumber);
			}
		}
		else
		{
			ApplyLiteral(Pin, Arg.Value, Statement.LineNumber);
		}
	}

	// `Make Array` with nothing is the empty array, and the node is born with one
	// element: left there, it would be an array of one default value.
	if (UK2Node_MakeArray* MakeArray = Cast<UK2Node_MakeArray>(Node); MakeArray && Statement.Args.Num() == 0)
	{
		const TArray<UEdGraphPin*> Elements = PositionalPins;
		for (UEdGraphPin* Element : Elements)
		{
			MakeArray->RemoveInputPin(Element);
		}
	}
}

void FNodeScribeBuildContext::ApplyDeferredElements(bool bFinal)
{
	TArray<FDeferredElement> Ready;

	DeferredElements.RemoveAll([bFinal, &Ready](const FDeferredElement& Element)
	{
		UEdGraphPin* Pin = Element.Pin.Resolve();
		bool bReady = bFinal || !Pin;

		if (Pin && !bReady)
		{
			for (const UEdGraphPin* Other : Pin->GetOwningNode()->Pins)
			{
				bReady = bReady || (Other->Direction == EGPD_Output && Other->LinkedTo.Num() > 0);
			}
		}

		if (bReady)
		{
			Ready.Add(Element);
		}
		return bReady;
	});

	// With no consumer, the links are the only thing that can give a value its type.
	Ready.StableSort([](const FDeferredElement& A, const FDeferredElement& B)
	{
		return A.bIsReference && !B.bIsReference;
	});

	for (const FDeferredElement& Element : Ready)
	{
		UEdGraphPin* Pin = Element.Pin.Resolve();
		if (!Pin)
		{
			continue;
		}

		const TSet<UEdGraphNode*> NodesBefore(Graph->Nodes);

		if (Element.bIsReference)
		{
			if (UEdGraphPin* Source = ResolveReference(Element.Value, Element.Line, Pin))
			{
				Connect(Source, Pin, Element.Line);
			}
		}
		else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard && !Element.Value.StartsWith(TEXT("?")))
		{
			AddError(Element.Line, FString::Printf(
				TEXT("`%s = %s` did not go in: nothing gives this Make Array a type. Link the array to a pin, or link one of its elements."),
				*Pin->PinName.ToString(), *Element.Value));
		}
		else
		{
			ApplyLiteral(Pin, Element.Value, Element.Line);
		}

		// The Get a reference made by itself is this line's, as in Run.
		for (UEdGraphNode* Added : Graph->Nodes)
		{
			if (Added && !NodesBefore.Contains(Added) && !Result.NodeLines.Contains(Added))
			{
				Result.NodeLines.Add(Added, Element.Line);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Node creation
// ---------------------------------------------------------------------------

UEdGraphNode* FNodeScribeBuildContext::CreateErrorComment(const FNodeScribeStatement& Statement, const FString& Reason)
{
	UEdGraphNode_Comment* Comment = AllocateNode<UEdGraphNode_Comment>();
	FinalizeNode(Comment);

	Comment->NodeComment = FString::Printf(TEXT("NodeScribe could not resolve:\n%s\n\n%s"), *Statement.RawLine, *Reason);
	Comment->CommentColor = FLinearColor(0.65f, 0.12f, 0.12f);
	Comment->NodeWidth = 460;
	Comment->NodeHeight = 160;
	Comment->bCommentBubbleVisible = false;

	// Run() is the one that registers in CreatedNodes, since it gets this node
	// back as the line's result. Adding it here would double the count.
	return Comment;
}

void FNodeScribeBuildContext::PreCreateCustomEvents(const TArray<FNodeScribeStatement>& Statements)
{
	if (!Blueprint)
	{
		return;
	}

	bool bCreatedAny = false;

	for (const FNodeScribeStatement& Statement : Statements)
	{
		if (Statement.bIsLabel || Statement.bIsVariable)
		{
			continue;
		}

		const FString Expression = Statement.NodeExpression.TrimStartAndEnd();

		if (!Expression.StartsWith(TEXT("Event "), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FString EventName = Expression.RightChop(6);
		EventName.TrimStartAndEndInline();

		// `X of Y` is a dispatcher event, and `__DelegateSignature` is refused:
		// neither becomes a Custom Event, so they do not go in here.
		if (EventName.IsEmpty()
			|| EventName.EndsWith(TEXT("__DelegateSignature"), ESearchCase::CaseSensitive)
			|| EventName.Contains(TEXT(" of "), ESearchCase::IgnoreCase)
			|| PreCreatedEvents.Contains(EventName))
		{
			continue;
		}

		// An event the parent class or an interface offers becomes an override,
		// not a Custom Event. Two interfaces offering it is the line's error to
		// say, in the main pass.
		TArray<FString> InterfaceCandidates;
		if (FindEventToImplement(EventName, InterfaceCandidates) || InterfaceCandidates.Num() > 0)
		{
			continue;
		}

		// A parameter with an unknown type: no event here, and the main pass
		// says why on the line.
		TArray<TPair<FName, FEdGraphPinType>> Params;
		if (!ResolveDeclaredEventParams(Statement, Params, /*bReportErrors*/ false))
		{
			continue;
		}

		const FName WantedName(*EventName);
		if (FindExistingEvent([&](UEdGraphNode* Existing)
			{
				const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Existing);
				return CustomEvent && CustomEvent->CustomFunctionName == WantedName;
			}))
		{
			continue;
		}

		UK2Node_CustomEvent* Node = CreateCustomEvent(WantedName, Params);

		PreCreatedEvents.Add(EventName, Node);
		bCreatedAny = true;

		// `Bind X (Event = $done)` may come before `done = event Done`: the
		// reader writes each chain under its own event, in graph order. The
		// name is known now, so it does not become a missing-variable Get.
		if (!Statement.OutputName.IsEmpty())
		{
			if (UEdGraphPin* OutputPin = FindPrimaryOutput(Node))
			{
				NamedOutputs.Add(Statement.OutputName, FPinRef(OutputPin));
			}
		}
	}

	// One regeneration for all of them, instead of one per event.
	if (bCreatedAny)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::GenerateBlueprintSkeleton(Blueprint, true);
	}
}

void FNodeScribeBuildContext::CreateDeclaredVariable(const FNodeScribeStatement& Statement)
{
	if (!Blueprint || Statement.VariableName.IsEmpty())
	{
		return;
	}

	// Declaring again is not an error: pasting the same text twice has to be
	// harmless, and the text the reader produces always carries the declarations.
	if (FindPropertyByFriendlyName(GetSelfClass(), Statement.VariableName))
	{
		AddInfo(Statement.LineNumber, FString::Printf(
			TEXT("`%s` already exists; left it alone."), *Statement.VariableName));
		return;
	}

	FEdGraphPinType PinType;
	if (!ResolvePinTypeFromNameInternal(Statement.VariableType, PinType))
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("Did not recognise type `%s`. Use the name shown in the interface, such as Float, Name, Timer Handle."),
			*Statement.VariableType));
		return;
	}

	// A Designer widget is not created by declaration: the variable is born when
	// the widget is placed on screen with Is Variable ticked. A variable with the
	// same name would compile and never point at the widget -- and would get in
	// the way when the real widget was created.
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		const UClass* WidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.Widget"));
		const UClass* UserWidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.UserWidget"));
		const UClass* VariableClass = Cast<UClass>(PinType.PinSubCategoryObject.Get());

		const bool bIsWidgetBlueprint = UserWidgetClass
			&& Blueprint->ParentClass
			&& Blueprint->ParentClass->IsChildOf(UserWidgetClass);

		if (bIsWidgetBlueprint && WidgetClass && VariableClass && VariableClass->IsChildOf(WidgetClass))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` is a widget: create it in the Designer and tick `Is Variable`, not here."),
				*Statement.VariableName));
			return;
		}
	}

	if (FBlueprintEditorUtils::AddMemberVariable(
		Blueprint, FName(*Statement.VariableName), PinType, Statement.VariableDefault))
	{
		AddInfo(Statement.LineNumber, FString::Printf(
			TEXT("Created variable `%s`."), *Statement.VariableName));
	}
	else
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("Could not create `%s`. The name may be taken by something inherited."),
			*Statement.VariableName));
	}
}

UEdGraphNode* FNodeScribeBuildContext::FindExistingEvent(const TFunctionRef<bool(UEdGraphNode*)>& Matches) const
{
	for (UEdGraphNode* Existing : Graph->Nodes)
	{
		if (Existing && Matches(Existing))
		{
			return Existing;
		}
	}

	return nullptr;
}

UEdGraphNode* FNodeScribeBuildContext::CreateDuplicateEventComment(
	const FNodeScribeStatement& Statement,
	const FString& EventLabel)
{
	AddError(Statement.LineNumber, FString::Printf(
		TEXT("Event `%s` already exists in this graph; Unreal allows only one."), *EventLabel));

	return CreateErrorComment(Statement, FString::Printf(
		TEXT("`%s` already exists in this graph.\n\n")
		TEXT("Unreal allows only one node per event, so I did not create another one nor touch the existing one.\n\n")
		TEXT("This line's chain was left without a start: link it to the existing event, ")
		TEXT("or delete that node before pasting."),
		*EventLabel));
}

UEdGraphNode* FNodeScribeBuildContext::ReuseEmptyEventOrRefuse(
	const FNodeScribeStatement& Statement,
	UEdGraphNode* Existing,
	const FString& EventLabel)
{
	// A second line for the same event in one text is still a duplicate, even
	// when its chain has not been linked yet.
	bool bHasWire = ReusedEvents.Contains(Existing);
	for (const UEdGraphPin* Pin : Existing->Pins)
	{
		if (Pin && Pin->LinkedTo.Num() > 0)
		{
			bHasWire = true;
			break;
		}
	}

	if (bHasWire)
	{
		return CreateDuplicateEventComment(Statement, EventLabel);
	}

	// Inside the paste's transaction, so Ctrl+Z puts the stub back where it was.
	// A disabled stub turns into a real node on its first link, as on screen.
	Existing->Modify();
	ReusedEvents.Add(Existing);

	AddInfo(Statement.LineNumber, FString::Printf(
		TEXT("Used the empty `%s` that was already in this graph."), *EventLabel));

	return Existing;
}

UFunction* FNodeScribeBuildContext::FindEventToImplement(const FString& EventName, TArray<FString>& OutCandidates) const
{
	OutCandidates.Reset();

	if (!Blueprint)
	{
		return nullptr;
	}

	// The Engine prefixes implementable events; the user writes `BeginPlay`,
	// the real function is called `ReceiveBeginPlay`.
	const TArray<FString> Attempts = {
		EventName,
		FString(TEXT("Receive")) + EventName,
		FString(TEXT("K2_")) + EventName
	};

	if (UClass* ParentClass = Blueprint->ParentClass.Get())
	{
		for (const FString& Attempt : Attempts)
		{
			if (UFunction* Found = ParentClass->FindFunctionByName(FName(*Attempt)))
			{
				if (Found->HasAnyFunctionFlags(FUNC_BlueprintEvent))
				{
					return Found;
				}
			}
		}
	}

	// The interfaces this Blueprint implements are not the parent class's, so
	// the search above misses them. Without this, `event X` of an interface
	// came back as a Custom Event called X: it compiles, looks the same, and
	// is never called through the interface.
	//
	// A function with outputs is implemented as a function graph, not as an
	// event, so it does not count here.
	UFunction* Chosen = nullptr;
	for (const FBPInterfaceDescription& Implemented : Blueprint->ImplementedInterfaces)
	{
		UClass* InterfaceClass = Implemented.Interface.Get();
		if (!InterfaceClass)
		{
			continue;
		}

		UFunction* Found = InterfaceClass->FindFunctionByName(FName(*EventName));
		if (Found && UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Found))
		{
			OutCandidates.Add(InterfaceClass->GetName() + TEXT(".") + EventName);
			Chosen = Found;
		}
	}

	if (OutCandidates.Num() > 1)
	{
		return nullptr;
	}

	OutCandidates.Reset();
	return Chosen;
}

bool FNodeScribeBuildContext::ResolveDeclaredEventParams(const FNodeScribeStatement& Statement,
	TArray<TPair<FName, FEdGraphPinType>>& OutParams, bool bReportErrors)
{
	OutParams.Reset();
	TSet<FString> Seen;

	for (const FNodeScribeEventParam& Param : Statement.EventParams)
	{
		FEdGraphPinType PinType;
		if (!ResolvePinTypeFromNameInternal(Param.Type, PinType))
		{
			if (bReportErrors)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Did not recognise type `%s` of parameter `%s`. Use the name shown in the interface, such as Float, Name, Timer Handle."),
					*Param.Type, *Param.Name));
			}
			return false;
		}

		bool bAlreadySeen = false;
		Seen.Add(FNodeScribeCatalog::Normalize(Param.Name), &bAlreadySeen);
		if (bAlreadySeen)
		{
			if (bReportErrors)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Parameter `%s` is declared twice."), *Param.Name));
			}
			return false;
		}

		OutParams.Emplace(FName(*Param.Name), PinType);
	}

	return true;
}

UK2Node_CustomEvent* FNodeScribeBuildContext::CreateCustomEvent(
	const FName& Name, const TArray<TPair<FName, FEdGraphPinType>>& Params)
{
	UK2Node_CustomEvent* Node = AllocateNode<UK2Node_CustomEvent>();
	Node->CustomFunctionName = Name;
	FinalizeNode(Node);

	// After the default pins, as the details panel's `+` does it: a user pin
	// created before AllocateDefaultPins would be made a second time by it.
	for (const TPair<FName, FEdGraphPinType>& Param : Params)
	{
		Node->CreateUserDefinedPin(Param.Key, Param.Value, EGPD_Output, /*bUseUniqueName*/ false);
	}

	return Node;
}

bool FNodeScribeBuildContext::EventSignatureMatches(
	const FNodeScribeStatement& Statement, UEdGraphNode* EventNode, FString& OutActual)
{
	TArray<const UEdGraphPin*> Outputs;
	TArray<FString> ActualParts;

	for (const UEdGraphPin* Pin : EventNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output || Pin->bHidden
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			continue;
		}

		Outputs.Add(Pin);
		ActualParts.Add(FString::Printf(TEXT("%s : %s"),
			*Pin->PinName.ToString(), *DescribePinType(Pin->PinType)));
	}

	OutActual = ActualParts.Num() > 0
		? TEXT("(") + FString::Join(ActualParts, TEXT(", ")) + TEXT(")")
		: TEXT("no parameters");

	if (Outputs.Num() != Statement.EventParams.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		const FNodeScribeEventParam& Declared = Statement.EventParams[Index];

		FEdGraphPinType DeclaredType;
		if (FNodeScribeCatalog::Normalize(Declared.Name) != FNodeScribeCatalog::Normalize(Outputs[Index]->PinName.ToString())
			|| !ResolvePinTypeFromNameInternal(Declared.Type, DeclaredType)
			|| !DescribePinType(DeclaredType).Equals(DescribePinType(Outputs[Index]->PinType), ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	return true;
}

void FNodeScribeBuildContext::WarnOnForeignSignature(
	const FNodeScribeStatement& Statement, UEdGraphNode* EventNode, const FString& EventLabel)
{
	// Nothing declared is the usual case: the reading never writes these.
	if (!EventNode || Statement.EventParams.Num() == 0)
	{
		return;
	}

	FString Actual;
	if (!EventSignatureMatches(Statement, EventNode, Actual))
	{
		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("`%s` takes its parameters from where it is declared, not from this line: it has %s. ")
			TEXT("The declared ones were ignored."),
			*EventLabel, *Actual));
	}
}

// ---------------------------------------------------------------------------
// AnimGraph
// ---------------------------------------------------------------------------

/**
 * A line inside an animation graph.
 *
 * Three forms, in this order: the Output Pose that already exists, an anim node
 * by its menu name, and an animation asset -- which here becomes a node instead
 * of being refused. Outside the AnimGraph a bare asset name is still refused,
 * and for good reason: there is no obvious node to wrap the asset there. Here
 * there is exactly one, and it is the same one dragging the asset into the
 * graph produces.
 *
 * The node class comes before the asset because that vocabulary is closed and
 * the asset one is not: an animation called `Blend` cannot steal the `Blend` node.
 */
UEdGraphNode* FNodeScribeBuildContext::TryCreateAnimNode(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = false;

	const FString Expression = Statement.NodeExpression.TrimStartAndEnd();
	const FString Normalized = FNodeScribeCatalog::Normalize(Expression);

	if (Expression.IsEmpty())
	{
		return nullptr;
	}

	// --- Output Pose ------------------------------------------------------
	if (Normalized == TEXT("outputpose")
		|| Normalized == TEXT("finalanimationpose")
		|| Normalized == TEXT("result"))
	{
		bOutHandled = true;

		if (UEdGraphNode* Existing = NodeScribeAnimGraph::FindOutputPose(Graph))
		{
			AdoptedNodes.Add(Existing);
			return Existing;
		}

		AddError(Statement.LineNumber,
			TEXT("This graph has no Output Pose. It is born with the AnimGraph -- ")
			TEXT("if it is gone, the graph itself is wrong, and creating another one does not fix it."));

		return CreateErrorComment(Statement,
			TEXT("Could not find this graph's Output Pose.\n\n")
			TEXT("It is not created: it is born together with the AnimGraph."));
	}

	// --- Anim node by name ------------------------------------------------
	const NodeScribeAnimGraph::FLookup Lookup = NodeScribeAnimGraph::FindNodeClass(Expression);

	if (Lookup.IsAmbiguous())
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` is the name of more than one anim node: %s."),
			*Expression, *FString::Join(Lookup.Candidates, TEXT(", "))));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("`%s` is ambiguous.\n\nCandidates: %s"),
			*Expression, *FString::Join(Lookup.Candidates, TEXT("\n"))));
	}

	if (Lookup.IsConfident())
	{
		bOutHandled = true;

		UAnimGraphNode_Base* Node = NewObject<UAnimGraphNode_Base>(Graph, Lookup.NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		FinalizeNode(Node);

		// `Locomotion = State Machine` names the machine. A state machine's name
		// is its sub-graph's name, and without this every machine would be born
		// "New State Machine" -- including the second one, which would become
		// "New State Machine 1".
		if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
		{
			if (!Statement.OutputName.IsEmpty() && Machine->EditorStateMachineGraph)
			{
				FBlueprintEditorUtils::RenameGraph(Machine->EditorStateMachineGraph, Statement.OutputName);
			}
		}

		AnimNodes.Add(Node);
		return Node;
	}

	// --- Animation asset --------------------------------------------------
	const NodeScribeAnimGraph::FAssetLookup Asset = NodeScribeAnimGraph::FindAnimationAsset(Expression);

	if (Asset.IsAmbiguous())
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("There is more than one animation called `%s`. Write the full path: %s"),
			*Expression, *FString::Join(Asset.Candidates, TEXT(", "))));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("`%s` is the name of more than one animation.\n\nWrite the path:\n%s"),
			*Expression, *FString::Join(Asset.Candidates, TEXT("\n"))));
	}

	if (!Asset.IsConfident())
	{
		return nullptr;
	}

	UClass* NodeClass = NodeScribeAnimGraph::NodeClassForAsset(Asset.Asset);
	if (!NodeClass)
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` is an animation asset, but there is no AnimGraph node that plays a %s."),
			*Expression, *Asset.Asset->GetClass()->GetName()));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("No node plays `%s`."), *Expression));
	}

	bOutHandled = true;

	UAnimGraphNode_AssetPlayerBase* Player =
		NewObject<UAnimGraphNode_AssetPlayerBase>(Graph, NodeClass, NAME_None, RF_Transactional);
	Graph->AddNode(Player, false, false);
	Player->CreateNewGuid();
	Player->SetAnimationAsset(Asset.Asset);

	// After the asset: a BlendSpace's pins depend on its axes, and allocating
	// before would give a node with the pins of the wrong space.
	FinalizeNode(Player);

	AnimNodes.Add(Player);

	AddInfo(Statement.LineNumber, FString::Printf(
		TEXT("`%s` became a %s."), *Expression,
		*NodeClass->GetName().Replace(TEXT("AnimGraphNode_"), TEXT(""))));

	return Player;
}

UEdGraphNode* FNodeScribeBuildContext::TryCreateAnimGetter(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = false;

	// Whose rule this is. The graph lives inside the transition, and the
	// transition knows which state it leaves from -- which is the answer the
	// getter needs and the text has no way to say.
	UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Graph->GetOuter());
	if (!Transition)
	{
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	// The closest native class: the getters are declared in C++, and an
	// AnimBlueprint that inherits from another AnimBlueprint does not redeclare them.
	UClass* NativeClass = AnimBlueprint->ParentClass;
	while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native))
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	if (!NativeClass)
	{
		return nullptr;
	}

	const FString Wanted = FNodeScribeCatalog::Normalize(Statement.NodeExpression);

	UFunction* Getter = nullptr;
	TArray<FString> Available;

	for (TFieldIterator<UFunction> It(NativeClass); It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasMetaData(TEXT("AnimGetter")) || !Function->HasAnyFunctionFlags(FUNC_Native))
		{
			continue;
		}

		// `GetterContext` says where the getter is valid: `Transition`,
		// `CustomBlend`. Without a context, it is valid anywhere. A getter with
		// the wrong context goes in and does not work, and the Engine only
		// complains much later.
		const FString Context = Function->HasMetaData(TEXT("GetterContext"))
			? Function->GetMetaData(TEXT("GetterContext")) : FString();
		if (!Context.IsEmpty() && !Context.Contains(TEXT("Transition")))
		{
			continue;
		}

		const FString DisplayName = Function->HasMetaData(TEXT("DisplayName"))
			? Function->GetMetaData(TEXT("DisplayName"))
			: FName::NameToDisplayString(Function->GetName(), false);

		Available.Add(DisplayName);

		if (FNodeScribeCatalog::Normalize(Function->GetName()) == Wanted
			|| FNodeScribeCatalog::Normalize(DisplayName) == Wanted)
		{
			Getter = Function;
			break;
		}
	}

	if (!Getter)
	{
		return nullptr;
	}

	bOutHandled = true;

	UAnimStateNodeBase* PreviousState = Transition->GetPreviousState();
	if (!PreviousState)
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` needs to know which state the transition leaves from, and this one is not linked to any."),
			*Statement.NodeExpression));

		return CreateErrorComment(Statement,
			TEXT("The transition has no source state, and this getter reads precisely the source state."));
	}

	// The machine that owns the state. It is the state's mandatory partner:
	// without it the node compiles with "contains invalid data. Please delete
	// and recreate the node."
	UAnimGraphNode_StateMachineBase* MachineNode = nullptr;
	if (UAnimationStateMachineGraph* MachineGraph = Cast<UAnimationStateMachineGraph>(PreviousState->GetOuter()))
	{
		MachineNode = Cast<UAnimGraphNode_StateMachineBase>(MachineGraph->GetOuter());
	}

	UK2Node_AnimGetter* Node = AllocateNode<UK2Node_AnimGetter>();
	Node->SetFromFunction(Getter);
	Node->SourceStateNode = PreviousState;
	Node->SourceNode = MachineNode;
	Node->GetterClass = NativeClass;
	Node->SourceAnimBlueprint = AnimBlueprint;

	// The title is stored, not computed: `GetNodeTitle` returns `CachedTitle`
	// and nothing else, so a node without this shows up with no name at all.
	const FString DisplayName = Getter->HasMetaData(TEXT("DisplayName"))
		? Getter->GetMetaData(TEXT("DisplayName"))
		: FName::NameToDisplayString(Getter->GetName(), false);

	Node->CachedTitle = FText::FromString(FString::Printf(
		TEXT("%s (%s)"), *DisplayName, *PreviousState->GetStateName()));

	Node->Contexts.Add(TEXT("Transition"));

	FinalizeNode(Node);

	AddInfo(Statement.LineNumber, FString::Printf(
		TEXT("`%s` reads state `%s`, which is where this transition leaves from."),
		*Statement.NodeExpression, *PreviousState->GetStateName()));

	return Node;
}

UEdGraphNode* FNodeScribeBuildContext::TryCreateSpecialNode(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = true;

	// --- `$Something` alone on a line -------------------------------------
	//
	// The whole line is a value. It shows up where the block *is* an
	// expression: a transition's rule, whose result is the block's last node.
	//
	// `Get Is In Air` already worked there, and `$Is In Air` -- the form written
	// in every argument -- answered "no node called `$Is In Air`". They are the
	// same thing written two ways, and accepting only one of them forces the
	// writer to find out which, in a place where the error does not say that
	// was the difference.
	if (Statement.NodeExpression.StartsWith(TEXT("$")))
	{
		const FString Reference = Statement.NodeExpression.RightChop(1).TrimStartAndEnd();

		// No consumer pin: nothing receives the value here, so a variable that
		// does not exist is still an error -- the same criterion as a loose
		// `Get X`, and for the same reason (there is nowhere to take the type from).
		if (UEdGraphPin* Pin = ResolveReference(Reference, Statement.LineNumber, nullptr))
		{
			return Pin->GetOwningNodeUnchecked();
		}

		// ResolveReference already said what happened.
		return nullptr;
	}

	// In a transition rule, the state machine getters come before the catalog:
	// the functions of the same name exist and are meant for somewhere else.
	if (bTransitionGraph)
	{
		bool bGetterHandled = false;
		if (UEdGraphNode* Getter = TryCreateAnimGetter(Statement, bGetterHandled))
		{
			return Getter;
		}
		if (bGetterHandled)
		{
			return nullptr;
		}
	}

	// In an animation graph the anim vocabulary comes first: `Blend` there is a
	// pose node, not the math library function of the same name.
	if (bPoseGraph)
	{
		bool bAnimHandled = false;
		if (UEdGraphNode* AnimNode = TryCreateAnimNode(Statement, bAnimHandled))
		{
			return AnimNode;
		}
		if (bAnimHandled)
		{
			return nullptr;
		}
	}

	const FString Expression = Statement.NodeExpression.TrimStartAndEnd();
	const FString Normalized = FNodeScribeCatalog::Normalize(Expression);

	// --- Branch -----------------------------------------------------------
	if (Normalized == TEXT("branch") || Normalized == TEXT("if"))
	{
		UK2Node_IfThenElse* Node = AllocateNode<UK2Node_IfThenElse>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Sequence ---------------------------------------------------------
	if (Normalized == TEXT("sequence"))
	{
		UK2Node_ExecutionSequence* Node = AllocateNode<UK2Node_ExecutionSequence>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Return -----------------------------------------------------------
	if (Normalized == TEXT("return"))
	{
		UK2Node_FunctionResult* Node = AllocateNode<UK2Node_FunctionResult>();
		FinalizeNode(Node);
		return Node;
	}

	// --- To Text ----------------------------------------------------------
	//
	// A node of its own, not the function of the same name: the function is
	// `BlueprintInternalUseOnly` and is not in the catalog. Without this case
	// the line the reader writes would have no way back.
	if (Normalized == TEXT("totext"))
	{
		UK2Node_GenericToText* Node = AllocateNode<UK2Node_GenericToText>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Self -------------------------------------------------------------
	if (Normalized == TEXT("self"))
	{
		UK2Node_Self* Node = AllocateNode<UK2Node_Self>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Free comment -----------------------------------------------------
	if (Expression.StartsWith(TEXT("Comment "), ESearchCase::IgnoreCase))
	{
		const int32 SpaceIndex = Expression.Find(TEXT(" "));
		UEdGraphNode_Comment* Node = AllocateNode<UEdGraphNode_Comment>();
		FinalizeNode(Node);
		Node->NodeComment = Expression.Mid(SpaceIndex + 1);
		Node->NodeWidth = 400;
		Node->NodeHeight = 140;
		return Node;
	}

	// --- Cast to <Class> --------------------------------------------------
	if (Expression.StartsWith(TEXT("Cast to "), ESearchCase::IgnoreCase))
	{
		FString ClassName = Expression.RightChop(8);
		ClassName.TrimStartAndEndInline();

		if (!ClassName.IsEmpty())
		{
			UClass* TargetClass = FindClassByFriendlyNameInternal(ClassName);

			if (!TargetClass)
			{
				return CreateErrorComment(Statement,
					FString::Printf(TEXT("Could not find class `%s`."), *ClassName));
			}

			UK2Node_DynamicCast* Node = AllocateNode<UK2Node_DynamicCast>();
			Node->TargetType = TargetClass;
			Node->SetPurity(false);
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- Event <Name> -----------------------------------------------------
	{
		FString EventName;
		if (Expression.StartsWith(TEXT("Event "), ESearchCase::IgnoreCase))
		{
			EventName = Expression.RightChop(6);
		}

		if (!EventName.IsEmpty())
		{
			EventName.TrimStartAndEndInline();

			TArray<FString> InterfaceCandidates;
			UFunction* EventFunction = FindEventToImplement(EventName, InterfaceCandidates);

			if (InterfaceCandidates.Num() > 1)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s` is an event of more than one interface this Blueprint implements: %s. ")
					TEXT("The format cannot say which; create it by hand."),
					*EventName, *FString::Join(InterfaceCandidates, TEXT(", "))));

				return CreateErrorComment(Statement, FString::Printf(
					TEXT("`%s` exists in more than one interface (%s), and I did not choose."),
					*EventName, *FString::Join(InterfaceCandidates, TEXT(", "))));
			}

			if (EventFunction)
			{
				const FName WantedEvent = EventFunction->GetFName();

				UEdGraphNode* Node = nullptr;

				if (UEdGraphNode* Existing = FindExistingEvent([&](UEdGraphNode* Candidate)
					{
						const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Candidate);
						return EventNode
							&& EventNode->bOverrideFunction
							&& EventNode->EventReference.GetMemberName() == WantedEvent;
					}))
				{
					Node = ReuseEmptyEventOrRefuse(Statement, Existing, EventName);
				}
				else
				{
					UK2Node_Event* NewNode = AllocateNode<UK2Node_Event>();
					NewNode->EventReference.SetExternalMember(EventFunction->GetFName(), EventFunction->GetOwnerClass());
					NewNode->bOverrideFunction = true;
					FinalizeNode(NewNode);
					Node = NewNode;
				}

				if (Cast<UK2Node_Event>(Node))
				{
					WarnOnForeignSignature(Statement, Node, EventName);
				}
				return Node;
			}

			// --- event <Dispatcher> of <Variable> ------------------------
			// The missing form: the red node from before said "recreate it by
			// hand" precisely because this block did not exist.
			{
				FString DelegateName;
				FString ComponentName;

				const bool bHasOwner =
					EventName.Split(TEXT(" of "), &DelegateName, &ComponentName, ESearchCase::IgnoreCase);

				if (bHasOwner)
				{
					DelegateName.TrimStartAndEndInline();
					ComponentName.TrimStartAndEndInline();

					UClass* SelfClass = GetSelfClass();

					FObjectProperty* ComponentProperty = SelfClass
						? FindFProperty<FObjectProperty>(SelfClass, FName(*ComponentName))
						: nullptr;

					if (!ComponentProperty)
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` is not an object variable of this Blueprint."), *ComponentName));

						return CreateErrorComment(Statement, FString::Printf(
							TEXT("`%s` must be a variable of this Blueprint that points at another object."),
							*ComponentName));
					}

					FMulticastDelegateProperty* DelegateProperty =
						FindDelegateByFriendlyName(ComponentProperty->PropertyClass, DelegateName);

					if (!DelegateProperty)
					{
						TArray<FString> Available;
						for (TFieldIterator<FMulticastDelegateProperty> It(ComponentProperty->PropertyClass); It; ++It)
						{
							Available.Add(It->GetName());
						}

						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` has no dispatcher `%s`. Dispatchers: %s"),
							*ComponentName, *DelegateName,
							Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("none")));

						return CreateErrorComment(Statement, FString::Printf(
							TEXT("`%s` does not expose a dispatcher called `%s`."), *ComponentName, *DelegateName));
					}

					const FName WantedDelegate = DelegateProperty->GetFName();
					const FName WantedComponent = ComponentProperty->GetFName();

					if (UEdGraphNode* Existing = FindExistingEvent([&](UEdGraphNode* Candidate)
						{
							const UK2Node_ComponentBoundEvent* Bound = Cast<UK2Node_ComponentBoundEvent>(Candidate);
							return Bound
								&& Bound->DelegatePropertyName == WantedDelegate
								&& Bound->GetComponentPropertyName() == WantedComponent;
						}))
					{
						UEdGraphNode* Reused = ReuseEmptyEventOrRefuse(Statement, Existing,
							FString::Printf(TEXT("%s of %s"), *DelegateName, *ComponentName));

						if (Cast<UK2Node_Event>(Reused))
						{
							WarnOnForeignSignature(Statement, Reused, EventName);
						}
						return Reused;
					}

					UK2Node_ComponentBoundEvent* Node = AllocateNode<UK2Node_ComponentBoundEvent>();
					Node->InitializeComponentBoundEventParams(ComponentProperty, DelegateProperty);
					FinalizeNode(Node);
					WarnOnForeignSignature(Statement, Node, EventName);
					return Node;
				}
			}

			// `__DelegateSignature` is the suffix the Engine puts on a delegate's
			// signature function. A Custom Event with that name is not something
			// anyone would write: it is a dispatcher event that lost its binding.
			// Creating it anyway would give a plausible, dead node, which is
			// exactly the result this plugin refuses to produce.
			if (EventName.EndsWith(TEXT("__DelegateSignature"), ESearchCase::CaseSensitive))
			{
				FString DispatcherName = EventName;
				DispatcherName.RemoveFromEnd(TEXT("__DelegateSignature"));

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s` is a dispatcher/delegate event. The format cannot recreate that binding yet."),
					*DispatcherName));

				return CreateErrorComment(Statement, FString::Printf(
					TEXT("`%s` is an event bound to a dispatcher/delegate.\n\n")
					TEXT("A Custom Event with that name would compile and never fire, so I created none.\n\n")
					TEXT("To do it by hand: right-click the graph, search for `%s`, and pick the event option."),
					*DispatcherName, *DispatcherName));
			}

			// The pre-pass already created this event so that lines above could
			// call it. Here we just hand over the same node.
			if (UEdGraphNode** PreCreated = PreCreatedEvents.Find(EventName))
			{
				return *PreCreated;
			}

			const FName WantedCustomEvent(*EventName);

			if (UEdGraphNode* Existing = FindExistingEvent([&](UEdGraphNode* Candidate)
				{
					const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Candidate);
					return CustomEvent && CustomEvent->CustomFunctionName == WantedCustomEvent;
				}))
			{
				// Reused only with the parameters the line asks for. Changing them
				// would break every call to the event elsewhere, and keeping the old
				// ones would leave this line's `$x.Pin` pointing at nothing.
				FString Actual;
				if (!EventSignatureMatches(Statement, Existing, Actual))
				{
					AddError(Statement.LineNumber, FString::Printf(
						TEXT("Custom Event `%s` already exists in this graph with %s. Nothing was created or changed."),
						*EventName, *Actual));

					return CreateErrorComment(Statement, FString::Printf(
						TEXT("`%s` already exists with %s, which is not what this line declares.\n\n")
						TEXT("I did not touch it: change its parameters by hand, or delete it before pasting."),
						*EventName, *Actual));
				}

				return ReuseEmptyEventOrRefuse(Statement, Existing, EventName);
			}

			TArray<TPair<FName, FEdGraphPinType>> Params;
			if (!ResolveDeclaredEventParams(Statement, Params, /*bReportErrors*/ true))
			{
				return CreateErrorComment(Statement, FString::Printf(
					TEXT("`%s` declares a parameter I could not create, so I created no event."), *EventName));
			}

			UK2Node_CustomEvent* Node = CreateCustomEvent(WantedCustomEvent, Params);

			// Without this, a later line that CALLS this event does not find it:
			// the skeleton class only gets the function when it is regenerated,
			// and nobody compiles in the middle of a paste. It costs little --
			// there are few events per text -- and it is what makes `Jump`
			// callable in the same paste that declared it.
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::GenerateBlueprintSkeleton(Blueprint, true);

			UClass* SelfParentClass = Blueprint ? Blueprint->ParentClass.Get() : nullptr;

			if (UClass* OwningClass = FindClassOwningBlueprintEvent(EventName))
			{
				// The event exists -- only in another hierarchy. A Custom Event with
				// the name of an Engine event compiles, looks plausible in the graph
				// and never fires. That is a warning, not a footnote.
				FString Message = FString::Printf(
					TEXT("`%s` is an event of `%s`, but this Blueprint derives from `%s`. ")
					TEXT("I created a Custom Event with that name, and it will never fire by itself."),
					*EventName,
					*OwningClass->GetName(),
					SelfParentClass ? *SelfParentClass->GetName() : TEXT("?"));

				const FString Available = ListAvailableEvents(SelfParentClass, 8);
				if (!Available.IsEmpty())
				{
					Message += FString::Printf(TEXT(" Available here: %s."), *Available);
				}

				AddWarning(Statement.LineNumber, Message);
			}
			else
			{
				AddInfo(Statement.LineNumber, FString::Printf(
					TEXT("`%s` does not exist in the parent class; created a Custom Event with that name."), *EventName));
			}

			return Node;
		}
	}

	// --- Select ----------------------------------------------------------
	if (Normalized == TEXT("select"))
	{
		UK2Node_Select* Node = AllocateNode<UK2Node_Select>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Make Array ------------------------------------------------------
	if (Normalized == TEXT("makearray"))
	{
		UK2Node_MakeArray* Node = AllocateNode<UK2Node_MakeArray>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Call / Bind / Unbind / Clear dispatcher -------------------------
	{
		struct FDelegateForm
		{
			const TCHAR* Prefix;
			int32 PrefixLength;
			UClass* (*MakeClass)();
		};

		static const FDelegateForm DelegateForms[] = {
			{ TEXT("Call "),        5,  []() { return UK2Node_CallDelegate::StaticClass(); } },
			{ TEXT("Bind "),        5,  []() { return UK2Node_AddDelegate::StaticClass(); } },
			{ TEXT("Unbind "),      7,  []() { return UK2Node_RemoveDelegate::StaticClass(); } },
			{ TEXT("Clear "),       6,  []() { return UK2Node_ClearDelegate::StaticClass(); } }
		};

		for (const FDelegateForm& Form : DelegateForms)
		{
			if (!Expression.StartsWith(Form.Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}

			FString DelegateName = Expression.RightChop(Form.PrefixLength);
			DelegateName.TrimStartAndEndInline();

			// Without `Target`, the dispatcher belongs to this Blueprint. With
			// it, to the object pointed at -- same rule as `Set X (Target = $obj)`.
			UClass* OwnerClass = FindTargetClassFromArgs(Statement);
			const bool bSelfContext = (OwnerClass == nullptr);

			if (bSelfContext)
			{
				OwnerClass = GetSelfClass();
			}

			FMulticastDelegateProperty* DelegateProperty =
				FindDelegateByFriendlyName(OwnerClass, DelegateName);

			if (!DelegateProperty)
			{
				// Not a dispatcher: it may be a function that just starts with
				// "Clear" or "Call". Let the catalog try.
				break;
			}

			UK2Node_BaseMCDelegate* Node = static_cast<UK2Node_BaseMCDelegate*>(
				NewObject<UEdGraphNode>(Graph, Form.MakeClass(), NAME_None, RF_Transactional));

			Graph->AddNode(Node, false, false);
			Node->CreateNewGuid();
			Node->SetFromProperty(DelegateProperty, bSelfContext, OwnerClass);
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- Switch on <Enum|Int|String|Name> --------------------------------
	{
		FString SwitchOn;
		bool bExplicitSwitchOn = false;

		if (Expression.StartsWith(TEXT("Switch on "), ESearchCase::IgnoreCase))
		{
			SwitchOn = Expression.RightChop(10);
			bExplicitSwitchOn = true;
		}
		else if (Expression.StartsWith(TEXT("Switch "), ESearchCase::IgnoreCase))
		{
			SwitchOn = Expression.RightChop(7);
		}

		SwitchOn.TrimStartAndEndInline();

		if (!SwitchOn.IsEmpty())
		{
			const FString NormalizedSwitch = FNodeScribeCatalog::Normalize(SwitchOn);

			if (NormalizedSwitch == TEXT("int") || NormalizedSwitch == TEXT("integer"))
			{
				UK2Node_SwitchInteger* Node = AllocateNode<UK2Node_SwitchInteger>();
				FinalizeNode(Node);
				return Node;
			}

			if (NormalizedSwitch == TEXT("string"))
			{
				UK2Node_SwitchString* Node = AllocateNode<UK2Node_SwitchString>();
				FinalizeNode(Node);
				return Node;
			}

			if (NormalizedSwitch == TEXT("name"))
			{
				UK2Node_SwitchName* Node = AllocateNode<UK2Node_SwitchName>();
				FinalizeNode(Node);
				return Node;
			}

			// What is left is an enum. Each of its values becomes an execution
			// output, so the enum has to be set before allocating the pins.
			UEnum* Enum = nullptr;
			for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
			{
				if (FNodeScribeCatalog::Normalize(EnumIt->GetName()) == NormalizedSwitch)
				{
					Enum = *EnumIt;
					break;
				}
			}

			if (Enum)
			{
				UK2Node_SwitchEnum* Node = AllocateNode<UK2Node_SwitchEnum>();
				Node->SetEnum(Enum);
				FinalizeNode(Node);
				return Node;
			}

			// Only `Switch on X` promises an enum. A bare `Switch X` may be a
			// standard macro -- `Switch Has Authority` -- and has to reach the
			// macro lookup instead of dying here as "enum not found".
			if (bExplicitSwitchOn)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Could not find enum `%s`."), *SwitchOn));

				return CreateErrorComment(Statement, FString::Printf(
					TEXT("Enum `%s` not found.\n\nUse its exact name, such as `EJSL4UBatteryLevel`."),
					*SwitchOn));
			}
		}
	}

	// --- EnhancedInputAction <Action> ------------------------------------
	if (Expression.StartsWith(TEXT("EnhancedInputAction "), ESearchCase::IgnoreCase))
	{
		FString ActionName = Expression.RightChop(20);
		ActionName.TrimStartAndEndInline();

		// Resolved by path so as not to drag the InputBlueprintNodes module in as
		// a dependency: the plugin must not require Enhanced Input to be installed.
		UClass* NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
		if (!NodeClass)
		{
			AddError(Statement.LineNumber, TEXT("The Enhanced Input plugin is not enabled in this project."));
			return CreateErrorComment(Statement, TEXT("Enhanced Input is not enabled."));
		}

		UObject* Action = FindAssetByPathOrName(TEXT("/Script/EnhancedInput.InputAction"), ActionName);
		if (!Action)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Could not find Input Action `%s`. Use the full path if it is not open."), *ActionName));

			return CreateErrorComment(Statement, FString::Printf(
				TEXT("Input Action `%s` not found.\n\nUse the full path, something like\n/Game/.../IA_Attack.IA_Attack"),
				*ActionName));
		}

		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();

		// The action defines which trigger pins exist (Started, Triggered,
		// Completed...), so it has to be set before allocating pins.
		if (FObjectProperty* ActionProperty = FindFProperty<FObjectProperty>(NodeClass, TEXT("InputAction")))
		{
			ActionProperty->SetObjectPropertyValue_InContainer(Node, Action);
		}

		FinalizeNode(Node);
		return Node;
	}

	// --- Create Widget / Spawn Actor from Class --------------------------
	if (UClass* NodeClass = FindConstructNodeClass(Normalized))
	{
		UK2Node_ConstructObjectFromClass* Node = NewObject<UK2Node_ConstructObjectFromClass>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		FinalizeNode(Node);

		// The class has to go in before the other arguments: it is what makes the
		// Expose on Spawn pins exist. Applied in the normal order, the other
		// arguments would arrive before their pins and become "the node has no
		// pin X" -- an error that would explain nothing.
		const FNodeScribeArg* ClassArg = nullptr;
		for (const FNodeScribeArg& Arg : Statement.Args)
		{
			if (FNodeScribeCatalog::Normalize(Arg.PinName) == TEXT("class"))
			{
				ClassArg = &Arg;
				break;
			}
		}

		// Without a pin name, the first argument is the class: `Create Widget (WBP_X)`.
		if (!ClassArg && Statement.Args.Num() > 0 && Statement.Args[0].PinName.IsEmpty())
		{
			ClassArg = &Statement.Args[0];
		}

		// The class from a variable or another node: the wire is what makes the
		// pins exist, so it goes in now. Read as a class name, `$Rock Class`
		// found no class and every Expose on Spawn value after it was lost.
		if (ClassArg && ClassArg->bIsReference)
		{
			if (UEdGraphPin* ClassPin = Node->GetClassPin())
			{
				if (UEdGraphPin* Source = ResolveReference(ClassArg->Value, Statement.LineNumber, ClassPin))
				{
					Connect(Source, ClassPin, Statement.LineNumber);
					ArgsAppliedEarly.Add(ClassArg);
				}
			}
			return Node;
		}

		const FString ClassValue = ClassArg ? ClassArg->Value : FString();

		if (ClassValue.IsEmpty() || ClassValue.StartsWith(TEXT("?")))
		{
			AddWarning(Statement.LineNumber,
				TEXT("Without `Class`, this node has no Expose on Spawn pins. Pick the class on it."));
			return Node;
		}

		UClass* SpawnClass = ClassValue.StartsWith(TEXT("/"))
			? LoadObject<UClass>(nullptr, *ClassValue)
			: FindClassByFriendlyNameInternal(ClassValue);

		if (!SpawnClass)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Could not find class `%s`, so the Expose on Spawn pins do not exist."), *ClassValue));
			return Node;
		}

		if (UEdGraphPin* ClassPin = Node->GetClassPin())
		{
			Schema->TrySetDefaultObject(*ClassPin, SpawnClass);

			// `PinDefaultValueChanged` is the hook the editor fires when you pick
			// the class in the dropdown, and it is what creates the Expose on
			// Spawn pins. `ReconstructNode` alone rebuilds the node with the
			// default pins -- the title even changes, which is misleading, but
			// the pins do not come.
			Node->PinDefaultValueChanged(ClassPin);
		}

		return Node;
	}

	// --- Struct Make / Break ---------------------------------------------
	{
		FString StructName;
		bool bIsBreak = false;

		if (Expression.StartsWith(TEXT("Make "), ESearchCase::IgnoreCase))
		{
			StructName = Expression.RightChop(5);
		}
		else if (Expression.StartsWith(TEXT("Break "), ESearchCase::IgnoreCase))
		{
			StructName = Expression.RightChop(6);
			bIsBreak = true;
		}

		StructName.TrimStartAndEndInline();

		// Only becomes a struct node if the struct really exists. `Make Literal
		// Int` and friends keep falling into the function catalog, where they live.
		if (UScriptStruct* Struct = FindStructByFriendlyName(StructName))
		{
			// Some structs bring their own break or make function, and the Engine
			// warns at compile time when the generic node is used on one of them:
			// "The structure cannot be broken using generic 'break' node. Try use
			// specialized 'break' function if available."
			//
			// The warning is free for whoever writes the text -- the format cannot
			// even choose the specialised node --, so the choice is made here.
			// `Vector`, `Rotator` and `Transform`, the most written ones, are all
			// in that case.
			const TCHAR* const MetaKey = bIsBreak ? TEXT("HasNativeBreak") : TEXT("HasNativeMake");
			if (Struct->HasMetaData(MetaKey))
			{
				const FString FunctionPath = Struct->GetMetaData(MetaKey);
				if (UFunction* Native = FindObject<UFunction>(nullptr, *FunctionPath))
				{
					UK2Node_CallFunction* Node = AllocateNode<UK2Node_CallFunction>();
					Node->SetFromFunction(Native);
					FinalizeNode(Node);
					return Node;
				}
			}

			if (bIsBreak)
			{
				UK2Node_BreakStruct* Node = AllocateNode<UK2Node_BreakStruct>();
				Node->StructType = Struct;
				Node->bMadeAfterOverridePinRemoval = true;
				FinalizeNode(Node);
				return Node;
			}

			UK2Node_MakeStruct* Node = AllocateNode<UK2Node_MakeStruct>();
			Node->StructType = Struct;
			Node->bMadeAfterOverridePinRemoval = true;
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- Variable Get / Set ----------------------------------------------
	{
		FString VariableName;
		bool bIsSetter = false;

		if (Expression.StartsWith(TEXT("Get "), ESearchCase::IgnoreCase))
		{
			VariableName = Expression.RightChop(4);
		}
		else if (Expression.StartsWith(TEXT("Set "), ESearchCase::IgnoreCase))
		{
			VariableName = Expression.RightChop(4);
			bIsSetter = true;
		}

		VariableName.TrimStartAndEndInline();

		// Only treated as a variable if it really exists. That way `Get Player
		// Controller` keeps falling into the function catalog, where it lives.
		// Read-only does not become a Set. `LeaderPoseComponent` is
		// `BlueprintReadOnly` and used to go in as a setter anyway -- hiding the
		// function `SetLeaderPoseComponent`, which is what does the real work
		// (the bone map between the two meshes). The node went in, and what it
		// does is not what the line asked for. Blocked here, the line falls into
		// the catalog and finds the function, where it should always have fallen.
		const bool bSetterBlocked = bIsSetter && !IsVariableWritable(VariableName);

		// The arguments decide between a variable and a function with the same
		// name. `Set Visibility (Target = $Row, In Visibility = Visible)` is the
		// reader's line for `UWidget::SetVisibility`, and a Widget Blueprint is a
		// widget with a `Visibility` property of its own: taken as that variable,
		// the line lost its Target and died on `In Visibility`. A variable node
		// has the variable's own pin and nothing else -- plus Target, when the
		// variable belongs to another object. Any other name means a function.
		auto ArgsFitVariable = [&Statement, &VariableName](const FProperty* Property, bool bExternal)
		{
			for (const FNodeScribeArg& Arg : Statement.Args)
			{
				if (Arg.PinName.IsEmpty())
				{
					continue;
				}

				const FString Written = ResolvePinAlias(Arg.PinName);
				if (Written == TEXT("self"))
				{
					if (bExternal)
					{
						continue;
					}
					return false;
				}

				if (Written == FNodeScribeCatalog::Normalize(VariableName)
					|| (Property && (Written == FNodeScribeCatalog::Normalize(Property->GetName())
						|| Written == FNodeScribeCatalog::Normalize(Property->GetDisplayNameText().ToString()))))
				{
					continue;
				}
				return false;
			}
			return true;
		};

		if (IsBlueprintVariable(VariableName) && !bSetterBlocked
			&& ArgsFitVariable(FindPropertyByFriendlyName(GetSelfClass(), VariableName), false))
		{
			if (bIsSetter)
			{
				UK2Node_VariableSet* Node = AllocateNode<UK2Node_VariableSet>();
				Node->VariableReference.SetSelfMember(FName(*VariableName));
				FinalizeNode(Node);
				return Node;
			}

			UK2Node_VariableGet* Node = AllocateNode<UK2Node_VariableGet>();
			Node->VariableReference.SetSelfMember(FName(*VariableName));
			FinalizeNode(Node);
			return Node;
		}

		// A variable of ANOTHER object: `Set Show Mouse Cursor (Target = $pc, ...)`.
		// The class comes from the pin `Target` points at -- it is the same
		// information the editor uses when you drag from an object pin and ask
		// for the Set.
		if (!VariableName.IsEmpty())
		{
			if (UClass* TargetClass = FindTargetClassFromArgs(Statement))
			{
				FProperty* Property = FindPropertyByFriendlyName(TargetClass, VariableName);

				// Same reason as the block above, on the other side of `Target`.
				if (Property && bIsSetter
					&& Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst))
				{
					Property = nullptr;
				}

				if (Property && !ArgsFitVariable(Property, true))
				{
					Property = nullptr;
				}

				if (Property)
				{
					if (bIsSetter)
					{
						UK2Node_VariableSet* Node = AllocateNode<UK2Node_VariableSet>();
						Node->VariableReference.SetExternalMember(Property->GetFName(), TargetClass);
						FinalizeNode(Node);
						return Node;
					}

					UK2Node_VariableGet* Node = AllocateNode<UK2Node_VariableGet>();
					Node->VariableReference.SetExternalMember(Property->GetFName(), TargetClass);
					FinalizeNode(Node);
					return Node;
				}
			}
		}

		// Not a variable, but it may be a subsystem: `Get EnhancedInputLocalPlayerSubsystem`.
		// These nodes are not function calls and would never show up in the catalog.
		if (!bIsSetter && !VariableName.IsEmpty())
		{
			if (UClass* SubsystemClass = FindClassByFriendlyNameInternal(VariableName))
			{
				if (SubsystemClass->IsChildOf(USubsystem::StaticClass()))
				{
					UClass* NodeClass = ChooseSubsystemNodeClass(SubsystemClass);

					UK2Node_GetSubsystem* Node = NewObject<UK2Node_GetSubsystem>(Graph, NodeClass, NAME_None, RF_Transactional);
					Graph->AddNode(Node, false, false);
					Node->CreateNewGuid();
					Node->Initialize(SubsystemClass);
					FinalizeNode(Node);
					return Node;
				}
			}
		}
	}

	bOutHandled = false;
	return nullptr;
}

UEdGraphNode* FNodeScribeBuildContext::CreateNodeForStatement(const FNodeScribeStatement& Statement)
{
	bool bHandled = false;
	if (UEdGraphNode* Special = TryCreateSpecialNode(Statement, bHandled))
	{
		return Special;
	}

	if (bHandled)
	{
		// A special handler recognised the form but already reported the problem.
		return nullptr;
	}

	// The Target's class breaks ties the name cannot: `Set Visibility (Target =
	// $Panel)` exists on Widget, SceneComponent and two more, and the pin it is
	// wired to says which one the line means. It is what the editor itself
	// does when you drag from an object pin.
	const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(
		Statement.NodeExpression, GetSelfClass(), FindTargetClassFromArgs(Statement));

	// Standard library macros (ForEachLoop, DoOnce, Gate...).
	//
	// Naming the output (`x = ...`) gives priority to a PURE function of the
	// same name: `valid = Is Valid (...)` wants the bool, not the macro. But only
	// when that function really exists -- `loop = For Each Loop (...)` names the
	// output to be able to write `$loop.Array Element`, and there is no function
	// with that name. Before, the name alone discarded the macro, and the named
	// ForEachLoop was simply not found.
	const bool bPureFunctionWins = !Statement.OutputName.IsEmpty()
		&& Lookup.IsConfident()
		&& Lookup.Function->HasAnyFunctionFlags(FUNC_BlueprintPure);

	if (!bPureFunctionWins)
	{
		if (UEdGraph* MacroGraph = FindStandardMacroGraph(Statement.NodeExpression, GetSelfClass()))
		{
			UK2Node_MacroInstance* Node = AllocateNode<UK2Node_MacroInstance>();
			Node->SetMacroGraph(MacroGraph);
			FinalizeNode(Node);
			return Node;
		}
	}

	if (Lookup.IsConfident())
	{
		// An async action is not a function call: it is a node of its own, with
		// one execution output per proxy delegate (`On Connected`,
		// `On Disconnected`). Those outputs are already regular execution pins,
		// so the format's indented labels work with nothing new.
		if (FNodeScribeCatalog::IsAsyncActionFactory(Lookup.Function))
		{
			UK2Node_AsyncAction* Node = AllocateNode<UK2Node_AsyncAction>();

			// Before allocating the pins: the proxy's delegates say which pins
			// exist, and the proxy is only known after this line.
			Node->InitializeProxyFromFunction(Lookup.Function);
			FinalizeNode(Node);
			return Node;
		}

		// A function of an interface is called as a message, as the editor's
		// menu offers it: its Target takes any object, and an object that does
		// not implement the interface simply does nothing. As a plain call the
		// Target wants an interface reference, and an Actor variable's wire
		// was dropped on the way back.
		if (const UClass* Owner = Lookup.Function->GetOwnerClass();
			Owner && Owner->HasAnyClassFlags(CLASS_Interface))
		{
			UK2Node_Message* Node = AllocateNode<UK2Node_Message>();
			Node->SetFromFunction(Lookup.Function);
			FinalizeNode(Node);
			return Node;
		}

		// Append, the math operators and the like are their own node class in
		// the editor, the one with the "Add pin" button. As a plain call they
		// are stuck with two inputs, and the reader's `Append (A, B, C)` from a
		// real graph did not paste back.
		UK2Node_CallFunction* Node = Lookup.Function->HasMetaData(
			FBlueprintMetadata::MD_CommutativeAssociativeBinaryOperator)
			? AllocateNode<UK2Node_CommutativeAssociativeBinaryOperator>()
			: AllocateNode<UK2Node_CallFunction>();
		Node->SetFromFunction(Lookup.Function);
		FinalizeNode(Node);
		return Node;
	}

	if (Lookup.IsAmbiguous())
	{
		const FString Reason = FString::Printf(
			TEXT("More than one function matches. Write the exact name of one of them:\n  %s"),
			*FString::Join(Lookup.Candidates, TEXT("\n  ")));

		// Ambiguous is also missing vocabulary: the short name exists and is not
		// enough to decide. Either an alias is missing, or the format lacks a
		// way to break the tie.
		RecordUnresolvedName(TEXT("ambiguous"), Statement.NodeExpression);

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` is ambiguous. Candidates: %s"),
			*Statement.NodeExpression, *FString::Join(Lookup.Candidates, TEXT(" | "))));

		return CreateErrorComment(Statement, Reason);
	}

	// The line starts with a dispatcher verb and still did not become a node:
	// instead of repeating "not found", say which dispatchers the class really
	// has. It is the difference between "I got the name wrong" and "that
	// feature does not exist".
	FString DelegateHint;
	{
		static const TCHAR* const DelegateVerbs[] = {
			TEXT("Call "), TEXT("Bind "), TEXT("Unbind "), TEXT("Clear ")
		};

		const FString Expression = Statement.NodeExpression.TrimStartAndEnd();

		for (const TCHAR* Verb : DelegateVerbs)
		{
			if (!Expression.StartsWith(Verb, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TArray<FString> Available;
			if (UClass* SelfClass = GetSelfClass())
			{
				for (TFieldIterator<FMulticastDelegateProperty> It(SelfClass); It; ++It)
				{
					Available.Add(It->GetName());
				}
			}

			DelegateHint = Available.Num() > 0
				? FString::Printf(TEXT("\n\nIf it was a dispatcher: this Blueprint's are %s"),
					*FString::Join(Available, TEXT(", ")))
				: TEXT("\n\nIf it was a dispatcher: this Blueprint has none.");

			break;
		}
	}

	RecordUnresolvedName(TEXT("node"), Statement.NodeExpression);

	AddError(Statement.LineNumber, FString::Printf(
		TEXT("Could not find any node called `%s`.%s"),
		*Statement.NodeExpression, *DelegateHint.Replace(TEXT("\n\n"), TEXT(" "))));

	return CreateErrorComment(Statement,
		TEXT("No node with that name was found.") + DelegateHint);
}

namespace
{
	/**
	 * `state Idle` -> "Idle". Without the prefix, it is not a state declaration.
	 *
	 * The prefix is mandatory on purpose: inside a state machine a bare label
	 * would be indistinguishable from a pose input, and choosing by the shape of
	 * the name would be guessing.
	 */
	bool ParseStateLabel(const FString& Label, FString& OutName)
	{
		if (Label.StartsWith(TEXT("state "), ESearchCase::IgnoreCase))
		{
			OutName = Label.Mid(6).TrimStartAndEnd();
			return !OutName.IsEmpty();
		}
		return false;
	}

	/**
	 * `alias To Air` -> "To Air".
	 *
	 * An alias is a nickname for several states at once: a transition leaving
	 * it leaves all of them, without repeating the rule on each one. It has no
	 * sub-graph -- its block is the list of states it aliases, one per line.
	 */
	bool ParseAliasLabel(const FString& Label, FString& OutName)
	{
		if (Label.StartsWith(TEXT("alias "), ESearchCase::IgnoreCase))
		{
			OutName = Label.Mid(6).TrimStartAndEnd();
			return !OutName.IsEmpty();
		}
		return false;
	}

	/**
	 * `conduit To Fall` -> "To Fall".
	 *
	 * A conduit holds no pose: it is a crossing with a single rule, which
	 * several transitions go through instead of each repeating the same
	 * condition. Without a word of its own, `conduit X:` would be
	 * indistinguishable from `state X:` -- and the difference matters, because
	 * the block of one is a pose and the block of the other is a rule.
	 */
	bool ParseConduitLabel(const FString& Label, FString& OutName)
	{
		if (Label.StartsWith(TEXT("conduit "), ESearchCase::IgnoreCase))
		{
			OutName = Label.Mid(8).TrimStartAndEnd();
			return !OutName.IsEmpty();
		}
		return false;
	}

	/** `Idle -> Running`, with or without `transition` in front. */
	bool ParseTransitionLabel(const FString& Label, FString& OutFrom, FString& OutTo)
	{
		FString Rest = Label;
		if (Rest.StartsWith(TEXT("transition "), ESearchCase::IgnoreCase))
		{
			Rest = Rest.Mid(11);
		}

		if (!Rest.Split(TEXT("->"), &OutFrom, &OutTo))
		{
			return false;
		}

		OutFrom.TrimStartAndEndInline();
		OutTo.TrimStartAndEndInline();
		return !OutFrom.IsEmpty() && !OutTo.IsEmpty();
	}

	/** Where the states go in the sub-graph: a row, with room for the wires. */
	constexpr int32 StateColumnWidth = 320;
}

UEdGraphNode* FNodeScribeBuildContext::BuildSubGraph(UEdGraph* SubGraph, const TArray<FNodeScribeStatement>& Statements, int32 First, int32 End)
{
	if (!SubGraph || First >= End)
	{
		return nullptr;
	}

	TArray<FNodeScribeStatement> Body;
	Body.Reserve(End - First);
	for (int32 Index = First; Index < End; ++Index)
	{
		Body.Add(Statements[Index]);
	}

	// A context of its own: the sub-graph has another schema, another Output
	// Pose and another layout tree. Sharing ours would make both graphs fight
	// over the same state -- and one's `$references` would leak into the other,
	// where the pins do not even exist.
	FNodeScribeBuildContext Nested(SubGraph, Blueprint, FVector2D::ZeroVector);
	Nested.Run(Body);

	Result.Diagnostics.Append(Nested.Result.Diagnostics);
	Result.ErrorCount += Nested.Result.ErrorCount;
	Result.WarningCount += Nested.Result.WarningCount;
	NestedNodes.Append(Nested.Result.CreatedNodes);

	// Frames[0] is the block's root level, and its LastNode is the node of the
	// last line -- even when that line is a pure node, which does not enter the
	// flow chain and so does not show up in PendingExec.
	return Nested.Frames.Num() > 0 ? Nested.Frames[0].LastNode : nullptr;
}

int32 FNodeScribeBuildContext::BuildStateMachine(UAnimGraphNode_StateMachineBase* Machine, const TArray<FNodeScribeStatement>& Statements, int32 First)
{
	const int32 BlockIndent = Statements[First].Indent;

	int32 End = First;
	while (End < Statements.Num() && Statements[End].Indent >= BlockIndent)
	{
		++End;
	}

	UAnimationStateMachineGraph* MachineGraph = Machine->EditorStateMachineGraph;
	if (!MachineGraph)
	{
		AddError(Statements[First].LineNumber,
			TEXT("The state machine went in without a sub-graph. States cannot be created inside it."));
		return End;
	}

	// --- 1st pass: the states ---------------------------------------------
	//
	// All of them before any transition. That way `Idle -> Running` can be
	// written before `state Running`, and a wrong name in a transition becomes
	// an error instead of an empty state created by mistake.
	TMap<FString, UAnimStateNodeBase*> States;
	TArray<FString> DeclaredOrder;

	for (int32 Index = First; Index < End; ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];
		if (!Statement.bIsLabel || Statement.Indent != BlockIndent)
		{
			continue;
		}

		FString Name;
		const bool bIsState = ParseStateLabel(Statement.Label, Name);
		const bool bIsConduit = !bIsState && ParseConduitLabel(Statement.Label, Name);
		const bool bIsAlias = !bIsState && !bIsConduit && ParseAliasLabel(Statement.Label, Name);

		if (!bIsState && !bIsConduit && !bIsAlias)
		{
			continue;
		}

		const FString Key = FNodeScribeCatalog::Normalize(Name);
		if (States.Contains(Key))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("State `%s` was already declared in this machine."), *Name));
			continue;
		}

		// State, conduit and alias are born in the same list: all three are
		// transition ends. What changes is what lives inside -- a pose in the
		// state, a rule in the conduit, a list of names in the alias --, and the
		// node's class decides that.
		UAnimStateNodeBase* State = nullptr;
		if (bIsAlias)
		{
			State = AllocateNode<UAnimStateAliasNode>(MachineGraph);
		}
		else if (bIsConduit)
		{
			State = AllocateNode<UAnimStateConduitNode>(MachineGraph);
		}
		else
		{
			State = AllocateNode<UAnimStateNode>(MachineGraph);
		}

		FinalizeNode(State);
		ApplyLabelSettings(State, Statement);

		// A state's name is its sub-graph's; an alias's is a field, because an
		// alias has no sub-graph. Through the virtual, not the field:
		// `BoundGraph` is declared in each subclass, not in the base.
		if (UEdGraph* Bound = State->GetBoundGraph())
		{
			FBlueprintEditorUtils::RenameGraph(Bound, Name);
		}
		else
		{
			State->OnRenameNode(Name);
		}

		State->NodePosX = States.Num() * StateColumnWidth;
		State->NodePosY = 0;

		States.Add(Key, State);
		DeclaredOrder.Add(Key);
		NestedNodes.Add(State);
	}

	if (States.Num() == 0)
	{
		AddWarning(Statements[First].LineNumber,
			TEXT("This state machine declared no states. Use `state Name:`."));
		return End;
	}

	// The first one declared is where the machine starts. It is the only
	// possible reading without inventing syntax: in the graph the Entry points
	// at a single state.
	if (MachineGraph->EntryNode)
	{
		if (UEdGraphPin* EntryPin = MachineGraph->EntryNode->GetOutputPin())
		{
			UAnimStateNodeBase* FirstState = States[DeclaredOrder[0]];
			if (UEdGraphPin* StatePin = FirstState->GetInputPin())
			{
				EntryPin->BreakAllPinLinks();
				EntryPin->MakeLinkTo(StatePin);
			}
		}
	}

	// --- 2nd pass: the contents of each state and each transition ---------
	for (int32 Index = First; Index < End; ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];
		if (!Statement.bIsLabel || Statement.Indent != BlockIndent)
		{
			continue;
		}

		int32 BodyEnd = Index + 1;
		while (BodyEnd < End && Statements[BodyEnd].Indent > BlockIndent)
		{
			++BodyEnd;
		}

		FString Name;
		const bool bIsState = ParseStateLabel(Statement.Label, Name);
		const bool bIsConduit = !bIsState && ParseConduitLabel(Statement.Label, Name);
		const bool bIsAlias = !bIsState && !bIsConduit && ParseAliasLabel(Statement.Label, Name);

		if (bIsAlias)
		{
			UAnimStateAliasNode* Alias =
				Cast<UAnimStateAliasNode>(States.FindRef(FNodeScribeCatalog::Normalize(Name)));

			if (!Alias)
			{
				continue;
			}

			for (int32 Body = Index + 1; Body < BodyEnd; ++Body)
			{
				const FNodeScribeStatement& Line = Statements[Body];
				const FString Wanted = Line.bIsLabel ? Line.Label : Line.NodeExpression.TrimStartAndEnd();

				UAnimStateNodeBase* Target = States.FindRef(FNodeScribeCatalog::Normalize(Wanted));

				// Only real states. The Engine sweeps the graph for
				// `UAnimStateNode` when rebuilding the alias's references, so a
				// conduit or another alias in the list vanishes on the next save --
				// no error, no warning, and the transition that left from there
				// stops existing.
				if (!Target || !Target->IsA<UAnimStateNode>())
				{
					AddError(Line.LineNumber, FString::Printf(
						TEXT("`%s` is not a state of this machine, so it does not go into alias `%s`. ")
						TEXT("An alias only points at a `state`, not at a conduit nor at another alias."),
						*Wanted, *Name));
					continue;
				}

				Alias->GetAliasedStates().Add(Target);
			}

			continue;
		}

		if (bIsState || bIsConduit)
		{
			UAnimStateNodeBase* State = States.FindRef(FNodeScribeCatalog::Normalize(Name));
			UEdGraph* Bound = State ? State->GetBoundGraph() : nullptr;
			if (!Bound)
			{
				continue;
			}

			UEdGraphNode* Last = BuildSubGraph(Bound, Statements, Index + 1, BodyEnd);

			// In a state the block is a pose, and it links into the Output Pose
			// inside its own sub-graph. In a conduit the block is a rule: it ends
			// in a bool that needs to reach Can Enter Transition, just like a
			// transition's. Without this the conduit compiles and never lets
			// anything through.
			if (bIsConduit)
			{
				WireRule(Bound, Last, Statement, BodyEnd > Index + 1, false);
			}

			continue;
		}

		FString From, To;
		if (!ParseTransitionLabel(Statement.Label, From, To))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s:` is not a state, conduit, alias nor transition. Inside a state machine ")
				TEXT("there are only `state Name:`, `conduit Name:`, `alias Name:` and ")
				TEXT("`From -> To:`."), *Statement.Label));
			continue;
		}

		UAnimStateNodeBase* FromState = States.FindRef(FNodeScribeCatalog::Normalize(From));
		UAnimStateNodeBase* ToState = States.FindRef(FNodeScribeCatalog::Normalize(To));

		if (!FromState || !ToState)
		{
			TArray<FString> Known;
			for (const FString& Key : DeclaredOrder)
			{
				Known.Add(States[Key]->GetStateName());
			}

			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Transition `%s` mentions a state that does not exist (`%s`). States of this machine: %s"),
				*Statement.Label, *(FromState ? To : From), *FString::Join(Known, TEXT(", "))));
			continue;
		}

		if (FromState == ToState)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Transition `%s` leaves and arrives at the same state."), *Statement.Label));
			continue;
		}

		UAnimStateTransitionNode* Transition = AllocateNode<UAnimStateTransitionNode>(MachineGraph);
		FinalizeNode(Transition);
		ApplyLabelSettings(Transition, Statement);
		Transition->CreateConnections(FromState, ToState);

		// On top of the wire, which is where the editor draws it.
		Transition->NodePosX = (FromState->NodePosX + ToState->NodePosX) / 2;
		Transition->NodePosY = FromState->NodePosY - 120;

		NestedNodes.Add(Transition);

		UEdGraphNode* RuleResult = Transition->BoundGraph
			? BuildSubGraph(Transition->BoundGraph, Statements, Index + 1, BodyEnd)
			: nullptr;

		// With the automatic rule on, the transition fires when the source
		// state's animation is ending, and that is why it is born with no
		// condition at all. Without this exception, every automatic transition
		// read from Epic came back with a warning that it would never fire --
		// which was false, and taught people to ignore precisely the warning that
		// exists for the case where it is true.
		WireRule(Transition->BoundGraph, RuleResult, Statement,
			BodyEnd > Index + 1, Transition->bAutomaticRuleBasedOnSequencePlayerInState);
	}

	return End;
}

void FNodeScribeBuildContext::WireRule(UEdGraph* RuleGraph, UEdGraphNode* RuleResult,
	const FNodeScribeStatement& Statement, bool bHasBody, bool bRuleOptional)
{
	if (!RuleGraph)
	{
		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("`%s` was born without a sub-graph. The rule was not linked."), *Statement.Label));
		return;
	}

	// The rule is a data graph that ends in a bool. Linking the result is the
	// plugin's job, like any other link the text does not write -- and without
	// it the transition never fires.
	UAnimGraphNode_TransitionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : RuleGraph->Nodes)
	{
		if (UAnimGraphNode_TransitionResult* Found = Cast<UAnimGraphNode_TransitionResult>(Node))
		{
			ResultNode = Found;
			break;
		}
	}

	// These used to stay silent. A transition without a rule compiles, runs,
	// and never fires -- the empty pin looks like a deliberate `false`, and
	// nothing on screen tells "the text asked for no rule" apart from "the
	// plugin could not link it".
	if (!ResultNode)
	{
		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("`%s` was born without a result node. The rule was not linked."), *Statement.Label));
		return;
	}

	UEdGraphPin* CanEnter = ResultNode->FindPin(TEXT("bCanEnterTransition"));
	if (!CanEnter)
	{
		TArray<FString> Pins;
		for (const UEdGraphPin* Pin : ResultNode->Pins)
		{
			Pins.Add(Pin->PinName.ToString());
		}

		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("The result of `%s` has no `bCanEnterTransition` pin. It has: %s"),
			*Statement.Label,
			Pins.Num() > 0 ? *FString::Join(Pins, TEXT(", ")) : TEXT("none")));
		return;
	}

	if (CanEnter->LinkedTo.Num() > 0)
	{
		return;
	}

	if (!bHasBody)
	{
		// Empty block. With the automatic rule that is correct: the transition
		// fires at the end of the source state's animation, and a condition
		// written there would be ignored. Without it, it is a transition that
		// never fires -- and nothing on screen says so.
		if (!bRuleOptional)
		{
			AddWarning(Statement.LineNumber, FString::Printf(
				TEXT("`%s` has no rule. It compiles and never fires -- if the intent was ")
				TEXT("to fire at the end of the animation, write ")
				TEXT("`(Automatic Rule Based on Sequence Player in State = true)`."),
				*Statement.Label));
		}
		return;
	}

	// The block's last node is the rule's result, the same way the last node of
	// a pose branch is the branch's result.
	//
	// What says which node that is, is the walk of the block, not the order in
	// which the nodes landed in the graph: a node's argument is born *after* it,
	// so sweeping `Nodes` backwards picks the rule's `$Ground Speed` instead of
	// the comparison that consumes it -- and then the transition ends up without
	// a rule, with a wrong-type warning in its place.
	UEdGraphPin* Output = RuleResult ? FindPrimaryOutput(RuleResult) : nullptr;
	if (!Output)
	{
		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("The rule of `%s` did not end in a value. It never fires."), *Statement.Label));
		return;
	}

	// Both pins live in the rule's graph, not the machine's. Linking through the
	// outer schema produces a wire the inner graph does not recognise: it shows
	// up, and vanishes on the Blueprint's first refresh -- which comes right
	// away, in MarkBlueprintAsStructurallyModified. The one validating the link
	// has to be the schema of the graph where the pins live.
	if (const UEdGraphSchema* RuleSchema = RuleGraph->GetSchema())
	{
		RuleSchema->TryCreateConnection(Output, CanEnter);
	}

	// Check instead of trusting.
	if (CanEnter->LinkedTo.Num() == 0)
	{
		AddWarning(Statement.LineNumber, FString::Printf(
			TEXT("The rule of `%s` (`%s`) did not reach Can Enter Transition. It never fires."),
			*Statement.Label,
			*RuleResult->GetNodeTitle(ENodeTitleType::ListView).ToString()));
	}
}

// ---------------------------------------------------------------------------
// Walk
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Insertion into an existing chain
// ---------------------------------------------------------------------------

namespace
{
	bool AnchorArgMatches(const FNodeScribeArg& Wanted, const FNodeScribeArg& Have)
	{
		if (Wanted.bIsReference != Have.bIsReference)
		{
			return false;
		}

		// A reference is a name, compared as every name is; a literal is a value,
		// and a value the reading wrote is copied exactly.
		return Wanted.bIsReference
			? FNodeScribeCatalog::Normalize(Wanted.Value) == FNodeScribeCatalog::Normalize(Have.Value)
			: Wanted.Value.Equals(Have.Value, ESearchCase::CaseSensitive);
	}

	/**
	 * Whether a line of the reading is the one an `after` names.
	 *
	 * The `after` may say less than the whole line: each argument it writes has
	 * to be there with the same value, and the ones it leaves out are not
	 * compared. Saying less only widens the match -- two nodes left are refused,
	 * never chosen between.
	 */
	bool AnchorLineMatches(const FNodeScribeStatement& Wanted, const FNodeScribeStatement& Have)
	{
		if (FNodeScribeCatalog::Normalize(Wanted.NodeExpression) != FNodeScribeCatalog::Normalize(Have.NodeExpression))
		{
			return false;
		}

		if (!Wanted.OutputName.IsEmpty()
			&& FNodeScribeCatalog::Normalize(Wanted.OutputName) != FNodeScribeCatalog::Normalize(Have.OutputName))
		{
			return false;
		}

		for (int32 Index = 0; Index < Wanted.Args.Num(); ++Index)
		{
			const FNodeScribeArg& Arg = Wanted.Args[Index];
			const FNodeScribeArg* Found = nullptr;

			if (Arg.PinName.IsEmpty())
			{
				// Positional: where the reading itself wrote it.
				Found = Have.Args.IsValidIndex(Index) ? &Have.Args[Index] : nullptr;
			}
			else
			{
				const FString WantedPin = FNodeScribeCatalog::Normalize(Arg.PinName);
				Found = Have.Args.FindByPredicate([&WantedPin](const FNodeScribeArg& Candidate)
				{
					return FNodeScribeCatalog::Normalize(Candidate.PinName) == WantedPin;
				});
			}

			if (!Found || !AnchorArgMatches(Arg, *Found))
			{
				return false;
			}
		}

		return true;
	}
}

bool FNodeScribeBuildContext::ResolveAnchors(const TArray<FNodeScribeStatement>& Statements)
{
	const FNodeScribeStatement* FirstAnchor = Statements.FindByPredicate([](const FNodeScribeStatement& Statement)
	{
		return Statement.bIsAnchor;
	});

	if (!FirstAnchor)
	{
		return true;
	}

	if (bAnimGraph)
	{
		AddError(FirstAnchor->LineNumber,
			TEXT("`after` inserts into an execution chain, and an AnimGraph has none. Nothing was written."));
		return false;
	}

	// The graph as it is now, read the way the assistant saw it: the line in an
	// `after` was copied from this same reading, and the `$names` in the block
	// are this reading's names.
	const FNodeScribeReader::FResult Reading = FNodeScribeReader::ReadGraph(Graph, Blueprint);
	ExistingNames = Reading.NodeNames;

	struct FAnchorCandidate
	{
		UEdGraphNode* Node = nullptr;
		FString Line;
		FNodeScribeStatement Parsed;
	};

	TArray<FAnchorCandidate> Candidates;
	for (const TPair<UEdGraphNode*, FString>& Entry : Reading.ExecLines)
	{
		TArray<FNodeScribeDiagnostic> Ignored;
		TArray<FNodeScribeStatement> Parsed = FNodeScribeParser::Parse(Entry.Value, Ignored);

		if (Parsed.Num() == 1 && !Parsed[0].bIsLabel && !Parsed[0].bIsVariable)
		{
			Candidates.Add({ Entry.Key, FNodeScribeParser::StripComment(Entry.Value).TrimEnd(), MoveTemp(Parsed[0]) });
		}
	}

	bool bAllFound = true;

	for (int32 Index = 0; Index < Statements.Num(); ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];
		if (!Statement.bIsAnchor)
		{
			continue;
		}

		const FString Written = Statement.RawLine;

		// The block under it is a chain of its own; inside another block, where
		// that chain would end and the outer one resume is anybody's guess.
		if (Statement.Indent > 0)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` is indented. `after` opens a block of its own: write it at the start of the line. Nothing was written."),
				*Written));
			bAllFound = false;
			continue;
		}

		TArray<const FAnchorCandidate*> Matches;
		TArray<FString> SameName;
		const FString WantedExpression = FNodeScribeCatalog::Normalize(Statement.NodeExpression);

		for (const FAnchorCandidate& Candidate : Candidates)
		{
			if (AnchorLineMatches(Statement, Candidate.Parsed))
			{
				Matches.Add(&Candidate);
			}
			else if (FNodeScribeCatalog::Normalize(Candidate.Parsed.NodeExpression) == WantedExpression)
			{
				SameName.Add(TEXT("`") + Candidate.Line + TEXT("`"));
			}
		}

		if (Matches.Num() == 0)
		{
			AddError(Statement.LineNumber, SameName.Num() > 0
				? FString::Printf(
					TEXT("`%s` matches no node of this graph. With that name there are: %s. Nothing was written."),
					*Written, *FString::Join(SameName, TEXT(", ")))
				: FString::Printf(
					TEXT("`%s` matches no execution node of this graph. Copy the line from read_graph. Nothing was written."),
					*Written));
			bAllFound = false;
			continue;
		}

		if (Matches.Num() > 1)
		{
			TArray<FString> Lines;
			for (const FAnchorCandidate* Match : Matches)
			{
				Lines.Add(TEXT("`") + Match->Line + TEXT("`"));
			}

			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` matches %d nodes: %s. Write the arguments that tell them apart. Nothing was written."),
				*Written, Matches.Num(), *FString::Join(Lines, TEXT(", "))));
			bAllFound = false;
			continue;
		}

		UEdGraphNode* Node = Matches[0]->Node;
		const TArray<UEdGraphPin*> Outputs = GetFlowOutputs(Node);

		if (Outputs.Num() == 0)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` has no execution output to insert after. Nothing was written."), *Written));
			bAllFound = false;
			continue;
		}

		// Several outputs and no label: which one the block continues is the same
		// guess the chain refuses everywhere else.
		if (Outputs.Num() > 1)
		{
			const FNodeScribeStatement* Next = nullptr;
			for (int32 After = Index + 1; After < Statements.Num(); ++After)
			{
				if (!Statements[After].bIsVariable)
				{
					Next = &Statements[After];
					break;
				}
			}

			if (Next && Next->Indent > Statement.Indent && !Next->bIsLabel)
			{
				TArray<FString> Names;
				for (const UEdGraphPin* Pin : Outputs)
				{
					Names.Add(Pin->PinName.ToString());
				}

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s` has several outputs (%s). Open a label under `after` to say which one. Nothing was written."),
					*Written, *FString::Join(Names, TEXT(", "))));
				bAllFound = false;
				continue;
			}
		}

		AnchorNodes.Add(Statement.LineNumber, Node);
	}

	return bAllFound;
}

void FNodeScribeBuildContext::CaptureDisplaced(FFrame& Frame, UEdGraphPin* Origin, int32 Line)
{
	Frame.OpenedAtLine = Line;

	// A pin of a node this write created has nothing linked yet: only a pin
	// of a node already in the graph displaces anything.
	if (!Origin || Origin->LinkedTo.Num() == 0)
	{
		return;
	}

	Frame.OriginPin = FPinRef(Origin);
	for (UEdGraphPin* Linked : Origin->LinkedTo)
	{
		Frame.Displaced.Add(FPinRef(Linked));
	}
}

void FNodeScribeBuildContext::PopFrame()
{
	const FFrame Closed = Frames.Pop();

	if (Closed.Displaced.Num() == 0)
	{
		return;
	}

	UEdGraphPin* OriginPin = Closed.OriginPin.Resolve();

	TArray<UEdGraphPin*> Targets;
	for (const FPinRef& Ref : Closed.Displaced)
	{
		if (UEdGraphPin* Pin = Ref.Resolve())
		{
			Targets.Add(Pin);
		}
	}

	// The old wire is still there: the block only had data nodes, or started a
	// chain of its own. Nothing was inserted, and nothing needs relinking.
	const bool bUntouched = OriginPin && Targets.ContainsByPredicate([OriginPin](UEdGraphPin* Target)
	{
		return OriginPin->LinkedTo.Contains(Target);
	});

	if (!OriginPin || bUntouched)
	{
		return;
	}

	UEdGraphPin* Tail = Closed.PendingExec.Resolve();
	if (Tail && Tail != OriginPin)
	{
		for (UEdGraphPin* Target : Targets)
		{
			Connect(Tail, Target, Closed.OpenedAtLine);
		}
		return;
	}

	// The block ends in a node with several outputs, or in none: which of them
	// the old continuation hangs from is a guess. It stays loose, and loudly.
	TArray<FString> Names;
	for (UEdGraphPin* Target : Targets)
	{
		if (UEdGraphNode* Node = Target->GetOwningNodeUnchecked())
		{
			FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			Title.ReplaceInline(TEXT("\n"), TEXT(" "));
			Names.Add(TEXT("`") + Title.TrimStartAndEnd() + TEXT("`"));
		}
	}

	AddError(Closed.OpenedAtLine, FString::Printf(
		TEXT("The inserted chain does not end in a single execution output, so %s, which came next, was left unlinked. ")
		TEXT("Link it by hand to where the chain should continue."),
		*FString::Join(Names, TEXT(", "))));
}

// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::Run(const TArray<FNodeScribeStatement>& Statements)
{
	FFrame Root;
	Root.Indent = -1;
	Root.BaseX = FMath::RoundToInt(Origin.X);
	Root.BaseY = FMath::RoundToInt(Origin.Y);
	Frames.Add(Root);

	// Before anything is created: the reading behind it has to be the graph as
	// the assistant saw it, and a refusal has to leave that graph untouched.
	if (!ResolveAnchors(Statements))
	{
		return;
	}

	PreCreateCustomEvents(Statements);

	// By index, not by range-for, because the state machine does not read its
	// block line by line: it takes the whole block and returns where it stopped.
	for (int32 Index = 0; Index < Statements.Num(); ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];

		// A declaration creates no node and takes no part in the chain: it is
		// just a variable coming into existence before the lines that use it.
		if (Statement.bIsVariable)
		{
			CreateDeclaredVariable(Statement);
			continue;
		}

		if (Statement.bIsAnchor)
		{
			// Always at the top level (ResolveAnchors refuses the rest), so it
			// closes every open block.
			while (Frames.Num() > 1)
			{
				PopFrame();
			}

			// What comes after the block at the top level starts a chain of its
			// own, not the one that was open before it.
			Frames.Top().PendingExec = FPinRef();
			Frames.Top().LastNode = nullptr;

			UEdGraphNode* Existing = AnchorNodes.FindRef(Statement.LineNumber);

			FFrame Insertion;
			Insertion.Indent = Statement.Indent;
			Insertion.LastNode = Existing;
			Insertion.OpenedAtLine = Statement.LineNumber;

			// Below the node, not to its right: there sits what used to follow it.
			Insertion.BaseX = Existing->NodePosX + ColumnWidth;
			Insertion.BaseY = Existing->NodePosY + BranchRowHeight;

			// Several outputs: the labels under it choose, as under a new Branch.
			const TArray<UEdGraphPin*> Outputs = GetFlowOutputs(Existing);
			if (Outputs.Num() == 1)
			{
				Insertion.PendingExec = FPinRef(Outputs[0]);
				CaptureDisplaced(Insertion, Outputs[0], Statement.LineNumber);
			}

			Frames.Add(Insertion);
			continue;
		}

		if (Statement.bIsLabel)
		{
			// A label closes any block at the same level or deeper, so that
			// `true:` and `false:` are siblings, not nested.
			while (Frames.Num() > 1 && Frames.Top().Indent >= Statement.Indent)
			{
				PopFrame();
			}

			FFrame& Parent = Frames.Top();
			UEdGraphNode* Owner = Parent.LastNode;

			if (!Owner)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Label `%s:` has no node before it."), *Statement.Label));
				continue;
			}

			// The previous node did not resolve and became a comment. Complaining
			// that the label does not match its outputs would send people looking
			// for the problem on the wrong line -- the cause was already reported
			// one line above.
			if (Owner->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}

			// Under a state machine a label names no pin: it names a state or a
			// transition, and its body lives in another graph. The whole block
			// leaves the walk here.
			if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Owner))
			{
				Index = BuildStateMachine(Machine, Statements, Index) - 1;
				continue;
			}

			// In the AnimGraph a label names a pose *input*: `True Pose:` of a
			// blend opens the branch that feeds that pin. An AnimGraph's tree is
			// about what goes in, not what comes next.
			const bool bLabelsAreInputs = bAnimGraph && GetExecOutputs(Owner).Num() == 0;

			const TArray<UEdGraphPin*> ExecOutputs = bLabelsAreInputs
				? NodeScribeAnimGraph::GetPoseInputs(Owner)
				: GetExecOutputs(Owner);

			const FString Wanted = ResolveLabelAlias(Statement.Label);

			UEdGraphPin* Chosen = nullptr;
			for (UEdGraphPin* Pin : ExecOutputs)
			{
				if (FNodeScribeCatalog::Normalize(Pin->PinName.ToString()) == Wanted)
				{
					Chosen = Pin;
					break;
				}

				if (!Pin->PinFriendlyName.IsEmpty()
					&& FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString()) == Wanted)
				{
					Chosen = Pin;
					break;
				}
			}

			if (!Chosen)
			{
				TArray<FString> Available;
				for (UEdGraphPin* Pin : ExecOutputs)
				{
					Available.Add(Pin->PinName.ToString());
				}

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s:` is not %s of this node. %s: %s"),
					*Statement.Label,
					bLabelsAreInputs ? TEXT("a pose input") : TEXT("an output"),
					bLabelsAreInputs ? TEXT("Inputs") : TEXT("Outputs"),
					*FString::Join(Available, TEXT(", "))));
				continue;
			}

			// This node got a label: it leaves the "stopped without choosing a
			// branch" list.
			UnbranchedNodes.RemoveAll([Owner](const FUnbranchedNode& Entry)
			{
				return Entry.Node == Owner;
			});

			FFrame Branch;
			Branch.Indent = Statement.Indent;

			if (bLabelsAreInputs)
			{
				// The block starts with nothing before it: the branch's first node
				// is a tip of the tree, not the blend's continuation.
				Branch.FlowSink = FPinRef(Chosen);
			}
			else
			{
				Branch.PendingExec = FPinRef(Chosen);

				// Under an `after`, the label may open a pin that already leads
				// somewhere: the block goes in between, as it does after the node.
				CaptureDisplaced(Branch, Chosen, Statement.LineNumber);
			}

			// A pose branch draws to the left: there the flow walks towards the
			// Output Pose, and what feeds a node comes before it.
			Branch.BaseX = bLabelsAreInputs
				? Owner->NodePosX - ColumnWidth
				: Owner->NodePosX + ColumnWidth;
			Branch.BaseY = Parent.BaseY + (Parent.BranchesOpened * BranchRowHeight);

			// The previous branch may have grown past one row: sub-branches, or
			// a data stack taller than the row.
			if (Parent.BranchesOpened > 0 && !bLabelsAreInputs)
			{
				Branch.BaseY = FMath::Max(Branch.BaseY, NextFreeRowY());
			}

			++Parent.BranchesOpened;

			Frames.Add(Branch);
			continue;
		}

		while (Frames.Num() > 1 && Statement.Indent <= Frames.Top().Indent)
		{
			PopFrame();
		}

		// What is in the graph before the line: everything new after its
		// arguments is the line's, the Gets and conversions it made by itself too.
		TSet<UEdGraphNode*> NodesBeforeLine;
		NodesBeforeLine.Reserve(Graph->Nodes.Num());
		for (UEdGraphNode* Existing : Graph->Nodes)
		{
			NodesBeforeLine.Add(Existing);
		}

		UEdGraphNode* Node = CreateNodeForStatement(Statement);
		if (!Node)
		{
			continue;
		}

		FFrame& Frame = Frames.Top();

		// An adopted node was already in the graph -- the Output Pose is the
		// case. Moving it is touching what the user arranged, and counting it
		// would be saying we created something that was always there.
		const bool bAdopted = AdoptedNodes.Contains(Node);

		// Data nodes take no column: they stack below whoever consumes them, and
		// LayoutDataNodes() takes care of that, at the end.
		if (!bAdopted && !IsPureDataNode(Node))
		{
			OpenRowIfNeeded(Frame, Node);

			Node->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Node->NodePosY = Frame.BaseY;
			++Frame.Column;
		}

		if (!bAdopted && !ReusedEvents.Contains(Node))
		{
			Result.CreatedNodes.Add(Node);
		}

		// A comment takes the place of a line that did not resolve; applying its
		// arguments would produce a second batch of errors about the same problem.
		if (!Node->IsA<UEdGraphNode_Comment>())
		{
			ApplyArguments(Node, Statement);
			ApplyDeferredElements(false);
		}

		// The node itself even when it existed before: a pre-created event, a
		// reused stub.
		Result.NodeLines.Add(Node, Statement.LineNumber);
		for (UEdGraphNode* Added : Graph->Nodes)
		{
			if (Added && !NodesBeforeLine.Contains(Added) && !Result.NodeLines.Contains(Added))
			{
				Result.NodeLines.Add(Added, Statement.LineNumber);
			}
		}

		if (Statement.OutputName.IsEmpty() && !Node->IsA<UEdGraphNode_Comment>())
		{
			UnnamedLines.Add({ Statement.LineNumber, Statement.NodeExpression });
		}

		if (!Statement.OutputName.IsEmpty())
		{
			if (Node->IsA<UEdGraphNode_Comment>())
			{
				// The line did not become a node. Keeping the name avoids two
				// messages that help nobody: "has no data output" here, and
				// "`$name` does not exist" on every later line that used it.
				FailedOutputs.Add(Statement.OutputName, Statement.LineNumber);
			}
			else
			{
				RegisterOutput(Statement.OutputName, Node, Statement.LineNumber);
			}
		}

		UEdGraphPin* ExecIn = FindFlowInput(Node);
		const TArray<UEdGraphPin*> ExecOutputs = GetFlowOutputs(Node);

		if (ExecIn)
		{
			if (UEdGraphPin* PreviousExec = Frame.PendingExec.Resolve())
			{
				Connect(PreviousExec, ExecIn, Statement.LineNumber);
			}
		}
		else if (ExecOutputs.Num() == 0)
		{
			// Pure node: it is just a value, not a step. It is already queued for
			// positioning and takes no part in the execution chain.
			Frame.LastNode = Node;
			continue;
		}
		// No input but an output = event. It starts a new chain instead of
		// continuing the previous one, so there is nothing to link before it.

		// The block feeds a pose input up there. Each node links over the
		// previous one; the pin only accepts one wire, so the last one remains --
		// which is the block's result.
		if (ExecOutputs.Num() == 1)
		{
			if (UEdGraphPin* Sink = Frame.FlowSink.Resolve())
			{
				// Break before linking, instead of counting on the pin refusing
				// the second wire: if the pose output accepts several targets, the
				// previous node would keep feeding the blend *and* the next node.
				Sink->BreakAllPinLinks();
				Connect(ExecOutputs[0], Sink, Statement.LineNumber);
			}
		}

		if (ExecOutputs.Num() == 1)
		{
			Frame.PendingExec = FPinRef(ExecOutputs[0]);
		}
		else if (ExecOutputs.Num() > 1)
		{
			// Several outputs: choosing one would be guessing which path the user
			// wants to follow. The chain stops here until they open a label.
			Frame.PendingExec = FPinRef();

			TArray<FString> Names;
			for (UEdGraphPin* Pin : ExecOutputs)
			{
				Names.Add(Pin->PinName.ToString());
			}

			UnbranchedNodes.Add({ Node, Statement.LineNumber, FString::Join(Names, TEXT(", ")) });
		}
		else
		{
			Frame.PendingExec = FPinRef();
		}

		Frame.LastNode = Node;
	}

	// The blocks still open at the end of the text close here, and an insertion
	// among them relinks what it displaced.
	while (Frames.Num() > 1)
	{
		PopFrame();
	}

	ApplyDeferredElements(true);

	// Only now is it possible to know which ones really stayed without a label.
	for (const FUnbranchedNode& Entry : UnbranchedNodes)
	{
		AddInfo(Entry.Line, FString::Printf(
			TEXT("This node has several outputs (%s) and no indented label. The chain stopped here."),
			*Entry.Outputs));
	}

	// A Get the plugin created on its own and that linked to nothing stands for
	// no line of the text: it is the leftover of a link that failed. The failure
	// warning already came out; keeping the node would only clutter the graph.
	for (UEdGraphNode* GetNode : AutoCreatedGets)
	{
		bool bConnected = false;
		for (const UEdGraphPin* Pin : GetNode->Pins)
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				bConnected = true;
				break;
			}
		}

		if (!bConnected)
		{
			Result.CreatedNodes.Remove(GetNode);
			Graph->RemoveNode(GetNode);
		}
	}

	ConnectOutputPose();
	ConnectFunctionEntry();

	LayoutAnimNodes();
	LayoutDataNodes();
	ReportEmptyPoseInputs();

	// After the layout: they live in another graph and there is nothing here to
	// position in them. They go in only for the count and so Ctrl+Z catches all.
	Result.CreatedNodes.Append(NestedNodes);
}

// ---------------------------------------------------------------------------

FNodeScribeBuilder::FResult FNodeScribeBuilder::Build(
	const TArray<FNodeScribeStatement>& Statements,
	UEdGraph* Graph,
	UBlueprint* Blueprint,
	const FVector2D& Origin)
{
	FNodeScribeBuildContext Context(Graph, Blueprint, Origin);
	Context.Run(Statements);
	return MoveTemp(Context.Result);
}

bool NodeScribeTypeNames::ResolvePinTypeFromName(const FString& InTypeName, FEdGraphPinType& OutType)
{
	return ResolvePinTypeFromNameInternal(InTypeName, OutType);
}

UClass* NodeScribeTypeNames::FindClassByFriendlyName(const FString& Name)
{
	return FindClassByFriendlyNameInternal(Name);
}
