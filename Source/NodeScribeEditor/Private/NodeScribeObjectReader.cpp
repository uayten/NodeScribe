#include "NodeScribeObjectReader.h"

#include "NodeScribeAIReader.h"
#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
	using namespace NodeScribePropertyText;

	/** Uma linha de propriedade antes de virar texto alinhado. */
	struct FEntry
	{
		FString Left;      // `Max Walk Speed = 250`, ou com o tipo no modo filtrado
		FString Default;   // valor de fabrica, vazio quando nao mudou ou nao ha'
		int32 Depth = 0;   // membro de struct entra recuado sob ela
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
	 * Como chamar o alvo no cabecalho.
	 *
	 * O que o usuario reconhece e' o nome do asset -- `BT_Golem`, nao
	 * `BehaviorTree`. Quando o alvo veio como CDO de um Blueprint, o CDO se
	 * chama `Default__BP_Golem_C`, entao vale o nome do que foi pedido.
	 */
	FString DescribeTargetName(const UObject* Requested, const UObject* Target)
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Requested))
		{
			return Blueprint->GetName();
		}

		if (const UClass* Class = Cast<UClass>(Requested))
		{
			return CleanClassName(Class);
		}

		return Target->GetName();
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
	const void* FindDefaultValuePtr(const FProperty* Property, const UObject* Archetype)
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
		// Teto na coluna. Sem ele, uma linha larga empurra o comentario de todas
		// as outras para a mesma distancia -- e uma struct de colisao produz
		// linhas de milhares de caracteres, entao as vizinhas ganhavam milhares
		// de espacos. Alinhamento e' para ler; passou disso, atrapalha e custa.
		const int32 MaxColumn = 64;

		auto Indented = [](const FEntry& Entry)
		{
			return FString::ChrN(Entry.Depth * 2, TEXT(' ')) + Entry.Left;
		};

		int32 Widest = 0;
		for (const FEntry& Entry : Entries)
		{
			const int32 Length = Indented(Entry).Len();
			if (!Entry.Default.IsEmpty() && Length <= MaxColumn)
			{
				Widest = FMath::Max(Widest, Length);
			}
		}

		for (const FEntry& Entry : Entries)
		{
			const FString Left = Indented(Entry);

			if (Entry.Default.IsEmpty())
			{
				OutLines.Add(Left);
				continue;
			}

			const int32 Padding = FMath::Max(1, Widest - Left.Len() + 1);
			OutLines.Add(Left + FString::ChrN(Padding, TEXT(' '))
				+ TEXT("# padrao ") + Entry.Default);
		}
	}
}

namespace
{
	/** O que uma passada de propriedades produziu, alem das linhas. */
	struct FCollectStats
	{
		int32 AtDefault = 0;
		int32 Considered = 0;
		TArray<FString> Unreadable;
		TArray<FString> TooLong;
	};

	/**
	 * Acima disto o valor deixa de informar e passa a esconder.
	 *
	 * `Body Instance` de uma capsula sai com a tabela inteira de resposta de
	 * colisao: ~2.000 caracteres, mais outros ~2.000 do valor de fabrica ao
	 * lado. Sozinho, era maior que toda a ficha do BP_Golem -- exatamente o
	 * despejo que este formato existe para nao fazer.
	 *
	 * O corte vale so' na visao geral. Quem filtra pelo nome da propriedade
	 * esta' pedindo aquilo, e ai' sai inteiro.
	 */
	const int32 MaxSurveyValueLength = 160;

	/** Ate' onde abrir struct dentro de struct antes de desistir e so' avisar. */
	const int32 MaxStructDepth = 3;

