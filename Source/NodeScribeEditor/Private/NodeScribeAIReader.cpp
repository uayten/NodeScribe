#include "NodeScribeAIReader.h"

#include "NodeScribePropertyText.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "DataProviders/AIDataProvider.h"
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

	// -- Behavior Tree -------------------------------------------------------

	/** Dois espacos por nivel, como no resto do formato. */
	FString Indent(int32 Level)
	{
		return FString::ChrN(Level * 2, TEXT(' '));
	}

	/**
	 * O seletor de chave de blackboard sai como o nome da chave, so'.
	 *
	 * E' o parametro mais comum de BT, e a forma canonica dele e' um struct com
	 * a lista de tipos aceitos dentro -- um blobzao que enche a linha e esconde
	 * a unica coisa que interessa, que e' qual chave.
	 */
	bool TryDescribeKeySelector(const FProperty* Property, const void* ValuePtr, FString& OutText)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty || StructProperty->Struct != FBlackboardKeySelector::StaticStruct())
		{
			return false;
		}

		const FBlackboardKeySelector* Selector = static_cast<const FBlackboardKeySelector*>(ValuePtr);
		OutText = Selector->SelectedKeyName.ToString();
		return !OutText.IsEmpty() && OutText != TEXT("None");
	}

	/**
	 * Valor de BT que pode vir de um provider: `Wait Time`, `Acceptable Radius`
	 * e companhia.
	 *
	 * A forma canonica desses e' `(DefaultValue=1.000000)`, que enche a linha
	 * escondendo o `1.0`. A propria Engine sabe se descrever -- `ToString()` da'
	 * o numero quando o valor e' fixo e o nome da chave quando esta' amarrado ao
	 * blackboard, que e' exatamente a distincao que interessa ler.
	 *
	 * Sao duas familias porque a 5.8 aposentou a primeira: `FAIDataProviderValue`
	 * ainda existe e nodes antigos usam, mas `Wait Time` agora e'
	 * `FValueOrBBKey_Float`. Testar so' a antiga passa em silencio -- compila,
	 * roda, e devolve o blobzao.
	 */
	bool TryDescribeBTValue(const FProperty* Property, const void* ValuePtr, FString& OutText)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty)
		{
			return false;
		}

		if (StructProperty->Struct->IsChildOf(FValueOrBlackboardKeyBase::StaticStruct()))
		{
			OutText = static_cast<const FValueOrBlackboardKeyBase*>(ValuePtr)->ToString();
			return !OutText.IsEmpty();
		}

		if (StructProperty->Struct->IsChildOf(FAIDataProviderValue::StaticStruct()))
		{
			OutText = static_cast<const FAIDataProviderValue*>(ValuePtr)->ToString();
			return !OutText.IsEmpty();
		}

		return false;
	}

	/**
	 * `BTTask_Patrulhar` -> `Patrulhar`.
	 *
	 * `GetNodeName` ja' resolve os nodes da Engine, mas para classe vinda de
	 * Blueprint ele so' tira o `_C` -- entao a task que o proprio projeto criou
	 * aparecia com o prefixo tecnico, do lado de um `Move To` limpo.
	 */
	FString CleanNodeName(const UBTNode* Node)
	{
		FString Name = Node->GetNodeName();

		for (const TCHAR* Prefix : { TEXT("BTTask_"), TEXT("BTService_"),
			TEXT("BTDecorator_"), TEXT("BTComposite_") })
		{
			if (Name.RemoveFromStart(Prefix))
			{
				break;
			}
		}

		return Name;
	}

	/**
	 * Os parametros do node que fogem do padrao da classe dele, entre
	 * parenteses.
	 *
	 * Mesma pergunta da ficha, mesmo formatador: o CDO da classe do node e' o
	 * arquetipo. Um `Move To` com raio de fabrica nao ganha parametro nenhum.
	 */
	FString DescribeNodeParams(const UBTNode* Node)
	{
		const UClass* Class = Node->GetClass();
		const UObject* Defaults = Class->GetDefaultObject();

		TArray<FString> Params;

		for (TFieldIterator<FProperty> It(Class, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!NodeScribePropertyText::IsVisible(Property))
			{
				continue;
			}

			// O nome do node ja' e' a linha. Repeti-lo como parametro seria dizer
			// a mesma coisa duas vezes.
			if (Property->GetFName() == TEXT("NodeName"))
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
			const void* DefaultPtr = Defaults
				? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr;

			FString Value;
			if (TryDescribeKeySelector(Property, ValuePtr, Value))
			{
				// Seletor de chave e' transiente por dentro, entao a comparacao
				// com o padrao nao vale para ele: sai sempre que tiver chave.
				Params.Add(FString::Printf(TEXT("%s = %s"),
					*NodeScribePropertyText::DisplayName(Property), *Value));
				continue;
			}

			if (!NodeScribePropertyText::DiffersFromDefault(Property, ValuePtr, DefaultPtr))
			{
				continue;
			}

			if (!TryDescribeBTValue(Property, ValuePtr, Value)
				&& !NodeScribePropertyText::ValueToText(Property, ValuePtr, Value))
			{
				continue;
			}

			Params.Add(FString::Printf(TEXT("%s = %s"),
				*NodeScribePropertyText::DisplayName(Property), *Value));
		}

		return Params.Num() > 0
			? FString::Printf(TEXT(" (%s)"), *FString::Join(Params, TEXT(", ")))
			: FString();
	}

	/** Uma linha de node, com os parametros que fogem do padrao. */
	FString NodeLine(int32 Level, const TCHAR* Prefix, const UBTNode* Node)
	{
		return Indent(Level) + Prefix + CleanNodeName(Node) + DescribeNodeParams(Node);
	}

	/**
	 * Percorre a arvore.
	 *
	 * @param Seen  guarda contra asset corrompido. Uma BT nao cicla por
	 *              construcao, mas se ciclar isto para' e diz, em vez de
	 *              escrever ate' faltar memoria.
	 */
	void EmitNode(TArray<FString>& OutLines, const UBTNode* Node, int32 Level,
		TSet<const UBTNode*>& Seen)
	{
		if (!Node)
		{
			return;
		}

		if (Seen.Contains(Node))
		{
			OutLines.Add(Indent(Level) + TEXT("# [aviso]: ciclo em `")
				+ CleanNodeName(Node) + TEXT("` -- parei aqui."));
			return;
		}
		Seen.Add(Node);

		OutLines.Add(NodeLine(Level, TEXT(""), Node));

		// Service e' do node; decorator e' da ligacao com o pai, e por isso sai
		// junto com o filho, la' embaixo. E' assim que o editor mostra tambem.
		const TArray<TObjectPtr<UBTService>>* Services = nullptr;
		if (const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
		{
			Services = &Composite->Services;
		}
		else if (const UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
		{
			Services = &Task->Services;
		}

		if (Services)
		{
			for (const UBTService* Service : *Services)
			{
				if (Service)
				{
					OutLines.Add(NodeLine(Level + 1, TEXT("servico "), Service));
				}
			}
		}

		const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
		if (!Composite)
		{
			return;
		}

		for (const FBTCompositeChild& Child : Composite->Children)
		{
			for (const UBTDecorator* Decorator : Child.Decorators)
			{
				if (Decorator)
				{
					OutLines.Add(NodeLine(Level + 1, TEXT("decorador "), Decorator));
				}
			}

			const UBTNode* ChildNode = Child.ChildComposite
				? static_cast<const UBTNode*>(Child.ChildComposite)
				: static_cast<const UBTNode*>(Child.ChildTask);

			if (!ChildNode)
			{
				// Filho vazio existe: e' o galho que ficou pela metade no editor.
				OutLines.Add(Indent(Level + 1) + TEXT("# [aviso]: galho sem node."));
				continue;
			}

			EmitNode(OutLines, ChildNode, Level + 1, Seen);
		}
	}
}

bool FNodeScribeAIReader::Handles(const UObject* Object)
{
	return Object && (Object->IsA<UBlackboardData>() || Object->IsA<UBehaviorTree>());
}

FString FNodeScribeAIReader::ReadAsset(UObject* Object)
{
	if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Object))
	{
		return ReadBlackboard(Blackboard);
	}

	if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Object))
	{
		return ReadBehaviorTree(Tree);
	}

	return FString();
}

