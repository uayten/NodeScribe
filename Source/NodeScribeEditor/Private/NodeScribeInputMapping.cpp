#include "NodeScribeInputMapping.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeInstanced.h"
#include "NodeScribeParser.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* MappingContextClassPath = TEXT("/Script/EnhancedInput.InputMappingContext");
	const TCHAR* InputActionClassPath = TEXT("/Script/EnhancedInput.InputAction");

	/** Where the mappings live, found by reflection: Enhanced Input is not linked. */
	struct FMappingAccess
	{
		const FArrayProperty* Array = nullptr;
		void* ArrayPtr = nullptr;
		const UScriptStruct* Struct = nullptr;
		const FObjectPropertyBase* Action = nullptr;
		const FStructProperty* Key = nullptr;
		const FArrayProperty* Triggers = nullptr;
		const FArrayProperty* Modifiers = nullptr;

		bool IsValid() const
		{
			return Array && ArrayPtr && Struct && Action && Key && Triggers && Modifiers
				&& Key->Struct == FKey::StaticStruct();
		}

		FKey& KeyOf(void* Element) const { return *Key->ContainerPtrToValuePtr<FKey>(Element); }
		const UObject* ActionOf(void* Element) const
		{
			return Action->GetObjectPropertyValue(Action->ContainerPtrToValuePtr<void>(Element));
		}
	};

	FMappingAccess FindMappingAccess(const UObject* Context)
	{
		FMappingAccess Access;
		if (!Context)
		{
			return Access;
		}

		const FStructProperty* Defaults = CastField<FStructProperty>(
			Context->GetClass()->FindPropertyByName(NodeScribeInputMapping::MappingsPropertyName()));
		if (!Defaults)
		{
			return Access;
		}

		Access.Array = CastField<FArrayProperty>(Defaults->Struct->FindPropertyByName(TEXT("Mappings")));
		const FStructProperty* Inner = Access.Array ? CastField<FStructProperty>(Access.Array->Inner) : nullptr;
		if (!Inner)
		{
			return Access;
		}

		Access.ArrayPtr = Access.Array->ContainerPtrToValuePtr<void>(
			const_cast<void*>(Defaults->ContainerPtrToValuePtr<void>(Context)));
		Access.Struct = Inner->Struct;
		Access.Action = CastField<FObjectPropertyBase>(Access.Struct->FindPropertyByName(TEXT("Action")));
		Access.Key = CastField<FStructProperty>(Access.Struct->FindPropertyByName(TEXT("Key")));
		Access.Triggers = CastField<FArrayProperty>(Access.Struct->FindPropertyByName(TEXT("Triggers")));
		Access.Modifiers = CastField<FArrayProperty>(Access.Struct->FindPropertyByName(TEXT("Modifiers")));
		return Access;
	}

	TArray<FAssetData> FindInputActions()
	{
		TArray<FAssetData> Assets;
		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.GetAssetsByClass(FTopLevelAssetPath(InputActionClassPath), Assets, /*bSearchSubClasses*/ true);
		return Assets;
	}

	/**
	 * The action by its asset name when no other Input Action shares it, the
	 * path otherwise -- the same rule the writer resolves with, so the line
	 * that comes out is one that goes back in.
	 */
	FString InputActionText(const UObject* Action, const TArray<FAssetData>& Actions)
	{
		if (!Action)
		{
			return TEXT("None");
		}

		int32 SameName = 0;
		for (const FAssetData& Asset : Actions)
		{
			SameName += Asset.AssetName == Action->GetFName() ? 1 : 0;
		}
		return SameName == 1 ? Action->GetName() : Action->GetPathName();
	}

	UObject* ResolveInputAction(const FString& Written, const TArray<FAssetData>& Actions, FString& OutError)
	{
		const UClass* ActionClass = FindObject<UClass>(nullptr, InputActionClassPath);

		if (Written.StartsWith(TEXT("/")))
		{
			UObject* Loaded = LoadObject<UObject>(nullptr, *Written);
			if (!Loaded || !ActionClass || !Loaded->IsA(ActionClass))
			{
				OutError = FString::Printf(TEXT("`%s` is not an Input Action."), *Written);
				return nullptr;
			}
			return Loaded;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Written);
		TArray<const FAssetData*> Found;
		TArray<FString> Near;
		for (const FAssetData& Asset : Actions)
		{
			const FString Candidate = FNodeScribeCatalog::Normalize(Asset.AssetName.ToString());
			if (Candidate == Wanted)
			{
				Found.Add(&Asset);
			}
			else if (Near.Num() < 8 && (Candidate.Contains(Wanted) || FNodeScribeCatalog::IsNearMiss(Candidate, Wanted)))
			{
				Near.Add(Asset.AssetName.ToString());
			}
		}

		if (Found.Num() == 1)
		{
			return Found[0]->GetAsset();
		}

		if (Found.Num() > 1)
		{
			TArray<FString> Paths;
			for (const FAssetData* Asset : Found)
			{
				Paths.Add(Asset->GetObjectPathString());
			}
			OutError = FString::Printf(TEXT("`%s` names more than one Input Action; write the path: %s"),
				*Written, *FString::Join(Paths, TEXT(", ")));
			return nullptr;
		}

		OutError = FString::Printf(TEXT("did not find Input Action `%s`.%s"), *Written,
			Near.Num() > 0 ? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", "))) : TEXT(""));
		return nullptr;
	}

	/** The key by its name (`SpaceBar`) or the name on screen (`Space Bar`). */
	bool ResolveKey(const FString& Written, FKey& OutKey, FString& OutError)
	{
		const FKey Direct = FKey(FName(*Written));
		if (Direct.IsValid())
		{
			OutKey = Direct;
			return true;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Written);
		TArray<FKey> AllKeys;
		EKeys::GetAllKeys(AllKeys);

		TArray<FString> Near;
		for (const FKey& Key : AllKeys)
		{
			const FString ByName = FNodeScribeCatalog::Normalize(Key.GetFName().ToString());
			const FString ByScreen = FNodeScribeCatalog::Normalize(Key.GetDisplayName().ToString());
			if (ByName == Wanted || ByScreen == Wanted)
			{
				OutKey = Key;
				return true;
			}
			if (Near.Num() < 8 && (ByName.Contains(Wanted) || FNodeScribeCatalog::IsNearMiss(ByName, Wanted)))
			{
				Near.Add(Key.GetFName().ToString());
			}
		}

		OutError = FString::Printf(TEXT("`%s` is not a key.%s"), *Written,
			Near.Num() > 0 ? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", "))) : TEXT(""));
		return false;
	}

	int32 MappingIndent(const FString& Line)
	{
		int32 Spaces = 0;
		while (Spaces < Line.Len() && Line[Spaces] == TEXT(' '))
		{
			++Spaces;
		}
		return Spaces / 2;
	}

	bool IsMappingsSectionLine(const FString& Line)
	{
		return Line.Equals(TEXT("mappings"), ESearchCase::IgnoreCase)
			|| Line.Equals(TEXT("mappings:"), ESearchCase::IgnoreCase);
	}

	/** `Key -> Action`, split. */
	bool SplitMappingLine(const FString& Line, FString& OutKey, FString& OutAction)
	{
		if (!Line.Split(TEXT("->"), &OutKey, &OutAction))
		{
			return false;
		}
		OutKey.TrimStartAndEndInline();
		OutAction.TrimStartAndEndInline();
		return !OutKey.IsEmpty() && !OutAction.IsEmpty();
	}

	/** One `Key -> Action` line and what is under it. */
	struct FMappingText
	{
		int32 Line = 0;
		bool bDelete = false;
		FString Key;
		FString Action;
		bool bHasChildren = false;
		TArray<NodeScribeInstanced::FElementText> Triggers;
		TArray<NodeScribeInstanced::FElementText> Modifiers;
	};
}

