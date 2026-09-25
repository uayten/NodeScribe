#include "NodeScribeObjectWriter.h"

#include "NodeScribeAIWriter.h"
#include "NodeScribeBuilder.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeObjectTarget.h"
#include "NodeScribeParser.h"
#include "NodeScribePropertyText.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

using namespace NodeScribeObjectTarget;

namespace
{
	using namespace NodeScribePropertyText;

	/**
	 * Finds the property by the name the sheet writes.
	 *
	 * The comparison is the same as for pins: `max walk speed`, `MaxWalkSpeed`
	 * and `Max_Walk_Speed` are the same. Whoever writes the sheet back is
	 * copying what they read, and what they read was the display name.
	 */
	FProperty* FindProperty(UStruct* Owner, const FString& Name, TArray<FString>& OutNear)
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		for (TFieldIterator<FProperty> It(Owner, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!IsVisible(Property))
			{
				continue;
			}

			if (FNodeScribeCatalog::Normalize(DisplayName(Property)) == Wanted
				|| FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted)
			{
				return Property;
			}
		}

		// Without an exact hit, gather the similar ones: the common mistake is an
		// almost-right name, and a short list solves it faster than "not found".
		for (TFieldIterator<FProperty> It(Owner, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsVisible(Property))
			{
				continue;
			}
			const FString Candidate = FNodeScribeCatalog::Normalize(DisplayName(Property));
			if (Candidate.Contains(Wanted) || Wanted.Contains(Candidate))
			{
				OutNear.Add(DisplayName(Property));
			}
		}

		return nullptr;
	}

	/**
	 * The Blueprint variable a sheet line talks about, or NAME_None.
	 *
	 * The sheet writes the variable by its display name, and a boolean called
	 * `bIsAlive` shows up as `Is Alive`. Comparing only the internal name made a
	 * sheet pasted back create a second variable, `Is Alive`, next to the one
	 * it had just read -- so the friendly name counts too.
	 */
	FName FindOwnVariable(const UBlueprint* Blueprint, const FString& Name)
	{
		if (!Blueprint)
		{
			return NAME_None;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (FNodeScribeCatalog::Normalize(Variable.VarName.ToString()) == Wanted
				|| FNodeScribeCatalog::Normalize(Variable.FriendlyName) == Wanted)
			{
				return Variable.VarName;
			}
		}

		return NAME_None;
	}

	/** How many spaces open the line. Two per level, as in the rest of the format. */
	int32 MeasureIndent(const FString& Line)
	{
		int32 Spaces = 0;
		while (Spaces < Line.Len() && Line[Spaces] == TEXT(' '))
		{
			++Spaces;
		}
		return Spaces / 2;
	}

	bool IsResetToDefault(const FString& Value)
	{
		return Value.Equals(TEXT("default"), ESearchCase::IgnoreCase);
	}
}

