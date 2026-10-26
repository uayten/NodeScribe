#include "NodeScribeWidgetTree.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeObjectReader.h"
#include "NodeScribeObjectWriter.h"
#include "NodeScribeParser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace NodeScribeWidgetTree
{
	const TCHAR* const SectionKeyword = TEXT("designer");
}

namespace
{
	/** `WBP_VectorInspector_C` -> `WBP_VectorInspector`, as the Palette shows it. */
	FString WidgetClassName(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}

	/**
	 * What the widget line already says, or what is not the widget's look.
	 *
	 * `Slot` is the parent's object, with a block of its own (`slot ...`);
	 * written as a property it is a path to a subobject that pastes back as
	 * the wrong one. `Navigation` is an instanced object too, with no text form.
	 */
	const TSet<FName>& WidgetSkip()
	{
		static const TSet<FName> Names = { FName(TEXT("Slot")), FName(TEXT("Navigation")) };
		return Names;
	}

	/** A slot's own back-pointers. Not configuration, and they move with the widget. */
	const TSet<FName>& SlotSkip()
	{
		static const TSet<FName> Names = { FName(TEXT("Parent")), FName(TEXT("Content")) };
		return Names;
	}

	/** Indentation levels: a tab or two spaces each, as the parser counts them. */
	int32 WidgetIndent(const FString& Line)
	{
		int32 Spaces = 0;
		int32 Levels = 0;
		for (const TCHAR C : Line)
		{
			if (C == TEXT('\t'))
			{
				++Levels;
			}
			else if (C == TEXT(' '))
			{
				++Spaces;
			}
			else
			{
				break;
			}
		}
		return Levels + Spaces / 2;
	}

	bool IsSectionLine(const FString& Trimmed)
	{
		return Trimmed.Equals(NodeScribeWidgetTree::SectionKeyword, ESearchCase::IgnoreCase);
	}

	/** `IsStillAnswer : TextBlock variable`. */
	FString DescribeWidget(const UWidget* Widget)
	{
		FString Line = FString::Printf(TEXT("%s : %s"),
			*Widget->GetName(), *WidgetClassName(Widget->GetClass()));
		if (Widget->bIsVariable)
		{
			Line += TEXT(" variable");
		}
		return Line;
	}

	TArray<UWidget*> ChildrenOf(const UWidget* Widget)
	{
		TArray<UWidget*> Children;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Child = Panel->GetChildAt(Index))
				{
					Children.Add(Child);
				}
			}
		}
		return Children;
	}

	struct FReadNotes
	{
		TArray<FString> TooLong;
		TArray<FString> Unreadable;
	};

	const TCHAR* const NamedSlotKeyword = TEXT("named slot ");

	/**
	 * One widget and what hangs from it.
	 *
	 * @return false when, filtering, neither it nor anything under it matched --
	 *         the caller then leaves it out, ancestors included.
	 */
	bool ReadWidget(const UWidgetBlueprint* Blueprint, const UWidget* Widget, int32 Depth,
		const FString& NormalizedFilter, TArray<FString>& OutLines, FReadNotes& Notes)
	{
		const bool bFiltering = !NormalizedFilter.IsEmpty();
		const FString Name = Widget->GetName();

		const bool bMatches = !bFiltering
			|| FNodeScribeCatalog::Normalize(Name).Contains(NormalizedFilter)
			|| FNodeScribeCatalog::Normalize(WidgetClassName(Widget->GetClass())).Contains(NormalizedFilter);

		TArray<FString> ChildLines;
		bool bAnyChild = false;
		for (const UWidget* Child : ChildrenOf(Widget))
		{
			bAnyChild |= ReadWidget(Blueprint, Child, Depth + 1, NormalizedFilter, ChildLines, Notes);
		}

		// Content placed in a user widget's named slot lives in that instance,
		// not in the panel's children: `named slot Name`, the content under it.
		TArray<FString> NamedSlotLines;
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			TArray<FName> SlotNames;
			UserWidget->GetSlotNames(SlotNames);
			for (const FName& SlotName : SlotNames)
			{
				const UWidget* Content = UserWidget->GetContentForSlot(SlotName);
				TArray<FString> ContentLines;
				if (Content && ReadWidget(Blueprint, Content, Depth + 2, NormalizedFilter, ContentLines, Notes))
				{
					NamedSlotLines.Add(FString::ChrN((Depth + 1) * 2, TEXT(' ')) + NamedSlotKeyword + SlotName.ToString());
					NamedSlotLines.Append(ContentLines);
					bAnyChild = true;
				}
			}
		}

		if (!bMatches && !bAnyChild)
		{
			return false;
		}

		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		const FString Inner = FString::ChrN((Depth + 1) * 2, TEXT(' '));

		OutLines.Add(Indent + DescribeWidget(Widget));

		// An ancestor of a match comes out bare: it is there to say where the
		// match is, and a bare line writes back as an anchor, changing nothing.
		if (bMatches)
		{
			// Whoever filters is asking for these widgets: their values come out
			// whole, as the sheet does for a property asked for by name.
			TArray<FString> PropertyLines;
			FNodeScribeObjectReader::ReadProperties(Widget, WidgetSkip(), Name, bFiltering,
				PropertyLines, Notes.TooLong, Notes.Unreadable);
			for (const FString& Line : PropertyLines)
			{
				OutLines.Add(Inner + Line);
			}

			if (const UPanelSlot* Slot = Widget->Slot)
			{
				TArray<FString> SlotLines;
				FNodeScribeObjectReader::ReadProperties(Slot, SlotSkip(), Name + TEXT(".slot"),
					bFiltering, SlotLines, Notes.TooLong, Notes.Unreadable);

				// The keyword goes on the property, not on a struct member under
				// it: `slot Padding:` opens the block, its members follow indented.
				for (const FString& Line : SlotLines)
				{
					OutLines.Add(Inner + (Line.StartsWith(TEXT(" ")) ? Line : TEXT("slot ") + Line));
				}
			}

			// A value bound to a function is not in the widget's properties at
			// all: the Text shows the Designer's placeholder and the function
			// decides at runtime. Without this, the reading says "-" for a text
			// that is never "-".
			for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
			{
				if (Binding.ObjectName != Name)
				{
					continue;
				}

				FString Property = Binding.PropertyName.ToString();
				Property.RemoveFromEnd(TEXT("Delegate"));

				const FName Source = Binding.Kind == EBindingKind::Function
					? Binding.FunctionName : Binding.SourceProperty;

				OutLines.Add(FString::Printf(TEXT("%s# %s is bound to `%s`"),
					*Inner, *Property, *Source.ToString()));
			}
		}

		OutLines.Append(NamedSlotLines);
		OutLines.Append(ChildLines);
		return true;
	}
}