namespace NodeScribeInputMapping
{

bool Handles(const UObject* Object)
{
	const UClass* Class = FindObject<UClass>(nullptr, MappingContextClassPath);
	return Object && Class && Object->IsA(Class);
}

FName MappingsPropertyName()
{
	return TEXT("DefaultKeyMappings");
}

FString Read(const UObject* Context)
{
	const FMappingAccess Access = FindMappingAccess(Context);
	if (!Access.IsValid())
	{
		return TEXT("# [warning]: could not find this Input Mapping Context's mappings.");
	}

	const TArray<FAssetData> Actions = FindInputActions();

	TArray<FString> Lines;
	Lines.Add(TEXT("mappings"));

	TArray<FString> Unreadable;
	TArray<FString> NotShown;

	// The members the section does not say -- player-mappable settings --
	// compared with a fresh mapping, so a change there is named, not lost.
	TArray<uint8> Fresh;
	Fresh.SetNumZeroed(Access.Struct->GetStructureSize());
	Access.Struct->InitializeStruct(Fresh.GetData());

	FScriptArrayHelper Helper(Access.Array, Access.ArrayPtr);
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		void* Element = Helper.GetRawPtr(Index);
		const FString Head = FString::Printf(TEXT("%s -> %s"),
			*Access.KeyOf(Element).GetFName().ToString(),
			*InputActionText(Access.ActionOf(Element), Actions));
		Lines.Add(Head);

		NodeScribeInstanced::Read(Access.Triggers, Access.Triggers->ContainerPtrToValuePtr<void>(Element),
			1, TEXT("trigger"), Lines, Unreadable);
		NodeScribeInstanced::Read(Access.Modifiers, Access.Modifiers->ContainerPtrToValuePtr<void>(Element),
			1, TEXT("modifier"), Lines, Unreadable);

		for (TFieldIterator<FProperty> It(Access.Struct); It; ++It)
		{
			const FProperty* Member = *It;
			if (Member == Access.Action || Member == Access.Key || Member == Access.Triggers
				|| Member == Access.Modifiers || Member->HasAnyPropertyFlags(CPF_Transient)
				|| !Member->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}
			if (!Member->Identical_InContainer(Element, Fresh.GetData()))
			{
				NotShown.Add(Head + TEXT(" ") + Member->GetName());
			}
		}
	}