FNodeScribeObjectWriter::FResult FNodeScribeObjectWriter::WriteObject(
	UObject* Object, const FString& Text)
{
	FResult Result;

	if (!Object)
	{
		Result.Diagnostics.Add(TEXT("[error]: no object given."));
		return Result;
	}

	// AI assets have a shape of their own both ways, same as the reader.
	if (FNodeScribeAIWriter::Handles(Object))
	{
		const FNodeScribeAIWriter::FResult AIResult =
			FNodeScribeAIWriter::WriteAsset(Object, Text);

		Result.Applied = AIResult.Applied;
		Result.Diagnostics = AIResult.Diagnostics;
		return Result;
	}

	UObject* Root = ResolveTarget(Object);
	if (!Root)
	{
		Result.Diagnostics.Add(FString::Printf(
			TEXT("[error]: `%s` has no compiled class -- compile the Blueprint first."),
			*Object->GetName()));
		return Result;
	}

	UBlueprint* Blueprint = FindBlueprint(Object, Root->GetClass());
	const TMap<FString, UObject*> Components = CollectComponents(Root, Blueprint);

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	const FScopedTransaction Transaction(
		LOCTEXT("WriteObjectTransaction", "NodeScribe: write sheet"));

	// The target of each indentation level. [0] is the object; a component block
	// replaces [0]; a struct block stacks on [1].
	UObject* CurrentObject = Root;
	FString CurrentObjectLabel;

	// Open struct block, when there is one.
	FStructProperty* OpenStruct = nullptr;
	void* OpenStructValue = nullptr;
	int32 OpenStructIndent = -1;

	TSet<UObject*> Touched;
	bool bTouchedBlueprint = false;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const int32 Indent = MeasureIndent(Lines[Index]);
		FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();

		if (Line.IsEmpty())
		{
			continue;
		}

		// The sheet's header and its `~ N properties at default` footer: reader
		// output, not instructions. Without skipping the footer, pasting a whole
		// sheet back -- which should change nothing -- ended in an error on its
		// last line.
		if (Line.StartsWith(TEXT("sheet ")) || Line.StartsWith(TEXT("~")) || Line.StartsWith(TEXT("//")))
		{
			continue;
		}

		if (OpenStruct && Indent <= OpenStructIndent)
		{
			OpenStruct = nullptr;
			OpenStructValue = nullptr;
		}

		// Zero indentation closes the component block. Without this the block
		// never ended: a line of the actor itself, written after a component,
		// kept being applied to the component. If it had a property with that
		// name, it wrote to the wrong place without saying anything -- and the
		// mistake would only show up at runtime.
		if (Indent == 0)
		{
			CurrentObject = Root;
			CurrentObjectLabel.Empty();
			OpenStruct = nullptr;
			OpenStructValue = nullptr;
		}

		// `delete variable Name`. It needs a word of its own: this writer deletes
		// nothing on principle, and deleting a variable breaks every node that
		// used it. It cannot happen through a formatting slip.
		if (Line.StartsWith(TEXT("delete variable "), ESearchCase::IgnoreCase)
			|| Line.StartsWith(TEXT("remove variable "), ESearchCase::IgnoreCase))
		{
			const FString Written = Line.RightChop(Line.Find(TEXT("variable "),
				ESearchCase::IgnoreCase) + 9).TrimStartAndEnd();

			if (!Blueprint)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: `%s` is not a Blueprint; there is no variable to delete."),
					LineNumber, *Object->GetName()));
				continue;
			}

			const FName VarName = FindOwnVariable(Blueprint, Written);
			if (VarName.IsNone())
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: `%s` is not a variable of this Blueprint."),
					LineNumber, *Written));
				continue;
			}

			Blueprint->Modify();
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);
			bTouchedBlueprint = true;
			++Result.Applied;
			continue;
		}

		// `variable Name : Type [editable] [= value]`.
		//
		// Creates it when it does not exist, and then the type is mandatory. When
		// it already exists, the type is ignored -- changing the type of a used
		// variable breaks the nodes that consume it, and that calls for a
		// decision, not a side effect.
		if (Line.StartsWith(TEXT("variable "), ESearchCase::IgnoreCase))
		{
			Line = Line.RightChop(9).TrimStart();

			int32 Equals = INDEX_NONE;
			const bool bHasValue = Line.FindChar(TEXT('='), Equals);

			FString Declaration = bHasValue ? Line.Left(Equals).TrimEnd() : Line;
			const FString Value = bHasValue ? Line.RightChop(Equals + 1).TrimStart() : FString();

			// `editable` closes the declaration, like `synced` on the blackboard.
			bool bInstanceEditable = false;
			for (const TCHAR* Word : { TEXT(" instance editable"), TEXT(" editable") })
			{
				if (Declaration.EndsWith(Word, ESearchCase::IgnoreCase))
				{
					Declaration = Declaration.LeftChop(FCString::Strlen(Word)).TrimEnd();
					bInstanceEditable = true;
					break;
				}
			}

			int32 Colon = INDEX_NONE;
			const bool bHasType = Declaration.FindChar(TEXT(':'), Colon);

			const FString WrittenName = (bHasType ? Declaration.Left(Colon) : Declaration).TrimEnd();
			const FString TypeName = bHasType ? Declaration.RightChop(Colon + 1).TrimStart() : FString();

			const FName ExistingName = FindOwnVariable(Blueprint, WrittenName);

			if (ExistingName.IsNone())
			{
				if (!Blueprint)
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("line %d [error]: `%s` does not exist, and `%s` is not a Blueprint to create it in."),
						LineNumber, *WrittenName, *Object->GetName()));
					continue;
				}

				if (!bHasType)
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("line %d [error]: `%s` does not exist. To create it, state the type: `variable %s : Float`."),
						LineNumber, *WrittenName, *WrittenName));
					continue;
				}

				FEdGraphPinType PinType;
				if (!NodeScribeTypeNames::ResolvePinTypeFromName(TypeName, PinType))
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("line %d [error]: did not recognise type `%s`. Use the name shown in the ")
						TEXT("interface, such as Float, Name, Timer Handle, or `Array of X`."),
						LineNumber, *TypeName));
					continue;
				}

				Blueprint->Modify();
				if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*WrittenName), PinType, Value))
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("line %d [error]: could not create `%s`."), LineNumber, *WrittenName));
					continue;
				}

				bTouchedBlueprint = true;
				++Result.Applied;

				// Created together with its value: there is nothing to apply
				// afterwards. And the CDO only gets the property on the next
				// compile, so trying to write into it now would find nothing.
				if (bInstanceEditable)
				{
					FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(
						Blueprint, FName(*WrittenName), /*bNewBlueprintOnly*/ false);
				}
				continue;
			}

			if (bInstanceEditable && Blueprint)
			{
				Blueprint->Modify();
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(
					Blueprint, ExistingName, /*bNewBlueprintOnly*/ false);
				bTouchedBlueprint = true;
			}

			if (!bHasValue)
			{
				// Only a declaration of a variable that already exists: nothing to do.
				continue;
			}

			// It exists: falls into the regular path of writing a property value.
			Line = WrittenName + TEXT(" = ") + Value;
		}

		int32 Equals = INDEX_NONE;
		const bool bHasEquals = Line.FindChar(TEXT('='), Equals);

		// Without `=` and ending in `:` it opens a block.
		if (!bHasEquals)
		{
			FString Label = Line;
			Label.RemoveFromEnd(TEXT(":"));

			// `Comp : Type` -- the type is informative, the name is what matters.
			int32 Colon = INDEX_NONE;
			if (Label.FindChar(TEXT(':'), Colon))
			{
				Label = Label.Left(Colon);
			}
			Label.TrimStartAndEndInline();

			if (UObject* const* Component = Components.Find(Label))
			{
				CurrentObject = *Component;
				CurrentObjectLabel = Label;
				OpenStruct = nullptr;
				continue;
			}

			// Not a component: it may be a struct block of the current object.
			TArray<FString> Near;
			FProperty* Property = FindProperty(CurrentObject->GetClass(), Label, Near);
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				OpenStruct = StructProperty;
				OpenStructValue = StructProperty->ContainerPtrToValuePtr<void>(CurrentObject);
				OpenStructIndent = Indent;
				continue;
			}

			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: `%s` is neither a component nor a struct of this object.%s"),
				LineNumber, *Label,
				Near.Num() > 0
					? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", ")))
					: TEXT("")));
			continue;
		}

		const FString Name = Line.Left(Equals).TrimEnd();
		const FString Value = Line.RightChop(Equals + 1).TrimStart();

		// Inside a struct block, the property's owner is the struct.
		UStruct* Owner = OpenStruct ? static_cast<UStruct*>(OpenStruct->Struct)
			: static_cast<UStruct*>(CurrentObject->GetClass());
		void* Container = OpenStruct ? OpenStructValue : static_cast<void*>(CurrentObject);

		TArray<FString> Near;
		FProperty* Property = FindProperty(Owner, Name, Near);
		if (!Property)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: `%s` does not exist in %s.%s"),
				LineNumber, *Name,
				CurrentObjectLabel.IsEmpty() ? *Root->GetClass()->GetName() : *CurrentObjectLabel,
				Near.Num() > 0
					? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", ")))
					: TEXT("")));
			continue;
		}

		// A property the Engine retired.
		//
		// It stays EditAnywhere so old assets can load, so the name lookup finds
		// it and the write "works" -- and nothing reads what was saved. It is
		// exactly the failure mode this plugin exists to avoid: it saves into the
		// asset, nobody complains, and it only shows up at runtime. `SkeletalMesh`
		// on a mesh component is the classic case: it became `SkinnedAsset` in 5.1
		// and is still there, accepting values.
		//
		// Clearing one of these is still allowed. The danger is writing a value
		// into it, not removing what was left behind.
		const bool bDeprecated = Property->HasAnyPropertyFlags(CPF_Deprecated)
			|| Property->HasMetaData(TEXT("DeprecatedProperty"));

		if (bDeprecated && !IsResetToDefault(Value))
		{
			const FString Says = Property->GetMetaData(TEXT("DeprecationMessage"));

			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: `%s` is deprecated -- nothing reads what gets saved into it, ")
				TEXT("and the write would pass silently.%s"),
				LineNumber, *Name,
				Says.IsEmpty()
					? TEXT(" Look for the property that replaced it.")
					: *FString::Printf(TEXT(" The Engine says: %s"), *Says)));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

		CurrentObject->Modify();
		Touched.Add(CurrentObject);

		if (IsResetToDefault(Value))
		{
			UObject* Archetype = CurrentObject->GetArchetype();
			const FProperty* DefaultProperty = Archetype
				? Archetype->GetClass()->FindPropertyByName(Property->GetFName()) : nullptr;

			if (!OpenStruct && DefaultProperty && DefaultProperty->SameType(Property))
			{
				Property->CopyCompleteValue(ValuePtr,
					DefaultProperty->ContainerPtrToValuePtr<void>(Archetype));
			}
			else
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: could not find a factory value for `%s`."),
					LineNumber, *Name));
				continue;
			}
		}
		else
		{
			FString Error;
			if (!TextToValue(Property, ValuePtr, Value, Error))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: %s"), LineNumber, *Error));
				continue;
			}
		}

		FPropertyChangedEvent Changed(Property);
		CurrentObject->PostEditChangeProperty(Changed);

		++Result.Applied;
	}

	if (Result.Applied > 0 && Blueprint)
	{
		// Touching the variable list changes the class, not just a value:
		// without recompiling, the CDO keeps the old shape and the details panel
		// shows what no longer exists.
		if (bTouchedBlueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

			// And compile. `AddMemberVariable` touches the Blueprint's list, but
			// the property only exists in the class after this -- before, the
			// freshly created variable shows up neither in the sheet nor in the
			// details panel, and the caller would have to ask for a click on
			// Compile. Creating something that cannot be seen is worse than not
			// creating it.
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
		else
		{
			// Without this the asset keeps the change in memory and clean on disk:
			// the editor closes and the work vanishes without anyone warning.
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