FString FNodeScribeAIReader::ReadBehaviorTree(UBehaviorTree* Tree)
{
	TArray<FString> Lines;

	FString Header = FString::Printf(TEXT("arvore %s"), *Tree->GetName());
	if (const UBlackboardData* Blackboard = Tree->BlackboardAsset)
	{
		// O blackboard vai no cabecalho porque toda chave citada la' embaixo
		// vem dele -- sem isso os nomes de chave sao palavras soltas.
		Header += FString::Printf(TEXT("  (blackboard %s)"), *Blackboard->GetName());
	}
	Lines.Add(Header);

	for (const UBTDecorator* Decorator : Tree->RootDecorators)
	{
		if (Decorator)
		{
			Lines.Add(NodeLine(0, TEXT("decorador "), Decorator));
		}
	}

	if (!Tree->RootNode)
	{
		Lines.Add(TEXT("# arvore vazia"));
		return FString::Join(Lines, TEXT("\n"));
	}

	TSet<const UBTNode*> Seen;
	EmitNode(Lines, Tree->RootNode, 0, Seen);

	return FString::Join(Lines, TEXT("\n"));
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
		//
		// Palavra, nao comentario: o escritor descarta comentario, e uma chave
		// que volta dessincronizada seria um bug que so' aparece com dois
		// inimigos na tela.
		if (Entry.bInstanceSynced)
		{
			Line += TEXT(" sincronizada");
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