	Access.Struct->DestroyStruct(Fresh.GetData());

	if (Unreadable.Num() > 0)
	{
		Lines.Add(TEXT("# [warning]: I do not know how to write the value of: ")
			+ FString::Join(Unreadable, TEXT(", ")));
	}
	if (NotShown.Num() > 0)
	{
		Lines.Add(TEXT("# [warning]: changed, and the section does not say it: ")
			+ FString::Join(NotShown, TEXT(", ")));
	}

	return FString::Join(Lines, TEXT("\n"));
}

bool SplitText(const FString& Text, FString& OutSheet, FString& OutMappings)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	int32 Section = INDEX_NONE;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (MappingIndent(Lines[Index]) == 0
			&& IsMappingsSectionLine(FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd()))
		{
			Section = Index;
			break;
		}
	}

	if (Section == INDEX_NONE)
	{
		OutSheet = Text;
		OutMappings.Empty();
		return false;
	}

	// Emptied, not cut, so the line numbers in the diagnostics stay the text's.
	TArray<FString> Sheet = Lines;
	TArray<FString> Mappings = Lines;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		(Index < Section ? Mappings : Sheet)[Index].Empty();
	}
	Mappings[Section].Empty();

	OutSheet = FString::Join(Sheet, TEXT("\n"));
	OutMappings = FString::Join(Mappings, TEXT("\n"));
	return true;
}