namespace
{
	/**
	 * The widget class by the name the Palette shows.
	 *
	 * Two sources: the loaded classes, and the Widget Blueprints the Asset
	 * Registry knows -- a Blueprint not opened this session is not a class in
	 * memory yet, and answering "does not exist" for it would be a lie.
	 * Ambiguous is refused with the paths, never chosen.
	 */
	UClass* ResolveWidgetClass(const FString& Written, FString& OutError)
	{
		if (Written.StartsWith(TEXT("/")))
		{
			UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *Written);
			if (const UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Loaded))
			{
				if (UClass* Generated = Blueprint->GeneratedClass.Get())
				{
					return Generated;
				}
			}
			if (UClass* Class = Cast<UClass>(Loaded))
			{
				if (Class->IsChildOf(UWidget::StaticClass()))
				{
					return Class;
				}
			}
			OutError = FString::Printf(TEXT("`%s` is not a widget class or a Widget Blueprint."), *Written);
			return nullptr;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Written);

		TArray<UClass*> Found;
		TSet<FString> FoundPaths;
		TArray<FString> Near;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UWidget::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}

			const FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_"))
				|| ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

			const FString Candidate = FNodeScribeCatalog::Normalize(WidgetClassName(Class));
			if (Candidate == Wanted)
			{
				Found.AddUnique(Class);
				if (const UObject* Blueprint = Class->ClassGeneratedBy)
				{
					FoundPaths.Add(Blueprint->GetPathName());
				}
			}
			else if (Near.Num() < 8 && (Candidate.Contains(Wanted) || FNodeScribeCatalog::IsNearMiss(Candidate, Wanted)))
			{
				Near.AddUnique(WidgetClassName(Class));
			}
		}

		TArray<FAssetData> Unloaded;
		{
			TArray<FAssetData> Assets;
			IAssetRegistry& Registry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			Registry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets, true);

			for (const FAssetData& Asset : Assets)
			{
				const FString Candidate = FNodeScribeCatalog::Normalize(Asset.AssetName.ToString());
				if (Candidate == Wanted && !FoundPaths.Contains(Asset.GetObjectPathString()))
				{
					Unloaded.Add(Asset);
				}
				else if (Near.Num() < 8 && Candidate != Wanted && (Candidate.Contains(Wanted) || FNodeScribeCatalog::IsNearMiss(Candidate, Wanted)))
				{
					Near.AddUnique(Asset.AssetName.ToString());
				}
			}
		}

		if (Found.Num() + Unloaded.Num() > 1)
		{
			TArray<FString> Paths;
			for (const UClass* Class : Found)
			{
				Paths.Add(Class->ClassGeneratedBy ? Class->ClassGeneratedBy->GetPathName() : Class->GetPathName());
			}
			for (const FAssetData& Asset : Unloaded)
			{
				Paths.Add(Asset.GetObjectPathString());
			}
			OutError = FString::Printf(TEXT("`%s` is ambiguous -- write the full path instead: %s"),
				*Written, *FString::Join(Paths, TEXT(", ")));
			return nullptr;
		}

		if (Found.Num() == 1)
		{
			return Found[0];
		}

		if (Unloaded.Num() == 1)
		{
			if (const UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Unloaded[0].GetAsset()))
			{
				if (UClass* Generated = Blueprint->GeneratedClass.Get())
				{
					return Generated;
				}
			}
			OutError = FString::Printf(TEXT("`%s` did not load as a compiled Widget Blueprint."),
				*Unloaded[0].GetObjectPathString());
			return nullptr;
		}

		OutError = FString::Printf(TEXT("no widget class called `%s`.%s"), *Written,
			Near.Num() > 0
				? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", ")))
				: TEXT(""));
		return nullptr;
	}

	/** One `Name : Class ...` line of the text, and what it resolved to. */
	struct FWidgetLine
	{
		int32 Line = 0;
		int32 Indent = 0;
		FString Name;
		FString ClassText;
		FString LikeName;
		bool bVariableWord = false;
		TOptional<bool> IsVariable;  // from an `Is Variable = ...` line

		int32 Parent = INDEX_NONE;   // index into the widget lines
		TArray<int32> Children;

		UClass* Class = nullptr;
		UWidget* Existing = nullptr;
		UWidget* Widget = nullptr;   // after applying: the existing one or the new one

		// A `named slot Name` line: not a widget, the place its one child goes.
		bool bNamedSlot = false;
		FName SlotName;

		// An existing widget written under a parent it is not in.
		bool bMove = false;

		// The property lines, at their own line numbers -- the sheet's writer
		// reports `line N`, and N has to be the line of the text that was sent.
		TArray<FString> OwnText;
		TArray<FString> SlotText;
		bool bLastWasSlot = false;
	};

	struct FWidgetRename
	{
		int32 Line = 0;
		FString From;
		FString To;
		UWidget* Widget = nullptr;
	};

	struct FWidgetDelete
	{
		int32 Line = 0;
		FString Name;
		UWidget* Widget = nullptr;
	};

	/**
	 * `Name : Class`, and not `Text = "a: b"` nor a struct block's `Font:`.
	 * The colon has to come before any `=`, and something has to follow it.
	 */
	bool SplitWidgetLine(const FString& Line, FString& OutName, FString& OutRest)
	{
		int32 Colon = INDEX_NONE;
		int32 Equals = INDEX_NONE;
		Line.FindChar(TEXT(':'), Colon);
		Line.FindChar(TEXT('='), Equals);

		if (Colon == INDEX_NONE || (Equals != INDEX_NONE && Equals < Colon))
		{
			return false;
		}

		OutName = Line.Left(Colon).TrimStartAndEnd();
		OutRest = Line.RightChop(Colon + 1).TrimStartAndEnd();
		return !OutName.IsEmpty() && !OutRest.IsEmpty();
	}

	/**
	 * Content of a named slot: not the root, and with no panel above it.
	 * MoveWidget only knows panels, so moving in or out of a slot is refused.
	 */
	bool IsNamedSlotContent(const UWidgetTree* Tree, const UWidget* Widget)
	{
		return Widget && !Widget->GetParent() && Widget != Tree->RootWidget;
	}

	/** What a host offers: a user widget instance's slots, or the parent class's for the tree. */
	TArray<FName> NamedSlotsOfHost(const UWidgetBlueprint* Blueprint, const UObject* Host)
	{
		TArray<FName> Names;
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Host))
		{
			UserWidget->GetSlotNames(Names);
		}
		else if (!Host)
		{
			Names = Blueprint->GetInheritedAvailableNamedSlots();
		}
		return Names;
	}

	/** Puts a line in a sparse text at its own line number. */
	void Place(TArray<FString>& Text, int32 LineCount, int32 Index, const FString& Line)
	{
		if (Text.Num() == 0)
		{
			Text.SetNum(LineCount);
		}
		Text[Index] = Line;
	}

	/**
	 * `like Other`: the source's look onto the target, property by property.
	 *
	 * Instanced objects are left out on purpose: copying the pointer would make
	 * two widgets share one subobject, and editing one would change the other.
	 * That is also why the slot is copied apart, as its own object.
	 */
	int32 CopyLook(const UObject* From, UObject* To, const TSet<FName>& Skip)
	{
		if (!From || !To || From->GetClass() != To->GetClass())
		{
			return 0;
		}

		To->Modify();

		int32 Copied = 0;
		for (TFieldIterator<FProperty> It(To->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit)
				|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst
					| CPF_InstancedReference | CPF_ContainsInstancedReference)
				|| Skip.Contains(Property->GetFName()))
			{
				continue;
			}

			const void* Source = Property->ContainerPtrToValuePtr<void>(From);
			void* Target = Property->ContainerPtrToValuePtr<void>(To);
			if (!Property->Identical(Source, Target, PPF_None))
			{
				Property->CopyCompleteValue(Target, Source);
				++Copied;
			}
		}

		if (Copied > 0)
		{
			To->PostEditChange();
		}
		return Copied;
	}
}

