#include "NodeScribeAIWriter.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeParser.h"
#include "NodeScribePropertyText.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
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

bool FNodeScribeAIWriter::Handles(const UObject* Object)
{
	return Object && Object->IsA<UBlackboardData>();
}

FNodeScribeAIWriter::FResult FNodeScribeAIWriter::WriteAsset(UObject* Object, const FString& Text)
{
	if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Object))
	{
		return WriteBlackboard(Blackboard, Text);
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

#undef LOCTEXT_NAMESPACE