	/**
	 * Abre uma struct e emite so' os membros que mudaram.
	 *
	 * E' o mesmo principio da ficha, um nivel abaixo: `Body Instance` difere do
	 * padrao em tres campos, nao nos cinquenta que a forma plana despeja.
	 *
	 * So' e' chamada quando a forma plana estourou o teto. Abrir sempre custaria
	 * legibilidade nas structs pequenas -- `Relative Location` vale mais como uma
	 * linha do que como tres, e o `Z` sozinho perderia a companhia do `X` e do
	 * `Y` que dizem que aquilo e' uma posicao.
	 *
	 * @return quantos membros entraram. Zero significa que abrir nao ajudou, e
	 *         quem chamou desfaz.
	 */
	int32 CollectStructMembers(const FStructProperty* StructProperty,
		const void* ValuePtr, const void* DefaultPtr, int32 Depth,
		const FString& Context, TArray<FEntry>& OutEntries, FCollectStats& Stats)
	{
		int32 Added = 0;

		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* Member = *It;
			if (!IsVisible(Member))
			{
				continue;
			}

			const void* MemberValue = Member->ContainerPtrToValuePtr<void>(ValuePtr);
			const void* MemberDefault = DefaultPtr
				? Member->ContainerPtrToValuePtr<void>(DefaultPtr) : nullptr;

			if (!DiffersFromDefault(Member, MemberValue, MemberDefault))
			{
				continue;
			}

			const FString Name = DisplayName(Member);
			const FString Qualified = Context + TEXT(".") + Name;

			FString Value;
			if (!ValueToText(Member, MemberValue, Value))
			{
				Stats.Unreadable.Add(Qualified);
				continue;
			}

			if (Value.Len() > MaxSurveyValueLength)
			{
				const FStructProperty* Inner = CastField<FStructProperty>(Member);

				if (Inner && Depth < MaxStructDepth)
				{
					const int32 Before = OutEntries.Num();
					const int32 TooLongBefore = Stats.TooLong.Num();
					const int32 UnreadableBefore = Stats.Unreadable.Num();

					FEntry Head;
					Head.Left = Name + TEXT(":");
					Head.Depth = Depth;
					OutEntries.Add(Head);

					const int32 InnerAdded = CollectStructMembers(Inner, MemberValue,
						MemberDefault, Depth + 1, Qualified, OutEntries, Stats);

					if (InnerAdded > 0)
					{
						Added += InnerAdded;
						continue;
					}

					// Abrir nao ajudou: desfaz tudo, inclusive o que a tentativa
					// anotou. Sem isto a nota nomeia o membro de dentro e o de
					// fora, que sao a mesma coisa dita duas vezes.
					OutEntries.SetNum(Before);
					Stats.TooLong.SetNum(TooLongBefore);
					Stats.Unreadable.SetNum(UnreadableBefore);
				}

				Stats.TooLong.Add(Qualified);
				continue;
			}

			FEntry Entry;
			Entry.Left = FString::Printf(TEXT("%s = %s"), *Name, *Value);
			Entry.Depth = Depth;

			FString DefaultText;
			if (MemberDefault && ValueToText(Member, MemberDefault, DefaultText)
				&& DefaultText.Len() <= MaxSurveyValueLength)
			{
				Entry.Default = DefaultText;
			}

			OutEntries.Add(MoveTemp(Entry));
			++Added;
		}

