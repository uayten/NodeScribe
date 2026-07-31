#include "NodeScribeAIWriter.h"

#include "NodeScribeCatalog.h"
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
	 * A classe de tipo de chave pelo nome curto: `Object` ->
	 * `UBlackboardKeyType_Object`.
	 *
	 * Varre as classes em vez de manter uma tabela, pelo mesmo motivo do leitor:
	 * qualquer projeto pode escrever o seu tipo de chave, e uma tabela fixa
	 * responderia "nao conheco" para o tipo que o proprio projeto criou.
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
	 * Poe o detalhe que qualifica a chave -- classe base de Object, enum de
	 * Enum -- por reflexao, espelhando o leitor.
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

			// O leitor escreve o nome curto (`Actor`), que nao carrega caminho.
			// Aqui vale procurar a classe pelo nome antes de desistir.
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
					OutError = FString::Printf(TEXT("nao achei a classe `%s`"), *Detail);
					return false;
				}
			}

			return NodeScribePropertyText::TextToValue(Property, ValuePtr, Text, OutError);
		}

		OutError = FString::Printf(
			TEXT("o tipo %s nao aceita detalhe entre parenteses"), *KeyType->GetClass()->GetName());
		return false;
	}

	FString StripComment(const FString& Line)
	{
		int32 Hash = INDEX_NONE;
		return Line.FindChar(TEXT('#'), Hash) ? Line.Left(Hash) : Line;
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
	Result.Diagnostics.Add(TEXT("[erro]: nao sei escrever neste tipo de asset."));
	return Result;
}

FNodeScribeAIWriter::FResult FNodeScribeAIWriter::WriteBlackboard(
	UBlackboardData* Blackboard, const FString& Text)
{
	FResult Result;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	const FScopedTransaction Transaction(
		LOCTEXT("WriteBlackboardTransaction", "NodeScribe: escrever blackboard"));

	bool bChanged = false;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		FString Line = StripComment(Lines[Index]).TrimStartAndEnd();

		if (Line.IsEmpty() || Line.StartsWith(TEXT("blackboard "), ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!Line.RemoveFromStart(TEXT("chave "), ESearchCase::IgnoreCase))
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("linha %d [erro]: esperava `chave Nome : Tipo`, veio `%s`."),
				LineNumber, *Line));
			continue;
		}

		// Sincronizada e' comportamento, e sai do fim da linha antes do resto.
		bool bInstanceSynced = false;
		if (Line.EndsWith(TEXT(" sincronizada"), ESearchCase::IgnoreCase)
			|| Line.EndsWith(TEXT(" synced"), ESearchCase::IgnoreCase))
		{
			bInstanceSynced = true;
			Line = Line.Left(Line.Find(TEXT(" "), ESearchCase::CaseSensitive,
				ESearchDir::FromEnd)).TrimEnd();
		}

		int32 Colon = INDEX_NONE;
		if (!Line.FindChar(TEXT(':'), Colon))
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("linha %d [erro]: falta o `:` entre o nome e o tipo."), LineNumber));
			continue;
		}

		const FString KeyName = Line.Left(Colon).TrimEnd();
		FString TypeText = Line.RightChop(Colon + 1).TrimStart();

		// `Object (Actor)` -- o detalhe entre parenteses qualifica o tipo.
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
				TEXT("linha %d [erro]: nome ou tipo vazio."), LineNumber));
			continue;
		}

		TArray<FString> Known;
		UClass* KeyTypeClass = FindKeyTypeClass(TypeText, Known);
		if (!KeyTypeClass)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("linha %d [erro]: nao conheco o tipo `%s`. Conheco: %s."),
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

		// Trocar o tipo troca o subobjeto. Reaproveitar o antigo deixaria as
		// propriedades da chave anterior penduradas na nova.
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
					TEXT("linha %d [aviso]: chave `%s` criada, mas %s."),
					LineNumber, *KeyName, *Error));
			}
		}

		bChanged = true;
		++Result.Applied;
	}

	if (bChanged)
	{
		// Sem isto o asset fica alterado em memoria e limpo em disco: fecha o
		// editor e o trabalho some sem ninguem avisar.
		Blackboard->MarkPackageDirty();
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
