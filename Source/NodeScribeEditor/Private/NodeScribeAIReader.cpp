#include "NodeScribeAIReader.h"

#include "NodeScribePropertyText.h"

#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "UObject/UnrealType.h"

namespace
{
	/**
	 * O tipo da chave como aparece na tela: `UBlackboardKeyType_Object` -> `Object`.
	 */
	FString DescribeKeyTypeName(const UBlackboardKeyType* KeyType)
	{
		FString Name = KeyType->GetClass()->GetName();
		Name.RemoveFromStart(TEXT("BlackboardKeyType_"));
		return Name;
	}

	/**
	 * O detalhe que qualifica a chave: a classe base de um Object, o enum de um
	 * Enum. Vazio quando o tipo nao tem detalhe.
	 *
	 * Por reflexao, e nao por cast para cada subclasse conhecida. Sao dez tipos
	 * na Engine e qualquer um pode escrever o seu; um `switch` de casts
	 * responderia vazio para o tipo de chave que o proprio projeto criou, sem
	 * dizer que estava ignorando algo.
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

			// O nome curto basta aqui: e' qualificador de tipo, nao referencia a
			// resolver. `Object (Actor)` se le'; `Object (/Script/Engine.Actor)`
			// enche a linha sem acrescentar.
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
}

bool FNodeScribeAIReader::Handles(const UObject* Object)
{
	return Object && Object->IsA<UBlackboardData>();
}

FString FNodeScribeAIReader::ReadAsset(UObject* Object)
{
	if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Object))
	{
		return ReadBlackboard(Blackboard);
	}

	return FString();
}

FString FNodeScribeAIReader::ReadBlackboard(UBlackboardData* Blackboard)
{
	TArray<FString> Lines;

	FString Header = FString::Printf(TEXT("blackboard %s"), *Blackboard->GetName());
	if (const UBlackboardData* Parent = Blackboard->Parent)
	{
		// Chave herdada nao e' repetida: seriam as mesmas linhas em toda ficha
		// filha, e o pai esta' a uma chamada de distancia.
		Header += FString::Printf(TEXT("  (herda de %s)"), *Parent->GetName());
	}
	Lines.Add(Header);

	TArray<FString> Unreadable;

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (!Entry.KeyType)
		{
			// Chave sem tipo existe: e' a linha recem-criada no editor, ainda
			// sem escolha. Dizer isso e' melhor que omitir.
			Unreadable.Add(Entry.EntryName.ToString());
			continue;
		}

		FString Line = FString::Printf(TEXT("chave %s : %s"),
			*Entry.EntryName.ToString(), *DescribeKeyTypeName(Entry.KeyType));

		const FString Detail = DescribeKeyTypeDetail(Entry.KeyType);
		if (!Detail.IsEmpty())
		{
			Line += FString::Printf(TEXT(" (%s)"), *Detail);
		}

		// Sincronizada entre instancias e' comportamento, nao tipo, e muda o que
		// a IA faz -- some da linha padrao e aparece quando esta' ligada.
		if (Entry.bInstanceSynced)
		{
			Line += TEXT("  # sincronizada entre instancias");
		}

		Lines.Add(Line);
	}

	if (Blackboard->Keys.Num() == 0)
	{
		Lines.Add(TEXT("# sem chaves"));
	}

	if (Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [aviso]: chave sem tipo escolhido: %s"),
			*FString::Join(Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}