		return Added;
	}

	/**
	 * Percorre as propriedades de um objeto e devolve as linhas.
	 *
	 * Uma funcao so' para objeto, componente e variavel: as tres perguntam a
	 * mesma coisa -- o que aqui foge do padrao -- e responder diferente em cada
	 * uma seria tres formatos para o leitor aprender.
	 */
	void CollectEntries(const UObject* Target, const UObject* Archetype,
		const FString& NormalizedFilter, const TSet<FName>& Skip,
		const FString& Context, TArray<FEntry>& OutEntries, FCollectStats& Stats)
	{
		const bool bFiltering = !NormalizedFilter.IsEmpty();

		// `Body Instance, Body Instance` num rodape nao diz de quem sao. Toda
		// capsula e todo mesh tem uma, entao sem o componente na frente a nota
		// so' confunde.
		auto Qualify = [&Context](const FString& Name)
		{
			return Context.IsEmpty() ? Name : Context + TEXT(".") + Name;
		};

		for (TFieldIterator<FProperty> It(Target->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsVisible(Property) || Skip.Contains(Property->GetFName()))
			{
				continue;
			}

			++Stats.Considered;

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
				++Stats.AtDefault;
				continue;
			}

			FString Value;
			if (!ValueToText(Property, ValuePtr, Value))
			{
				// Nao inventar uma linha que parece certa e volta diferente. Some da
				// ficha e aparece no aviso, com nome, para a ausencia ser visivel.
				Stats.Unreadable.Add(Qualify(Name));
				continue;
			}

			if (!bFiltering && Value.Len() > MaxSurveyValueLength)
			{
				// Longa demais plana: tenta abrir e mostrar so' o que mudou.
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					const int32 Before = OutEntries.Num();
					const int32 TooLongBefore = Stats.TooLong.Num();
					const int32 UnreadableBefore = Stats.Unreadable.Num();

					FEntry Head;
					Head.Left = Name + TEXT(":");
					OutEntries.Add(Head);

					if (CollectStructMembers(StructProperty, ValuePtr, DefaultPtr, 1,
						Qualify(Name), OutEntries, Stats) > 0)
					{
						continue;
					}

					OutEntries.SetNum(Before);
					Stats.TooLong.SetNum(TooLongBefore);
					Stats.Unreadable.SetNum(UnreadableBefore);
				}

				// Nomeado, nao sumido: quem le' fica sabendo que aquilo mudou e
				// que da' para pedir. Some em silencio seria mentir por omissao.
				Stats.TooLong.Add(Qualify(Name));
				continue;
			}

			FEntry Entry;
			Entry.Left = bFiltering
				? FString::Printf(TEXT("%s : %s = %s"), *Name, *DescribeType(Property), *Value)
				: FString::Printf(TEXT("%s = %s"), *Name, *Value);

			// O valor de fabrica so' entra onde houve mudanca. Nas outras linhas ele
			// seria uma repeticao do que ja' esta' escrito.
			FString DefaultText;
			if (bChanged && DefaultPtr && ValueToText(Property, DefaultPtr, DefaultText)
				&& DefaultText.Len() <= MaxSurveyValueLength)
			{
				Entry.Default = DefaultText;
			}

			OutEntries.Add(MoveTemp(Entry));
		}
	}

	/** O Blueprint por tras do alvo, quando ha' um. */
	UBlueprint* FindBlueprint(const UObject* Requested, const UClass* Class)
	{
		if (UBlueprint* Direct = const_cast<UBlueprint*>(Cast<UBlueprint>(Requested)))
		{
			return Direct;
		}
		return Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
	}

	/** Os nomes das variaveis que o proprio Blueprint declara. */
	TSet<FName> CollectOwnVariableNames(const UBlueprint* Blueprint)
	{
		TSet<FName> Names;
		if (Blueprint)
		{
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				Names.Add(Variable.VarName);
			}
		}
		return Names;
	}

	/**
	 * Os componentes do alvo, por nome.
	 *
	 * Duas origens, porque um Blueprint guarda em dois lugares: o que veio do
	 * construtor em C++ vive no proprio CDO, e o que foi arrastado no editor
	 * vive como template no SimpleConstructionScript. Ler so' um deles esconde
	 * metade dos componentes sem avisar.
	 */
	TMap<FString, UObject*> CollectComponents(UObject* Target, UBlueprint* Blueprint)
	{
		TMap<FString, UObject*> Components;

		if (const AActor* Actor = Cast<AActor>(Target))
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component)
				{
					Components.Add(Component->GetName(), Component);
				}
			}
		}

		// Sobe a cadeia: componente que o Blueprint pai criou tambem e' do filho.
		for (const UBlueprint* Current = Blueprint; Current; )
		{
			if (const USimpleConstructionScript* SCS = Current->SimpleConstructionScript)
			{
				for (const USCS_Node* Node : SCS->GetAllNodes())
				{
					if (Node && Node->ComponentTemplate)
					{
						Components.Add(Node->GetVariableName().ToString(), Node->ComponentTemplate);
					}
				}
			}

			const UClass* ParentClass = Current->ParentClass;
			Current = ParentClass ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
		}

		return Components;
	}
}