namespace NodeScribeWidgetTree
{

TSet<FName> CollectWidgetNames(const UWidgetBlueprint* Blueprint)
{
	TSet<FName> Names;
	if (Blueprint && Blueprint->WidgetTree)
	{
		Blueprint->WidgetTree->ForEachWidget([&Names](const UWidget* Widget)
		{
			Names.Add(Widget->GetFName());
		});
	}
	return Names;
}

FString Read(const UWidgetBlueprint* Blueprint, const FString& NormalizedFilter)
{
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		return FString();
	}

	const bool bFiltering = !NormalizedFilter.IsEmpty();

	TArray<FString> Lines;
	Lines.Add(SectionKeyword);

	const UWidgetTree* Tree = Blueprint->WidgetTree;
	const UWidget* Root = Tree->RootWidget;

	FReadNotes Notes;
	bool bAny = Root && ReadWidget(Blueprint, Root, 0, NormalizedFilter, Lines, Notes);

	// A child Widget Blueprint has no root of its own: what it designs is the
	// content of the named slots its parent class exposes, kept in its tree.
	const TArray<FName> InheritedSlots = Blueprint->GetInheritedAvailableNamedSlots();
	for (const FName& SlotName : InheritedSlots)
	{
		const UWidget* Content = Tree->GetContentForSlot(SlotName);
		if (!Content)
		{
			// Said only when there is no root: then the slots are the whole
			// Designer, and an empty one is a place to write into.
			if (!Root && !bFiltering)
			{
				Lines.Add(NamedSlotKeyword + SlotName.ToString() + TEXT("  # empty"));
			}
			continue;
		}

		TArray<FString> ContentLines;
		if (ReadWidget(Blueprint, Content, 1, NormalizedFilter, ContentLines, Notes))
		{
			Lines.Add(NamedSlotKeyword + SlotName.ToString());
			Lines.Append(ContentLines);
			bAny = true;
		}
	}

