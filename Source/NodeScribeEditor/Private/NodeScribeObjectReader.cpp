#include "NodeScribeObjectReader.h"

#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "Engine/Blueprint.h"
#include "UObject/UnrealType.h"

namespace
{
	using namespace NodeScribePropertyText;

	/** Uma linha de propriedade antes de virar texto alinhado. */
	struct FEntry
	{
		FString Left;      // `Max Walk Speed = 250`, ou com o tipo no modo filtrado
		FString Default;   // valor de fabrica, vazio quando nao mudou ou nao ha'
	};

	/**
	 * O objeto que carrega os valores.
	 *
	 * Blueprint e classe nao tem valor nenhum em si: quem guarda e' o CDO da
	 * classe compilada. E' de la' que o painel de detalhes le', entao e' de la'
	 * que a ficha tem que ler para dizer a mesma coisa que a tela.
	 */
	UObject* ResolveTarget(UObject* Object)
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			UClass* Generated = Blueprint->GeneratedClass.Get();
			return Generated ? Generated->GetDefaultObject() : nullptr;
		}

		if (UClass* Class = Cast<UClass>(Object))
		{
			return Class->GetDefaultObject();
		}

		return Object;
	}

	/** `BP_Golem_C` -> `BP_Golem`. O sufixo e' da compilacao, nao do nome. */
	FString CleanClassName(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}

	/**
	 * A cadeia de heranca ate' Object, sem ele.
	 *
	 * Todo mundo herda de Object; dizer isso em toda ficha e' uma linha que
	 * nunca informa nada.
	 */
	FString DescribeAncestry(const UClass* Class)
	{
		TArray<FString> Names;

		for (const UClass* Super = Class->GetSuperClass(); Super; Super = Super->GetSuperClass())
		{
			if (Super == UObject::StaticClass())
			{
				break;
			}
			Names.Add(CleanClassName(Super));
		}

		return FString::Join(Names, TEXT(" < "));
	}

	/**
	 * O valor de fabrica desta propriedade, se houver com o que comparar.
	 *
	 * O arquetipo pode ser de uma classe que nem conhece a propriedade -- e' o
	 * caso das variaveis que o proprio Blueprint declara, que nao existem no
	 * pai. Ler o mesmo deslocamento la' seria ler memoria de outra coisa.
	 */
	const void* FindDefaultValuePtr(const FProperty* Property, UObject* Archetype)
	{
		if (!Archetype)
		{
			return nullptr;
		}

		const FProperty* DefaultProperty =
			Archetype->GetClass()->FindPropertyByName(Property->GetFName());

		if (!DefaultProperty || !DefaultProperty->SameType(Property))
		{
			return nullptr;
		}

		return DefaultProperty->ContainerPtrToValuePtr<void>(Archetype);
	}

	/**
	 * Alinha os comentarios numa coluna so'.
	 *
	 * O ponto da ficha e' voce bater o olho e ver o que mudou; com os `# padrao`
	 * em posicoes diferentes, a coluna que importa fica serrilhada e some.
	 */
	void AppendAligned(TArray<FString>& OutLines, const TArray<FEntry>& Entries)
	{
		int32 Widest = 0;
		for (const FEntry& Entry : Entries)
		{
			if (!Entry.Default.IsEmpty())
			{
				Widest = FMath::Max(Widest, Entry.Left.Len());
			}
		}

		for (const FEntry& Entry : Entries)
		{
			if (Entry.Default.IsEmpty())
			{
				OutLines.Add(Entry.Left);
				continue;
			}

			const int32 Padding = FMath::Max(1, Widest - Entry.Left.Len() + 1);
			OutLines.Add(Entry.Left + FString::ChrN(Padding, TEXT(' '))
				+ TEXT("# padrao ") + Entry.Default);
		}
	}
}

FString FNodeScribeObjectReader::ReadObject(UObject* Object, const FString& Filter)
{
	if (!Object)
	{
		return TEXT("[erro]: nenhum objeto informado.");
	}

	UObject* Target = ResolveTarget(Object);
	if (!Target)
	{
		return FString::Printf(
			TEXT("[erro]: `%s` nao tem classe compilada -- compile o Blueprint antes."),
			*Object->GetName());
	}

	UClass* Class = Target->GetClass();
	UObject* Archetype = Target->GetArchetype();

	const bool bFiltering = !Filter.IsEmpty();
	const FString NormalizedFilter = FNodeScribeCatalog::Normalize(Filter);

	TArray<FEntry> Entries;
	TArray<FString> Unreadable;
	int32 AtDefault = 0;
	int32 Considered = 0;

	for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (!IsVisible(Property))
		{
			continue;
		}

		++Considered;

		const FString Name = DisplayName(Property);

		if (bFiltering && !FNodeScribeCatalog::Normalize(Name).Contains(NormalizedFilter))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
		const void* DefaultPtr = FindDefaultValuePtr(Property, Archetype);
		const bool bChanged = DiffersFromDefault(Property, ValuePtr, DefaultPtr);

		if (!bFiltering && !bChanged)
		{
			++AtDefault;
			continue;
		}

		FString Value;
		if (!ValueToText(Property, ValuePtr, Value))
		{
			// Nao inventar uma linha que parece certa e volta diferente. Some da
			// ficha e aparece no aviso, com nome, para a ausencia ser visivel.
			Unreadable.Add(Name);
			continue;
		}

		FEntry Entry;
		Entry.Left = bFiltering
			? FString::Printf(TEXT("%s : %s = %s"), *Name, *DescribeType(Property), *Value)
			: FString::Printf(TEXT("%s = %s"), *Name, *Value);

		// O valor de fabrica so' entra onde houve mudanca. Nas outras linhas ele
		// seria uma repeticao do que ja' esta' escrito.
		FString DefaultText;
		if (bChanged && DefaultPtr && ValueToText(Property, DefaultPtr, DefaultText))
		{
			Entry.Default = DefaultText;
		}

		Entries.Add(MoveTemp(Entry));
	}

	TArray<FString> Lines;

	Lines.Add(FString::Printf(TEXT("ficha %s (%s)"),
		*CleanClassName(Class), *CleanClassName(Class->GetSuperClass()
			? Class->GetSuperClass() : Class)));

	const FString Ancestry = DescribeAncestry(Class);
	if (!Ancestry.IsEmpty())
	{
		Lines.Add(TEXT("# herda: ") + Ancestry);
	}

	if (bFiltering)
	{
		Lines[0] += FString::Printf(TEXT("  ~ \"%s\" (%d de %d)"),
			*Filter, Entries.Num(), Considered);
	}

	AppendAligned(Lines, Entries);

	if (!bFiltering)
	{
		// O silencio precisa ser explicito: sem esta linha, "nao apareceu" fica
		// ambiguo entre estar no padrao e o plugin nao saber ler.
		Lines.Add(FString::Printf(TEXT("~ %d propriedades no padrao"), AtDefault));
	}

	if (Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [aviso]: nao sei escrever o valor de: %s"),
			*FString::Join(Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}