FString FNodeScribeObjectReader::ReadObject(UObject* Object, const FString& Filter)
{
	if (!Object)
	{
		return TEXT("[erro]: nenhum objeto informado.");
	}

	// Asset de IA tem forma propria: a informacao dele nao esta nas propriedades
	// do objeto de cima, e sim na estrutura pendurada nele. Uma ficha generica
	// de blackboard diz `Keys` e mais nada.
	//
	// Fica na mesma ferramenta de proposito: duas portas parecidas fazem quem
	// chama escolher errado e gastar um turno descobrindo isso.
	if (FNodeScribeAIReader::Handles(Object))
	{
		return FNodeScribeAIReader::ReadAsset(Object);
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
	const FString NormalizedFilter = bFiltering
		? FNodeScribeCatalog::Normalize(Filter) : FString();

	UBlueprint* Blueprint = FindBlueprint(Object, Class);
	const TSet<FName> OwnVariables = CollectOwnVariableNames(Blueprint);

	FCollectStats Stats;

	// As variaveis do proprio Blueprint saem em bloco proprio, com `variavel` e
	// o tipo, entao ficam de fora da passada comum.
	TArray<FEntry> Entries;
	CollectEntries(Target, Archetype, NormalizedFilter, OwnVariables,
		FString(), Entries, Stats);

	TArray<FEntry> VariableEntries;
	for (const FBPVariableDescription& Variable : Blueprint
		? Blueprint->NewVariables : TArray<FBPVariableDescription>())
	{
		const FProperty* Property = Class->FindPropertyByName(Variable.VarName);
		if (!Property || !IsVisible(Property))
		{
			continue;
		}

		++Stats.Considered;

		const FString Name = DisplayName(Property);
		if (bFiltering && !FNodeScribeCatalog::Normalize(Name).Contains(NormalizedFilter))
		{
			continue;
		}

		// Variavel declarada aqui aparece sempre, mesmo no valor de fabrica: ela
		// nao existe na classe pai, entao a existencia dela ja' e' a informacao.
		FString Value;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
		const bool bHasValue = ValueToText(Property, ValuePtr, Value);

		FEntry Entry;
		Entry.Left = FString::Printf(TEXT("variavel %s : %s"), *Name, *DescribeType(Property));

		// Instance Editable e' o que faz a variavel aparecer no painel de quem
		// usa o Blueprint -- e' assim que uma BTTask ganha parametro por node.
		// Sem sair aqui, uma ida e volta apagaria a marcacao calada.
		if (Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
		{
			Entry.Left += TEXT(" editavel");
		}

		if (bHasValue && Value != TEXT("None") && Value != TEXT("0") && Value != TEXT("false"))
		{
			Entry.Left += TEXT(" = ") + Value;
		}

		VariableEntries.Add(MoveTemp(Entry));
	}

	TArray<FString> Lines;

	// O nome do asset, e entre parenteses a classe -- `ficha BT_Golem
	// (BehaviorTree)`. Em Blueprint os dois sao a mesma palavra, e repetir nao
	// diz nada; ali vale a classe pai, que e' a informacao que falta:
	// `ficha BP_Golem (Character)`.
	const FString TargetName = DescribeTargetName(Object, Target);
	FString ClassName = CleanClassName(Class);
	if (ClassName == TargetName && Class->GetSuperClass())
	{
		ClassName = CleanClassName(Class->GetSuperClass());
	}

	Lines.Add(FString::Printf(TEXT("ficha %s (%s)"), *TargetName, *ClassName));

	const FString Ancestry = DescribeAncestry(Class);
	if (!Ancestry.IsEmpty())
	{
		Lines.Add(TEXT("# herda: ") + Ancestry);
	}

	// Os grafos, pelo nome com que `read_graph` os encontra.
	//
	// Sem esta linha o nome do grafo e' adivinhacao: `EventGraph` funciona quase
	// sempre e falha sem dizer por que, e nao havia como descobrir o certo --
	// ficava-se tentando nomes ate' acertar, ou desistindo do asset.
	if (Blueprint && !bFiltering)
	{
		TArray<FString> GraphNames;

		auto Collect = [&GraphNames](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (const UEdGraph* Graph : Graphs)
			{
				if (Graph)
				{
					GraphNames.Add(Graph->GetName());
				}
			}
		};

		Collect(Blueprint->UbergraphPages);
		Collect(Blueprint->FunctionGraphs);
		Collect(Blueprint->MacroGraphs);

		if (GraphNames.Num() > 0)
		{
			Lines.Add(TEXT("# grafos: ") + FString::Join(GraphNames, TEXT(", ")));
		}
	}

	const int32 HeaderIndex = 0;

	AppendAligned(Lines, VariableEntries);
	AppendAligned(Lines, Entries);

	int32 Shown = VariableEntries.Num() + Entries.Num();

	// Componente e' onde mora metade do que se pergunta de um ator: `Max Walk
	// Speed` nao esta' no Character, esta' no CharacterMovement dele.
	for (const TPair<FString, UObject*>& Pair : CollectComponents(Target, Blueprint))
	{
		UObject* Component = Pair.Value;

		TArray<FEntry> ComponentEntries;
		CollectEntries(Component, Component->GetArchetype(),
			NormalizedFilter, TSet<FName>(), Pair.Key, ComponentEntries, Stats);

		if (ComponentEntries.Num() == 0)
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("%s : %s"),
			*Pair.Key, *CleanClassName(Component->GetClass())));

		TArray<FString> ComponentLines;
		AppendAligned(ComponentLines, ComponentEntries);
		for (const FString& Line : ComponentLines)
		{
			Lines.Add(TEXT("  ") + Line);
		}

		Shown += ComponentEntries.Num();
	}

	if (bFiltering)
	{
		Lines[HeaderIndex] += FString::Printf(TEXT("  ~ \"%s\" (%d de %d)"),
			*Filter, Shown, Stats.Considered);
	}
	else
	{
		// O silencio precisa ser explicito: sem esta linha, "nao apareceu" fica
		// ambiguo entre estar no padrao e o plugin nao saber ler.
		Lines.Add(FString::Printf(TEXT("~ %d propriedades no padrao"), Stats.AtDefault));
	}

	if (Stats.TooLong.Num() > 0)
	{
		Lines.Add(FString::Printf(
			TEXT("# [nota]: mudou, mas o valor e' longo demais para a visao geral -- ")
			TEXT("peca pelo nome para ver: %s"),
			*FString::Join(Stats.TooLong, TEXT(", "))));
	}

	if (Stats.Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [aviso]: nao sei escrever o valor de: %s"),
			*FString::Join(Stats.Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}