FResult Write(UObject* Context, const FString& Text)
{
	FResult Result;

	const FMappingAccess Access = FindMappingAccess(Context);
	if (!Access.IsValid())
	{
		Result.Diagnostics.Add(TEXT("[error]: could not find this Input Mapping Context's mappings."));
		return Result;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	// Parsed whole first: a trigger line belongs to the mapping above it, and
	// that is only known once the mapping's block has ended.
	TArray<FMappingText> Mappings;
	NodeScribeInstanced::FElementText* OpenElement = nullptr;

	// The mapping the indented lines belong to. NONE after a mapping line that
	// failed: its triggers must not fall onto the mapping before it.
	int32 Current = INDEX_NONE;
	bool bAfterFailure = false;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("sheet ")) || Line.StartsWith(TEXT("~")))
		{
			continue;
		}

		const int32 Indent = MappingIndent(Lines[Index]);

		if (Indent == 0)
		{
			OpenElement = nullptr;
			Current = INDEX_NONE;
			bAfterFailure = false;

			FMappingText& Mapping = Mappings.AddDefaulted_GetRef();
			Mapping.Line = LineNumber;

			FString Body = Line;
			if (Body.StartsWith(TEXT("delete mapping "), ESearchCase::IgnoreCase))
			{
				Mapping.bDelete = true;
				Body = Body.RightChop(15);
			}

			if (!SplitMappingLine(Body, Mapping.Key, Mapping.Action))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: a mapping is `Key -> Action`."), LineNumber));
				Mappings.Pop();
				bAfterFailure = true;
				continue;
			}
			Current = Mappings.Num() - 1;
			continue;
		}

		if (Current == INDEX_NONE)
		{
			if (!bAfterFailure)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: indented, with no `Key -> Action` above it."), LineNumber));
			}
			continue;
		}

		FMappingText& Mapping = Mappings[Current];

		if (Indent == 1)
		{
			Mapping.bHasChildren = true;

			const bool bTrigger = Line.StartsWith(TEXT("trigger "), ESearchCase::IgnoreCase);
			const bool bModifier = Line.StartsWith(TEXT("modifier "), ESearchCase::IgnoreCase);
			if (!bTrigger && !bModifier)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: under a mapping go `trigger X` and `modifier X`."), LineNumber));
				OpenElement = nullptr;
				continue;
			}

			OpenElement = &(bTrigger ? Mapping.Triggers : Mapping.Modifiers).AddDefaulted_GetRef();
			OpenElement->Class = Line.RightChop(bTrigger ? 8 : 9).TrimStartAndEnd();
			continue;
		}

		if (!OpenElement)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("line %d [error]: a property line with no `trigger` or `modifier` above it."), LineNumber));
			continue;
		}
		OpenElement->Body += Lines[Index].RightChop(FMath::Min(Lines[Index].Len(), 4)) + TEXT("\n");
	}

	const TArray<FAssetData> Actions = FindInputActions();

	Context->Modify();
	FScriptArrayHelper Helper(Access.Array, Access.ArrayPtr);

	// Two mappings can be the same key and action; each line claims the first
	// one not claimed yet, so a reading pasted back lines up one to one.
	TArray<bool> Claimed;
	Claimed.Init(false, Helper.Num());

	for (const FMappingText& Mapping : Mappings)
	{
		FKey Key;
		FString Error;
		UObject* Action = nullptr;
		if (!ResolveKey(Mapping.Key, Key, Error)
			|| (Action = ResolveInputAction(Mapping.Action, Actions, Error)) == nullptr)
		{
			Result.Diagnostics.Add(FString::Printf(TEXT("line %d [error]: %s"), Mapping.Line, *Error));
			continue;
		}

		int32 Found = INDEX_NONE;
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			void* Element = Helper.GetRawPtr(Index);
			if (!Claimed[Index] && Access.KeyOf(Element) == Key && Access.ActionOf(Element) == Action)
			{
				Found = Index;
				break;
			}
		}

		if (Mapping.bDelete)
		{
			if (Found == INDEX_NONE)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("line %d [error]: there is no mapping `%s -> %s` to delete."),
					Mapping.Line, *Mapping.Key, *Mapping.Action));
				continue;
			}
			Helper.RemoveValues(Found);
			Claimed.RemoveAt(Found);
			++Result.Applied;
			continue;
		}

		if (Found == INDEX_NONE)
		{
			Found = Helper.AddValue();
			Claimed.Add(false);

			void* Element = Helper.GetRawPtr(Found);
			Access.KeyOf(Element) = Key;
			Access.Action->SetObjectPropertyValue(Access.Action->ContainerPtrToValuePtr<void>(Element), Action);
			++Result.Applied;
		}
		Claimed[Found] = true;

		if (!Mapping.bHasChildren)
		{
			continue;
		}

		void* Element = Helper.GetRawPtr(Found);
		const FString Where = FString::Printf(TEXT("line %d, %s -> %s"), Mapping.Line, *Mapping.Key, *Mapping.Action);

		// The triggers and the modifiers are one set: a trigger that does not
		// resolve must not leave the modifiers replaced on their own.
		auto Resolves = [&](const FArrayProperty* Array, const TArray<NodeScribeInstanced::FElementText>& Written)
		{
			const UClass* Base = CastField<FObjectPropertyBase>(Array->Inner)->PropertyClass;
			bool bAll = true;
			for (const NodeScribeInstanced::FElementText& Element : Written)
			{
				FString ClassError;
				if (!Element.Class.Equals(TEXT("None"), ESearchCase::IgnoreCase)
					&& !NodeScribeInstanced::FindClass(Base, Element.Class, ClassError))
				{
					Result.Diagnostics.Add(FString::Printf(TEXT("%s [error]: %s"), *Where, *ClassError));
					bAll = false;
				}
			}
			return bAll;
		};
		const bool bTriggersResolve = Resolves(Access.Triggers, Mapping.Triggers);
		const bool bModifiersResolve = Resolves(Access.Modifiers, Mapping.Modifiers);
		if (!bTriggersResolve || !bModifiersResolve)
		{
			Result.Diagnostics.Add(FString::Printf(TEXT("%s [error]: its triggers and modifiers were not changed."), *Where));
			continue;
		}

		NodeScribeInstanced::Write(Context, Access.Triggers, Access.Triggers->ContainerPtrToValuePtr<void>(Element),
			Mapping.Triggers, Where + TEXT(" triggers"), Result.Applied, Result.Diagnostics);
		NodeScribeInstanced::Write(Context, Access.Modifiers, Access.Modifiers->ContainerPtrToValuePtr<void>(Element),
			Mapping.Modifiers, Where + TEXT(" modifiers"), Result.Applied, Result.Diagnostics);
	}

	if (Result.Applied > 0)
	{
		Context->PostEditChange();
		Context->MarkPackageDirty();
	}

	return Result;
}

}