	if (!bAny)
	{
		if (bFiltering)
		{
			return FString();
		}
		if (!Root && InheritedSlots.Num() == 0)
		{
			Lines.Add(TEXT("# empty: the Designer has no root widget"));
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	if (Notes.TooLong.Num() > 0)
	{
		Lines.Add(FString::Printf(
			TEXT("# [note]: changed, but the value is too long for the overview -- ")
			TEXT("filter by the widget's name to see it whole: %s"),
			*FString::Join(Notes.TooLong, TEXT(", "))));
	}

	if (Notes.Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [warning]: I do not know how to write the value of: %s"),
			*FString::Join(Notes.Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}

bool SplitText(const FString& Text, FString& OutSheet, FString& OutDesigner)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	int32 Section = INDEX_NONE;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (WidgetIndent(Lines[Index]) == 0
			&& IsSectionLine(FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd()))
		{
			Section = Index;
			break;
		}
	}

	if (Section == INDEX_NONE)
	{
		OutSheet = Text;
		OutDesigner.Empty();
		return false;
	}

	TArray<FString> Sheet = Lines;
	TArray<FString> Designer = Lines;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		(Index < Section ? Designer : Sheet)[Index].Empty();
	}

	OutSheet = FString::Join(Sheet, TEXT("\n"));
	OutDesigner = FString::Join(Designer, TEXT("\n"));
	return true;
}

FResult Write(UWidgetBlueprint* Blueprint, const FString& Text)
{
	FResult Result;

	if (!Blueprint || !Blueprint->WidgetTree)
	{
		Result.Diagnostics.Add(TEXT("[error]: not a Widget Blueprint with a Designer."));
		return Result;
	}

	UWidgetTree* Tree = Blueprint->WidgetTree;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	TArray<FWidgetLine> Widgets;
	TArray<FWidgetRename> Renames;
	TArray<FWidgetDelete> Deletes;

	auto Error = [&Result](int32 Line, const FString& Message)
	{
		Result.Diagnostics.Add(FString::Printf(TEXT("line %d [error]: %s"), Line, *Message));
	};

	// --- Parse ---------------------------------------------------------------

	int32 Current = INDEX_NONE;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const int32 Indent = WidgetIndent(Lines[Index]);
		const FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();

		if (Line.IsEmpty() || IsSectionLine(Line) || Line.StartsWith(TEXT("~")))
		{
			continue;
		}

		// The owner of this line is the nearest widget line above it with less
		// indentation -- the same rule as everywhere in the format.
		while (Current != INDEX_NONE && Widgets[Current].Indent >= Indent)
		{
			Current = Widgets[Current].Parent;
		}

		if (Line.StartsWith(TEXT("rename "), ESearchCase::IgnoreCase))
		{
			FString From;
			FString To;
			if (!Line.RightChop(7).Split(TEXT("->"), &From, &To))
			{
				Error(LineNumber, TEXT("write it `rename OldName -> NewName`."));
				continue;
			}
			FWidgetRename& Rename = Renames.AddDefaulted_GetRef();
			Rename.Line = LineNumber;
			Rename.From = From.TrimStartAndEnd();
			Rename.To = To.TrimStartAndEnd();
			continue;
		}

		if (Line.StartsWith(TEXT("delete widget "), ESearchCase::IgnoreCase))
		{
			FWidgetDelete& Delete = Deletes.AddDefaulted_GetRef();
			Delete.Line = LineNumber;
			Delete.Name = Line.RightChop(14).TrimStartAndEnd();
			continue;
		}

		if (Line.StartsWith(NamedSlotKeyword, ESearchCase::IgnoreCase))
		{
			FWidgetLine& Slot = Widgets.AddDefaulted_GetRef();
			Slot.Line = LineNumber;
			Slot.Indent = Indent;
			Slot.Name = Line.RightChop(FCString::Strlen(NamedSlotKeyword)).TrimStartAndEnd();
			Slot.bNamedSlot = true;
			Slot.Parent = Current;

			if (Current != INDEX_NONE && Widgets[Current].bNamedSlot)
			{
				Error(LineNumber, TEXT("a named slot holds a widget, not another named slot."));
			}
			else if (Current != INDEX_NONE && Widgets[Current].Indent != Indent - 1)
			{
				Error(LineNumber, FString::Printf(TEXT("`named slot %s` is indented more than one level under `%s`."),
					*Slot.Name, *Widgets[Current].Name));
			}
			else if (Current == INDEX_NONE && Indent > 0)
			{
				Error(LineNumber, FString::Printf(
					TEXT("`named slot %s` is indented, but there is no widget above it."), *Slot.Name));
			}

			Current = Widgets.Num() - 1;
			continue;
		}

		FString Name;
		FString Rest;
		if (SplitWidgetLine(Line, Name, Rest))
		{
			FWidgetLine& Widget = Widgets.AddDefaulted_GetRef();
			Widget.Line = LineNumber;
			Widget.Indent = Indent;
			Widget.Name = Name;

			const int32 LikeAt = Rest.Find(TEXT(" like "), ESearchCase::IgnoreCase);
			if (LikeAt != INDEX_NONE)
			{
				Widget.LikeName = Rest.RightChop(LikeAt + 6).TrimStartAndEnd();
				Rest = Rest.Left(LikeAt).TrimEnd();
			}

			if (Rest.EndsWith(TEXT(" variable"), ESearchCase::IgnoreCase))
			{
				Widget.bVariableWord = true;
				Rest = Rest.LeftChop(9).TrimEnd();
			}
			Widget.ClassText = Rest;

			const int32 Self = Widgets.Num() - 1;
			Widget.Parent = Current;

			if (Current != INDEX_NONE)
			{
				if (Widgets[Current].Indent != Indent - 1)
				{
					Error(LineNumber, FString::Printf(
						TEXT("`%s` is indented more than one level under `%s`."),
						*Name, *Widgets[Current].Name));
				}
				Widgets[Current].Children.Add(Self);
			}
			else if (Indent > 0)
			{
				Error(LineNumber, FString::Printf(
					TEXT("`%s` is indented, but there is no widget above it."), *Name));
			}

			Current = Self;
			continue;
		}

		// A property line: it belongs to the widget whose block it is in.
		if (Current == INDEX_NONE)
		{
			Error(LineNumber, FString::Printf(
				TEXT("`%s` is not under any widget. Write the widget's line above it: `Name : Class`."),
				*Line));
			continue;
		}

		FWidgetLine& Owner = Widgets[Current];
		if (Owner.bNamedSlot)
		{
			Error(LineNumber, FString::Printf(
				TEXT("under `named slot %s` goes one widget line, `Name : Class`."), *Owner.Name));
			continue;
		}
		const int32 Depth = Indent - (Owner.Indent + 1);

		if (Depth == 0)
		{
			Owner.bLastWasSlot = Line.StartsWith(TEXT("slot "), ESearchCase::IgnoreCase);
			const FString Body = Owner.bLastWasSlot ? Line.RightChop(5).TrimStart() : Line;

			// Is Variable is not a property the sheet can write: ticking it goes
			// through the editor's own toggle, which also renames the graph's
			// references to it.
			FString Left;
			FString Value;
			if (!Owner.bLastWasSlot && Body.Split(TEXT("="), &Left, &Value)
				&& FNodeScribeCatalog::Normalize(Left) == TEXT("isvariable"))
			{
				Value.TrimStartAndEndInline();
				if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
				{
					Owner.IsVariable = true;
				}
				else if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
				{
					Owner.IsVariable = false;
				}
				else
				{
					Error(LineNumber, TEXT("Is Variable takes `true` or `false`."));
				}
				continue;
			}

			Place(Owner.bLastWasSlot ? Owner.SlotText : Owner.OwnText, Lines.Num(), Index, Body);
		}
		else
		{
			// A struct member under the property line above.
			Place(Owner.bLastWasSlot ? Owner.SlotText : Owner.OwnText, Lines.Num(), Index,
				FString::ChrN(Depth * 2, TEXT(' ')) + Line);
		}
	}

	// --- Resolve, before touching anything ------------------------------------

	TMap<FString, UWidget*> RenamedTo;    // new name -> widget
	TSet<UWidget*> RenamedAway;

	auto FindByName = [&](const FString& Name) -> UWidget*
	{
		if (UWidget* const* Renamed = RenamedTo.Find(Name.ToLower()))
		{
			return *Renamed;
		}
		UWidget* Found = Tree->FindWidget(FName(*Name));
		return Found && !RenamedAway.Contains(Found) ? Found : nullptr;
	};

	for (FWidgetRename& Rename : Renames)
	{
		Rename.Widget = FindByName(Rename.From);
		if (!Rename.Widget)
		{
			Error(Rename.Line, FString::Printf(TEXT("there is no widget called `%s`."), *Rename.From));
			continue;
		}

		if (FindByName(Rename.To))
		{
			Error(Rename.Line, FString::Printf(TEXT("`%s` is already taken."), *Rename.To));
			continue;
		}

		FText Why;
		if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(Blueprint, Rename.Widget,
			FText::FromString(Rename.To), Why))
		{
			Error(Rename.Line, FString::Printf(TEXT("cannot rename `%s` to `%s`: %s"),
				*Rename.From, *Rename.To, *Why.ToString()));
			continue;
		}

