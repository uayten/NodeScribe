#include "NodeScribeInstanced.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeObjectReader.h"
#include "NodeScribeObjectWriter.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FObjectPropertyBase* InstancedInner(const FProperty* Property)
	{
		const FArrayProperty* Array = CastField<FArrayProperty>(Property);
		const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
		return Inner && Inner->HasAnyPropertyFlags(CPF_InstancedReference) ? Inner : nullptr;
	}

	/** `InputTriggerHold` under `InputTrigger` -> `Hold`; `TargetTagsGameplayEffectComponent` -> `TargetTags`. */
	FString NameWithoutBase(const UClass* Class, const UClass* Base)
	{
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		const FString BaseName = Base->GetName();
		if (Name != BaseName && !Name.RemoveFromStart(BaseName))
		{
			Name.RemoveFromEnd(BaseName);
		}
		return Name;
	}

	bool IsUsableInstancedClass(const UClass* Class, const UClass* Base)
	{
		if (!Class->IsChildOf(Base)
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		const FString Name = Class->GetName();
		return !Name.StartsWith(TEXT("SKEL_")) && !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("TRASHCLASS_"));
	}
}

namespace NodeScribeInstanced
{

bool IsInstancedObjectArray(const FProperty* Property)
{
	return InstancedInner(Property) != nullptr;
}

UClass* FindClass(const UClass* Base, const FString& Written, FString& OutError)
{
	if (Written.StartsWith(TEXT("/")))
	{
		UClass* Class = LoadObject<UClass>(nullptr, *Written);
		if (!Class && !Written.EndsWith(TEXT("_C")))
		{
			Class = LoadObject<UClass>(nullptr, *(Written + TEXT("_C")));
		}
		if (!Class || !IsUsableInstancedClass(Class, Base))
		{
			OutError = FString::Printf(TEXT("`%s` is not a %s class."), *Written, *Base->GetName());
			return nullptr;
		}
		return Class;
	}

	const FString Wanted = FNodeScribeCatalog::Normalize(Written);
	TArray<UClass*> Found;
	TArray<FString> Known;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!IsUsableInstancedClass(Class, Base))
		{
			continue;
		}

		const FString Screen = Class->GetDisplayNameText().ToString();
		Known.AddUnique(Screen);

		FString ClassName = Class->GetName();
		ClassName.RemoveFromEnd(TEXT("_C"));

		if (FNodeScribeCatalog::Normalize(Screen) == Wanted
			|| FNodeScribeCatalog::Normalize(ClassName) == Wanted
			|| FNodeScribeCatalog::Normalize(NameWithoutBase(Class, Base)) == Wanted)
		{
			Found.AddUnique(Class);
		}
	}

	if (Found.Num() == 1)
	{
		return Found[0];
	}

	if (Found.Num() > 1)
	{
		TArray<FString> Paths;
		for (const UClass* Class : Found)
		{
			Paths.Add(Class->GetPathName());
		}
		OutError = FString::Printf(TEXT("`%s` is ambiguous; write the path: %s"),
			*Written, *FString::Join(Paths, TEXT(", ")));
		return nullptr;
	}

	Known.Sort();
	OutError = FString::Printf(TEXT("`%s` matches no %s class. Known: %s"),
		*Written, *Base->GetName(), *FString::Join(Known, TEXT(", ")));
	return nullptr;
}

FString ClassText(const UClass* Class, const UClass* Base)
{
	const FString Screen = Class->GetDisplayNameText().ToString();
	FString Ignored;
	return FindClass(Base, Screen, Ignored) == Class ? Screen : Class->GetPathName();
}

void Read(const FArrayProperty* Property, const void* ArrayPtr, int32 Depth,
	const FString& Keyword, TArray<FString>& OutLines, TArray<FString>& OutUnreadable)
{
	const FObjectPropertyBase* Inner = InstancedInner(Property);
	if (!Inner || !ArrayPtr)
	{
		return;
	}

	const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
	const FString Prefix = Keyword.IsEmpty() ? FString() : Keyword + TEXT(" ");

	FScriptArrayHelper Helper(Property, ArrayPtr);
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		const UObject* Element = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index));
		if (!Element)
		{
			OutLines.Add(Indent + Prefix + TEXT("None"));
			continue;
		}

		const FString Text = ClassText(Element->GetClass(), Inner->PropertyClass);
		OutLines.Add(Indent + Prefix + Text);

		// Whole values: a cut value cannot be asked for by name here -- the
		// element has none -- and the block is pasted back as it is.
		TArray<FString> PropertyLines;
		TArray<FString> TooLong;
		TArray<FString> Unreadable;
		FNodeScribeObjectReader::ReadProperties(Element, TSet<FName>(), Text,
			/*bWhole*/ true, PropertyLines, TooLong, Unreadable);

		for (const FString& Line : PropertyLines)
		{
			OutLines.Add(Indent + TEXT("  ") + Line);
		}
		OutUnreadable.Append(Unreadable);
	}
}

