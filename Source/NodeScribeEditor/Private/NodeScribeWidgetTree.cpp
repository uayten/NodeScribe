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
		TArray<FString> NamedSlots;
	};

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

			// Content placed in a child user widget's named slot lives in that
			// instance, not in the panel's children. It is named, so its absence
			// is visible.
			if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
			{
				TArray<FName> SlotNames;
				UserWidget->GetSlotNames(SlotNames);
				for (const FName& SlotName : SlotNames)
				{
					if (UserWidget->GetContentForSlot(SlotName))
					{
						Notes.NamedSlots.Add(Name + TEXT(".") + SlotName.ToString());
					}
				}
			}
		}

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

	const UWidget* Root = Blueprint->WidgetTree->RootWidget;
	if (!Root)
	{
		if (bFiltering)
		{
			return FString();
		}
		Lines.Add(TEXT("# empty: the Designer has no root widget"));
		return FString::Join(Lines, TEXT("\n"));
	}

	FReadNotes Notes;
	if (!ReadWidget(Blueprint, Root, 0, NormalizedFilter, Lines, Notes))
	{
		return FString();
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

	if (Notes.NamedSlots.Num() > 0)
	{
		Lines.Add(FString::Printf(
			TEXT("# [warning]: named slot content is not read yet, and exists in: %s"),
			*FString::Join(Notes.NamedSlots, TEXT(", "))));
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
		Mentioned.Add(Widget.Name.ToLower());
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

	for (int32 Self = 0; Self < Widgets.Num(); ++Self)
	{
		FWidgetLine& Widget = Widgets[Self];

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
			// move, which nothing here does by accident.
			if (Parent)
			{
				const UWidget* Actual = Widget.Existing->GetParent();
				if (!Parent->Existing || Actual != Parent->Existing)
				{
					Error(Widget.Line, FString::Printf(
						TEXT("`%s` is under `%s`, not `%s`. Moving a widget is not written yet: ")
						TEXT("drag it in the Designer's Hierarchy, or write it under its real parent."),
						*Widget.Name, Actual ? *Actual->GetName() : TEXT("nothing (it is the root)"),
						*Parent->Name));
					continue;
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

			if (!Parent)
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
				const UPanelWidget* Panel = ParentClass
					? Cast<UPanelWidget>(ParentClass->GetDefaultObject()) : nullptr;

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

	// Existing siblings must be written in their real order: a new widget is
	// placed by its neighbours, and an order the tree does not have would put
	// it somewhere nobody wrote.
	for (const FWidgetLine& Widget : Widgets)
	{
		const UPanelWidget* Panel = Cast<UPanelWidget>(Widget.Existing);
		if (!Panel)
		{
			continue;
		}

		int32 Previous = -1;
		FString PreviousName;
		for (int32 Child : Widget.Children)
		{
			const UWidget* Existing = Widgets[Child].Existing;
			if (!Existing)
			{
				continue;
			}
			const int32 At = Panel->GetChildIndex(Existing);
			if (At < Previous)
			{
				Error(Widgets[Child].Line, FString::Printf(
					TEXT("`%s` comes before `%s` in `%s`. Reordering is not written yet: ")
					TEXT("write the existing children in their real order."),
					*Widgets[Child].Name, *PreviousName, *Widget.Name));
			}
			Previous = At;
			PreviousName = Widgets[Child].Name;
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
		const bool bIsNew = Widget.Existing == nullptr;

		if (!bIsNew)
		{
			Widget.Widget = Widget.Existing;
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

			// The place comes from the neighbours in the text: right after the
			// sibling written above it, or right before the one written below.
			// With neither, at the end -- the case of a parent written alone as
			// an anchor, with only the new child under it.
			int32 ChildIndex = INDEX_NONE;
			if (Parent)
			{
				const TArray<int32>& Siblings = Widgets[Widget.Parent].Children;
				const int32 Position = Siblings.IndexOfByKey(Self);

				if (Position > 0 && Widgets[Siblings[Position - 1]].Widget)
				{
					ChildIndex = Parent->GetChildIndex(Widgets[Siblings[Position - 1]].Widget) + 1;
				}
				else
				{
					for (int32 Next = Position + 1; Next < Siblings.Num(); ++Next)
					{
						if (const UWidget* Below = Widgets[Siblings[Next]].Existing)
						{
							ChildIndex = Parent->GetChildIndex(Below);
							break;
						}
					}
				}
			}

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
					TEXT("`%s` has no slot to write into: it is the root."), *Widget.Name));
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