		RenamedAway.Add(Rename.Widget);
		RenamedTo.Add(Rename.To.ToLower(), Rename.Widget);
	}

	TSet<FString> Mentioned;
	for (const FWidgetLine& Widget : Widgets)
	{
		if (!Widget.bNamedSlot)
		{
			Mentioned.Add(Widget.Name.ToLower());
		}
	}

	TSet<UWidget*> Deleted;
	for (FWidgetDelete& Delete : Deletes)
	{
		Delete.Widget = FindByName(Delete.Name);
		if (!Delete.Widget)
		{
			Error(Delete.Line, FString::Printf(TEXT("there is no widget called `%s`."), *Delete.Name));
			continue;
		}
		if (Mentioned.Contains(Delete.Name.ToLower()))
		{
			Error(Delete.Line, FString::Printf(
				TEXT("`%s` is deleted and also written as a widget line -- which one is meant?"),
				*Delete.Name));
			continue;
		}
		Deleted.Add(Delete.Widget);
	}

	// Under a deleted panel, its children go too; a line naming one of them
	// would write into a widget that is about to vanish.
	auto IsBeingDeleted = [&Deleted](const UWidget* Widget)
	{
		for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
		{
			if (Deleted.Contains(Current))
			{
				return true;
			}
		}
		return false;
	};

	const bool bRootStays = Tree->RootWidget && !Deleted.Contains(Tree->RootWidget);
	int32 NewRoots = 0;
	TSet<FString> Seen;

	// The host a named slot line belongs to: the widget above it, or -- at
	// the top -- this Blueprint's own tree, for a child Widget Blueprint.
	auto SlotHost = [&](const FWidgetLine& Slot) -> UObject*
	{
		if (Slot.Parent == INDEX_NONE)
		{
			return nullptr;
		}
		const FWidgetLine& Host = Widgets[Slot.Parent];
		if (Host.Widget)
		{
			return Host.Widget;
		}
		if (Host.Existing)
		{
			return Host.Existing;
		}
		return Host.Class ? Host.Class->GetDefaultObject() : nullptr;
	};

	auto SlotContent = [&](const FWidgetLine& Slot) -> UWidget*
	{
		if (Slot.Parent == INDEX_NONE)
		{
			return Tree->GetContentForSlot(Slot.SlotName);
		}
		const FWidgetLine& Host = Widgets[Slot.Parent];
		const UUserWidget* UserWidget = Cast<UUserWidget>(Host.Widget ? Host.Widget : Host.Existing);
		return UserWidget ? UserWidget->GetContentForSlot(Slot.SlotName) : nullptr;
	};

	for (int32 Self = 0; Self < Widgets.Num(); ++Self)
	{
		FWidgetLine& Widget = Widgets[Self];

		if (Widget.bNamedSlot)
		{
			const UObject* Host = SlotHost(Widget);
			if (Widget.Parent != INDEX_NONE && !Cast<UUserWidget>(Host))
			{
				Error(Widget.Line, FString::Printf(TEXT("`%s` is not a user widget: it has no named slots."),
					*Widgets[Widget.Parent].Name));
				continue;
			}

			const TArray<FName> Available = NamedSlotsOfHost(Blueprint, Host);
			const FName* Match = Available.FindByPredicate([&Widget](const FName& Candidate)
			{
				return Candidate.ToString().Equals(Widget.Name, ESearchCase::IgnoreCase);
			});
			if (!Match)
			{
				TArray<FString> Names;
				for (const FName& Candidate : Available)
				{
					Names.Add(Candidate.ToString());
				}
				Error(Widget.Line, FString::Printf(TEXT("there is no named slot `%s` here. %s"), *Widget.Name,
					Names.Num() > 0 ? *(TEXT("It has: ") + FString::Join(Names, TEXT(", ")))
						: TEXT("It has none.")));
				continue;
			}
			Widget.SlotName = *Match;

			if (Widget.Children.Num() > 1)
			{
				Error(Widget.Line, FString::Printf(TEXT("`named slot %s` holds a single widget."), *Widget.Name));
			}
			continue;
		}

		if (Seen.Contains(Widget.Name.ToLower()))
		{
			Error(Widget.Line, FString::Printf(TEXT("`%s` is written twice."), *Widget.Name));
			continue;
		}
		Seen.Add(Widget.Name.ToLower());

		FString Why;
		Widget.Class = ResolveWidgetClass(Widget.ClassText, Why);
		if (!Widget.Class)
		{
			Error(Widget.Line, Why);
			continue;
		}

		Widget.Existing = FindByName(Widget.Name);
		const FWidgetLine* Parent = Widget.Parent != INDEX_NONE ? &Widgets[Widget.Parent] : nullptr;

		// Under a named slot: the existing content stays where it is, and a new
		// widget fills an empty slot. Moving into or out of a slot is not written.
		if (Parent && Parent->bNamedSlot)
		{
			UWidget* Content = SlotContent(*Parent);
			if (Widget.Existing && Content != Widget.Existing)
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`%s` is not in `named slot %s`. Moving a widget into a named slot is not written yet: ")
					TEXT("drag it in the Designer's Hierarchy."), *Widget.Name, *Parent->Name));
				continue;
			}
			if (!Widget.Existing && Content && !IsBeingDeleted(Content))
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`named slot %s` already holds `%s`; delete it first, or write it instead."),
					*Parent->Name, *Content->GetName()));
				continue;
			}
		}

		if (Widget.Existing)
		{
			if (IsBeingDeleted(Widget.Existing))
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`%s` is inside a widget this text deletes."), *Widget.Name));
				continue;
			}

			if (Widget.Existing->GetClass() != Widget.Class)
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`%s` is a %s, not a %s. Changing a widget's class is not written yet: ")
					TEXT("use Replace With in the Designer's Hierarchy."),
					*Widget.Name, *WidgetClassName(Widget.Existing->GetClass()), *Widget.ClassText));
				continue;
			}

			// A line at the top of the text is an anchor and may name any widget.
			// Indented, it says who the parent is -- and a different parent is a
			// move, there, at the place its neighbours in the text give it, as for
			// a new widget. The return names every move.
			if (Parent && !Parent->bNamedSlot)
			{
				const UWidget* Actual = Widget.Existing->GetParent();
				if (!Parent->Existing || Actual != Parent->Existing)
				{
					if (Widget.Existing == Tree->RootWidget)
					{
						Error(Widget.Line, FString::Printf(
							TEXT("`%s` is the root; the root does not move under another widget."), *Widget.Name));
						continue;
					}
					if (IsNamedSlotContent(Tree, Widget.Existing))
					{
						Error(Widget.Line, FString::Printf(
							TEXT("`%s` is in a named slot. Moving a widget out of a named slot is not written yet: ")
							TEXT("drag it in the Designer's Hierarchy."), *Widget.Name));
						continue;
					}

					// A parent whose own line failed already said so.
					if (!Parent->Class)
					{
						continue;
					}
					const UPanelWidget* Panel = Cast<UPanelWidget>(Parent->Class->GetDefaultObject());
					if (!Panel)
					{
						Error(Widget.Line, FString::Printf(TEXT("`%s` cannot have children: a %s is not a panel."),
							*Parent->Name, *Parent->ClassText));
						continue;
					}

					if (Parent->Existing)
					{
						FText CannotMove;
						if (!FWidgetBlueprintOperationUtils::CanAddToParent(Blueprint, Parent->Existing, CannotMove))
						{
							Error(Widget.Line, FString::Printf(TEXT("`%s` does not take `%s`: %s"),
								*Parent->Name, *Widget.Name, *CannotMove.ToString()));
							continue;
						}
						if (!FWidgetBlueprintOperationUtils::IsParentChildCycleFree(Blueprint, Widget.Existing, Parent->Existing))
						{
							Error(Widget.Line, FString::Printf(TEXT("`%s` is inside `%s`; it cannot go under it."),
								*Parent->Name, *Widget.Name));
							continue;
						}
					}

					Widget.bMove = true;
				}
			}
		}
		else
		{
			FKismetNameValidator Validator(Blueprint);
			if (Validator.IsValid(FName(*Widget.Name)) != EValidatorResult::Ok
				|| RenamedTo.Contains(Widget.Name.ToLower()))
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`%s` is new, but the name is taken by a variable, function or widget of this Blueprint."),
					*Widget.Name));
				continue;
			}

			if (Widget.Class->HasAnyClassFlags(CLASS_Abstract))
			{
				Error(Widget.Line, FString::Printf(TEXT("`%s` is abstract."), *Widget.ClassText));
				continue;
			}

			if (Blueprint->GeneratedClass && Widget.Class->IsChildOf(Blueprint->GeneratedClass))
			{
				Error(Widget.Line, TEXT("a Widget Blueprint cannot contain itself."));
				continue;
			}

			if (Parent && Parent->bNamedSlot)
			{
				// Checked above: the slot is there and empty.
			}
			else if (!Parent)
			{
				if (bRootStays)
				{
					Error(Widget.Line, FString::Printf(
						TEXT("`%s` is new and has no parent. The Designer has one root (`%s`): ")
						TEXT("write the new widget indented under the panel it goes in."),
						*Widget.Name, *Tree->RootWidget->GetName()));
					continue;
				}
				if (++NewRoots > 1)
				{
					Error(Widget.Line, TEXT("the Designer has one root; this is a second one."));
					continue;
				}
			}
			else
			{
				const UClass* ParentClass = Parent->Class;
				if (!ParentClass)
				{
					continue;
				}
				const UPanelWidget* Panel = Cast<UPanelWidget>(ParentClass->GetDefaultObject());

				if (!Panel)
				{
					Error(Widget.Line, FString::Printf(
						TEXT("`%s` cannot have children: a %s is not a panel."),
						*Parent->Name, *Parent->ClassText));
					continue;
				}

				if (Parent->Existing)
				{
					FText CannotAdd;
					if (!FWidgetBlueprintOperationUtils::CanAddToParent(Blueprint, Parent->Existing, CannotAdd))
					{
						Error(Widget.Line, FString::Printf(TEXT("`%s` does not take `%s`: %s"),
							*Parent->Name, *Widget.Name, *CannotAdd.ToString()));
						continue;
					}
				}
				else if (!Panel->CanHaveMultipleChildren() && Parent->Children.Num() > 1)
				{
					Error(Widget.Line, FString::Printf(TEXT("a %s holds a single child."),
						*Parent->ClassText));
					continue;
				}
			}
		}

		if (!Widget.LikeName.IsEmpty())
		{
			// An existing widget, or one created by a line above this one.
			const UClass* LikeClass = nullptr;
			if (const UWidget* Source = FindByName(Widget.LikeName))
			{
				LikeClass = Source->GetClass();
			}
			for (int32 Earlier = 0; Earlier < Self && !LikeClass; ++Earlier)
			{
				if (Widgets[Earlier].Name.Equals(Widget.LikeName, ESearchCase::IgnoreCase))
				{
					LikeClass = Widgets[Earlier].Class;
				}
			}

			if (!LikeClass)
			{
				Error(Widget.Line, FString::Printf(TEXT("`like %s`: there is no widget called that."),
					*Widget.LikeName));
				continue;
			}
			if (LikeClass != Widget.Class)
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`like %s`: it is a %s, and the look only copies between widgets of one class."),
					*Widget.LikeName, *WidgetClassName(LikeClass)));
				continue;
			}
		}
	}

	// Existing siblings written in another order are put in that order. The
	// ones the text names take the places they had among themselves; a sibling
	// the text does not name keeps its own -- reordering two of five children
	// does not touch the other three.
	struct FReorder
	{
		UPanelWidget* Panel = nullptr;
		FString Name;
		TArray<UWidget*> Order;
	};
	TArray<FReorder> Reorders;

	for (const FWidgetLine& Widget : Widgets)
	{
		UPanelWidget* Panel = Cast<UPanelWidget>(Widget.Existing);
		if (!Panel || Widget.bNamedSlot)
		{
			continue;
		}

		FReorder Reorder;
		Reorder.Panel = Panel;
		Reorder.Name = Widget.Name;

		int32 Previous = -1;
		bool bOutOfOrder = false;
		for (int32 Child : Widget.Children)
		{
			UWidget* Existing = Widgets[Child].Existing;
			if (!Existing || Widgets[Child].bMove || Existing->GetParent() != Panel || Deleted.Contains(Existing))
			{
				continue;
			}
			const int32 At = Panel->GetChildIndex(Existing);
			bOutOfOrder |= At < Previous;
			Previous = At;
			Reorder.Order.Add(Existing);
		}

		if (bOutOfOrder)
		{
			Reorders.Add(MoveTemp(Reorder));
		}
	}

	bool bAnyError = false;
	for (const FString& Diagnostic : Result.Diagnostics)
	{
		bAnyError |= Diagnostic.Contains(TEXT("[error]"));
	}
	if (bAnyError)
	{
		Result.Diagnostics.Add(TEXT("[error]: nothing in the Designer was changed."));
		return Result;
	}

	// --- Apply -------------------------------------------------------------

	const FScopedTransaction Transaction(LOCTEXT("WriteDesignerTransaction", "NodeScribe: write Designer"));
	Blueprint->Modify();
	Tree->SetFlags(RF_Transactional);
	Tree->Modify();

	for (const FWidgetRename& Rename : Renames)
	{
		if (FWidgetBlueprintOperationUtils::RenameWidget(Blueprint, Rename.Widget, Rename.To))
		{
			++Result.Applied;
		}
		else
		{
			Error(Rename.Line, FString::Printf(TEXT("the Engine refused to rename `%s`."), *Rename.From));
		}
	}

	for (const FWidgetDelete& Delete : Deletes)
	{
		FText Why;
		if (FWidgetBlueprintOperationUtils::RemoveWidget(Blueprint, Delete.Widget, Why))
		{
			++Result.Applied;
		}
		else
		{
			Error(Delete.Line, FString::Printf(TEXT("could not delete `%s`: %s"), *Delete.Name, *Why.ToString()));
		}
	}

	for (const FReorder& Reorder : Reorders)
	{
		TArray<int32> Places;
		for (const UWidget* Child : Reorder.Order)
		{
			Places.Add(Reorder.Panel->GetChildIndex(Child));
		}
		Places.Sort();

		// One at a time into the sorted places. MoveWidget counts the index in
		// the list before taking the widget out, so a move to the right asks
		// for one more.
		bool bMoved = false;
		for (int32 Index = 0; Index < Reorder.Order.Num(); ++Index)
		{
			UWidget* Child = Reorder.Order[Index];
			const int32 Now = Reorder.Panel->GetChildIndex(Child);
			const int32 Wanted = Places[Index];
			if (Now == Wanted)
			{
				continue;
			}

			FText Why;
			if (!FWidgetBlueprintOperationUtils::MoveWidget(Blueprint, Child, Reorder.Panel,
				Now < Wanted ? Wanted + 1 : Wanted, Why))
			{
				Result.Diagnostics.Add(FString::Printf(TEXT("[error]: could not reorder `%s` in `%s`: %s"),
					*Child->GetName(), *Reorder.Name, *Why.ToString()));
				continue;
			}
			bMoved = true;
		}

		if (bMoved)
		{
			++Result.Applied;
			TArray<FString> Names;
			for (int32 Index = 0; Index < Reorder.Panel->GetChildrenCount(); ++Index)
			{
				Names.Add(Reorder.Panel->GetChildAt(Index)->GetName());
			}
			Result.Diagnostics.Add(FString::Printf(TEXT("[note]: reordered `%s`: %s"),
				*Reorder.Name, *FString::Join(Names, TEXT(", "))));
		}
	}

	// Where a widget goes among its siblings: right after the sibling written
	// above it, or right before the existing one written below it; with
	// neither, at the end -- the case of a parent written alone as an anchor,
	// with only the new child under it.
	auto PlaceAmongSiblings = [&](int32 Self, const UPanelWidget* Parent) -> int32
	{
		const TArray<int32>& Siblings = Widgets[Widgets[Self].Parent].Children;
		const int32 Position = Siblings.IndexOfByKey(Self);

		if (Position > 0 && Widgets[Siblings[Position - 1]].Widget
			&& Widgets[Siblings[Position - 1]].Widget->GetParent() == Parent)
		{
			return Parent->GetChildIndex(Widgets[Siblings[Position - 1]].Widget) + 1;
		}
		for (int32 Next = Position + 1; Next < Siblings.Num(); ++Next)
		{
			const UWidget* Below = Widgets[Siblings[Next]].Existing;
			if (Below && Below->GetParent() == Parent)
			{
				return Parent->GetChildIndex(Below);
			}
		}
		return INDEX_NONE;
	};

	auto ApplyVariable = [&](FWidgetLine& Widget, bool bIsNew)
	{
		bool bWanted = Widget.Widget->bIsVariable;
		if (Widget.IsVariable.IsSet())
		{
			bWanted = Widget.IsVariable.GetValue();
		}
		else if (Widget.bVariableWord)
		{
			bWanted = true;
		}
		else if (bIsNew)
		{
			// New and without the word: not a variable, whatever the class's
			// default. The line says everything the widget is.
			bWanted = false;
		}

		if (bWanted != Widget.Widget->bIsVariable)
		{
			FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(Blueprint, Widget.Widget, bWanted);
			if (!bIsNew)
			{
				++Result.Applied;
			}
		}
	};

	auto WriteSheet = [&](UObject* Target, const TArray<FString>& Sparse)
	{
		if (Sparse.Num() == 0)
		{
			return;
		}
		const FNodeScribeObjectWriter::FResult Sheet =
			FNodeScribeObjectWriter::WriteProperties(Target, FString::Join(Sparse, TEXT("\n")));
		Result.Applied += Sheet.Applied;
		Result.Diagnostics.Append(Sheet.Diagnostics);
	};

	for (int32 Self = 0; Self < Widgets.Num(); ++Self)
	{
		FWidgetLine& Widget = Widgets[Self];
		if (Widget.bNamedSlot)
		{
			continue;
		}

		const bool bIsNew = Widget.Existing == nullptr;
		const FWidgetLine* SlotLine = Widget.Parent != INDEX_NONE && Widgets[Widget.Parent].bNamedSlot
			? &Widgets[Widget.Parent] : nullptr;

		if (!bIsNew)
		{
			Widget.Widget = Widget.Existing;

			if (Widget.bMove)
			{
				UPanelWidget* NewParent = Cast<UPanelWidget>(Widgets[Widget.Parent].Widget);
				const UPanelWidget* OldParent = Widget.Existing->GetParent();
				FText Why;
				if (!NewParent)
				{
					Error(Widget.Line, FString::Printf(TEXT("`%s` was not created, so `%s` has nowhere to go."),
						*Widgets[Widget.Parent].Name, *Widget.Name));
				}
				else if (FWidgetBlueprintOperationUtils::MoveWidget(Blueprint, Widget.Existing, NewParent,
					PlaceAmongSiblings(Self, NewParent), Why))
				{
					++Result.Applied;
					Result.Diagnostics.Add(FString::Printf(TEXT("line %d [note]: moved `%s` from `%s` to `%s`."),
						Widget.Line, *Widget.Name, OldParent ? *OldParent->GetName() : TEXT("?"),
						*NewParent->GetName()));
				}
				else
				{
					Error(Widget.Line, FString::Printf(TEXT("could not move `%s`: %s"), *Widget.Name, *Why.ToString()));
				}
			}
		}
		else if (SlotLine)
		{
			UObject* Host = SlotLine->Parent != INDEX_NONE ? Widgets[SlotLine->Parent].Widget : nullptr;
			if (SlotLine->Parent != INDEX_NONE && !Host)
			{
				Error(Widget.Line, FString::Printf(TEXT("`%s` was not created, so `%s` has nowhere to go."),
					*Widgets[SlotLine->Parent].Name, *Widget.Name));
				continue;
			}

			FAssetData Asset = Widget.Class->ClassGeneratedBy
				? FAssetData(Widget.Class->ClassGeneratedBy.Get()) : FAssetData(Widget.Class);

			FText Why;
			UWidget* Created = FWidgetBlueprintOperationUtils::CreateWidgetFromAsset(Blueprint, Asset, Tree, Why);
			if (!Created)
			{
				Error(Widget.Line, FString::Printf(TEXT("could not create `%s`: %s"), *Widget.Name, *Why.ToString()));
				continue;
			}
			if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(Blueprint, Created,
				FText::FromString(Widget.Name), Why))
			{
				FWidgetBlueprintOperationUtils::RemoveTransientWidgetFromTree(Blueprint, Created);
				Error(Widget.Line, FString::Printf(TEXT("cannot name it `%s`: %s"), *Widget.Name, *Why.ToString()));
				continue;
			}

			// The host keeps the binding: the instance for a user widget's slot,
			// the tree for a slot this Blueprint inherits.
			if (UUserWidget* UserWidget = Cast<UUserWidget>(Host))
			{
				UserWidget->SetFlags(RF_Transactional);
				UserWidget->Modify();
				UserWidget->SetContentForSlot(SlotLine->SlotName, Created);
			}
			else
			{
				Tree->SetContentForSlot(SlotLine->SlotName, Created);
			}

			// What AddWidget does for a panel's child: without the widget's
			// variable GUID, the rename below trips an ensure in the Engine.
			Blueprint->OnVariableAdded(Created->GetFName());

			FWidgetBlueprintOperationUtils::RenameWidget(Blueprint, Created, Widget.Name);
			if (!Created->GetName().Equals(Widget.Name, ESearchCase::IgnoreCase))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [warning]: the Engine named it `%s`, not `%s`."),
					Widget.Line, *Created->GetName(), *Widget.Name));
			}

			Widget.Widget = Created;
			++Result.Applied;
		}
		else
		{
			UPanelWidget* Parent = Widget.Parent != INDEX_NONE
				? Cast<UPanelWidget>(Widgets[Widget.Parent].Widget) : nullptr;

			if (Widget.Parent != INDEX_NONE && !Parent)
			{
				Error(Widget.Line, FString::Printf(TEXT("`%s` was not created, so `%s` has nowhere to go."),
					*Widgets[Widget.Parent].Name, *Widget.Name));
				continue;
			}

			const int32 ChildIndex = Parent ? PlaceAmongSiblings(Self, Parent) : INDEX_NONE;

			FAssetData Asset = Widget.Class->ClassGeneratedBy
				? FAssetData(Widget.Class->ClassGeneratedBy.Get()) : FAssetData(Widget.Class);

			FText Why;
			UWidget* Created = FWidgetBlueprintOperationUtils::CreateWidgetFromAsset(Blueprint, Asset, Tree, Why);
			if (!Created)
			{
				Error(Widget.Line, FString::Printf(TEXT("could not create `%s`: %s"),
					*Widget.Name, *Why.ToString()));
				continue;
			}

			if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(Blueprint, Created,
				FText::FromString(Widget.Name), Why))
			{
				FWidgetBlueprintOperationUtils::RemoveTransientWidgetFromTree(Blueprint, Created);
				Error(Widget.Line, FString::Printf(TEXT("cannot name it `%s`: %s"), *Widget.Name, *Why.ToString()));
				continue;
			}

			if (!FWidgetBlueprintOperationUtils::AddWidget(Blueprint, Created, Parent, ChildIndex, Why))
			{
				Error(Widget.Line, FString::Printf(TEXT("could not add `%s`: %s"), *Widget.Name, *Why.ToString()));
				continue;
			}

			FWidgetBlueprintOperationUtils::RenameWidget(Blueprint, Created, Widget.Name);
			if (!Created->GetName().Equals(Widget.Name, ESearchCase::IgnoreCase))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [warning]: the Engine named it `%s`, not `%s`."),
					Widget.Line, *Created->GetName(), *Widget.Name));
			}

			Widget.Widget = Created;
			++Result.Applied;
		}

		if (!Widget.LikeName.IsEmpty())
		{
			UWidget* Source = FindByName(Widget.LikeName);
			for (int32 Earlier = 0; Earlier < Self && !Source; ++Earlier)
			{
				if (Widgets[Earlier].Name.Equals(Widget.LikeName, ESearchCase::IgnoreCase))
				{
					Source = Widgets[Earlier].Widget;
				}
			}

			if (Source && Source != Widget.Widget)
			{
				int32 Copied = CopyLook(Source, Widget.Widget, WidgetSkip());

				// The slot only when both sit in the same kind of panel: a
				// vertical box's slot has nothing to give a canvas's.
				if (Source->Slot && Widget.Widget->Slot
					&& Source->Slot->GetClass() == Widget.Widget->Slot->GetClass())
				{
					Copied += CopyLook(Source->Slot, Widget.Widget->Slot, SlotSkip());
				}

				if (Copied > 0 && !bIsNew)
				{
					++Result.Applied;
				}
			}
		}

		WriteSheet(Widget.Widget, Widget.OwnText);

		if (Widget.SlotText.Num() > 0)
		{
			if (UPanelSlot* Slot = Widget.Widget->Slot)
			{
				WriteSheet(Slot, Widget.SlotText);
			}
			else
			{
				Error(Widget.Line, FString::Printf(
					TEXT("`%s` has no slot to write into: it is the root, or a named slot's content."),
					*Widget.Name));
			}
		}

		ApplyVariable(Widget, bIsNew);
	}

	if (Result.Applied > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	return Result;
}

}

#undef LOCTEXT_NAMESPACE