bool Write(UObject* Outer, const FArrayProperty* Property, void* ArrayPtr,
	const TArray<FElementText>& Elements, const FString& Context,
	int32& OutApplied, TArray<FString>& OutDiagnostics)
{
	const FObjectPropertyBase* Inner = InstancedInner(Property);
	if (!Inner || !ArrayPtr || !Outer)
	{
		OutDiagnostics.Add(FString::Printf(TEXT("[error]: `%s` is not an array of instanced objects."), *Context));
		return false;
	}

	// Every class first: a block half applied is an array nobody wrote.
	TArray<UClass*> Classes;
	bool bFailed = false;
	for (const FElementText& Element : Elements)
	{
		if (Element.Class.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Classes.Add(nullptr);
			continue;
		}

		FString Error;
		UClass* Class = FindClass(Inner->PropertyClass, Element.Class, Error);
		if (!Class)
		{
			OutDiagnostics.Add(FString::Printf(TEXT("[error]: %s: %s"), *Context, *Error));
			bFailed = true;
		}
		Classes.Add(Class);
	}

	if (bFailed)
	{
		OutDiagnostics.Add(FString::Printf(TEXT("[error]: %s: nothing changed."), *Context));
		return false;
	}

	FScriptArrayHelper Helper(Property, ArrayPtr);

	TArray<UObject*> Existing;
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		Existing.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
	}
	TArray<bool> Used;
	Used.Init(false, Existing.Num());

	Outer->Modify();

	TArray<UObject*> Result;
	for (int32 Index = 0; Index < Elements.Num(); ++Index)
	{
		UClass* Class = Classes[Index];
		if (!Class)
		{
			Result.Add(nullptr);
			continue;
		}

		// The first unused element of the same class is the one being talked
		// about: it keeps what the text does not say.
		UObject* Element = nullptr;
		for (int32 Old = 0; Old < Existing.Num(); ++Old)
		{
			if (!Used[Old] && Existing[Old] && Existing[Old]->GetClass() == Class)
			{
				Used[Old] = true;
				Element = Existing[Old];
				break;
			}
		}

		if (!Element)
		{
			// The flags the Details panel gives a new element: an element of a
			// CDO is an archetype too, or instances would share it.
			Element = NewObject<UObject>(Outer, Class, NAME_None,
				Outer->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
			++OutApplied;
		}

		Result.Add(Element);
	}

	TArray<FString> Removed;
	for (int32 Old = 0; Old < Existing.Num(); ++Old)
	{
		if (!Used[Old] && Existing[Old])
		{
			Removed.Add(ClassText(Existing[Old]->GetClass(), Inner->PropertyClass));
		}
	}

	const bool bReordered = Result.Num() != Existing.Num() || Result != Existing;

	Helper.Resize(Result.Num());
	for (int32 Index = 0; Index < Result.Num(); ++Index)
	{
		Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Result[Index]);
	}

	// The lines under each element only after it is in the array: an element
	// that reacts to its own change asks its owner to recompute, and the owner
	// walks the array -- a Gameplay Effect's tag component, written before it
	// was in `Components`, recomputed nothing.
	for (int32 Index = 0; Index < Result.Num(); ++Index)
	{
		UObject* Element = Result[Index];
		if (!Element || Elements[Index].Body.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		const FNodeScribeObjectWriter::FResult BodyResult =
			FNodeScribeObjectWriter::WriteProperties(Element, Elements[Index].Body);

		OutApplied += BodyResult.Applied;
		for (const FString& Diagnostic : BodyResult.Diagnostics)
		{
			OutDiagnostics.Add(FString::Printf(TEXT("%s, %s: %s"),
				*Context, *Elements[Index].Class, *Diagnostic));
		}
	}

	if (Removed.Num() > 0)
	{
		OutApplied += Removed.Num();
		OutDiagnostics.Add(FString::Printf(TEXT("[note]: %s: removed what the text did not list: %s"),
			*Context, *FString::Join(Removed, TEXT(", "))));
	}
	else if (bReordered)
	{
		++OutApplied;
	}

	return true;
}

}
