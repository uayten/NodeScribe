#include "NodeScribeReader.h"

#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GenericToText.h"
#include "K2Node_GetEnumeratorName.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

using namespace NodeScribePropertyText;

namespace
{
	/** Dois espacos por nivel: e' o que FNodeScribeParser::MeasureIndent conta. */
	const TCHAR* const IndentUnit = TEXT("  ");

	/**
	 * true para o node que a Unreal insere sozinha ao ligar tipos diferentes.
	 *
	 * Ninguem escolhe esses nodes: eles aparecem no `TryCreateConnection` quando
	 * o pino de origem nao e' do tipo do pino de destino, e o builder recria cada
	 * um deles do mesmo jeito, sozinho, ao refazer a ligacao. Por isso sao
	 * atravessados como reroute em vez de virar linha.
	 *
	 * Duas familias, que e' o que `CreateAutomaticConversionNodeAndConnections`
	 * conhece: a funcao marcada `BlueprintAutocast` (`To Integer64 (Integer)`) e
	 * os nodes especializados de enum (`Enum to String`).
	 */
	bool IsImplicitConversionNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}

		if (Node->IsA<UK2Node_GetEnumeratorName>() || Node->IsA<UK2Node_GetEnumeratorNameAsString>())
		{
			return true;
		}

		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const UFunction* Function = Call->GetTargetFunction();
			return Function && Function->HasMetaData(TEXT("BlueprintAutocast"));
		}

		return false;
	}

	/** O unico pino de dado de entrada de um node de conversao. */
	UEdGraphPin* FindSingleDataInput(UEdGraphNode* Node)
	{
		UEdGraphPin* Found = nullptr;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin) || Pin->bHidden)
			{
				continue;
			}

			if (Found)
			{
				// Mais de uma entrada: nao e' a conversao de um valor so'.
				return nullptr;
			}

			Found = Pin;
		}

		return Found;
	}

	/**
	 * Nodes de reroute nao existem no formato de texto -- sao decoracao de
	 * layout. Atravessamos ate' o pino de verdade em vez de emitir uma linha
	 * que o builder nao saberia recriar. O mesmo vale para o node de conversao
	 * implicita, que o builder poe de volta sozinho.
	 *
	 * @param OutTraversed  recebe os nodes de conversao atravessados, para que a
	 *                      contagem de orfaos nao os acuse de "nao alimentam
	 *                      nada" -- eles alimentam, so' nao viraram linha.
	 */
	UEdGraphPin* FollowToSourcePin(UEdGraphPin* InputPin, TSet<UEdGraphNode*>* OutTraversed = nullptr)
	{
		UEdGraphPin* Current = InputPin;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Source = Current->LinkedTo[0];
			if (!Source)
			{
				return nullptr;
			}

			UEdGraphNode* SourceNode = Source->GetOwningNode();

			if (const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(SourceNode))
			{
				Current = Knot->GetInputPin();
				continue;
			}

			if (IsImplicitConversionNode(SourceNode))
			{
				if (UEdGraphPin* Inner = FindSingleDataInput(SourceNode))
				{
					if (OutTraversed)
					{
						OutTraversed->Add(SourceNode);
					}
					Current = Inner;
					continue;
				}
			}

			return Source;
		}

		return nullptr;
	}

	/**
	 * Idem, no sentido da execucao -- e devolvendo o PINO em que o fio cai, nao
	 * so' o node dono dele.
	 *
	 * O node nao basta. Um fio de execucao pode chegar num pino que nao e' a
	 * entrada principal do node: o `Reset` de um Do Once, o `Stop` de uma
	 * Timeline, o `Close` de um Gate. Esses nao continuam a cadeia -- sao um
	 * comando lateral para um node que ja' esta' em outro lugar do grafo.
	 *
	 * Enquanto so' o node era seguido, os dois eram a mesma coisa para a leitura,
	 * e ela descrevia o grafo errado com toda a confianca: dois Do Once que se
	 * resetam saiam empilhados, um debaixo do outro, como se um chamasse o outro.
	 */
	UEdGraphPin* FollowExecTargetPin(UEdGraphPin* ExecOutput)
	{
		UEdGraphPin* Current = ExecOutput;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Next = Current->LinkedTo[0];
			if (!Next)
			{
				return nullptr;
			}

			const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Next->GetOwningNode());
			if (!Knot)
			{
				return Next;
			}

			Current = Knot->GetOutputPin();
		}

		return nullptr;
	}

	TArray<UEdGraphPin*> GetExecOutputs(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Outputs;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) && Pin->Direction == EGPD_Output)
			{
				Outputs.Add(Pin);
			}
		}
		return Outputs;
	}

	UEdGraphPin* FindExecInput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) && Pin->Direction == EGPD_Input)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/**
	 * A entrada de execucao por onde a cadeia realmente continua: a primeira.
	 *
	 * Qualquer outra e' um comando lateral, e o node do outro lado nao pertence a
	 * esta cadeia -- ele tem vida propria em outro ponto do grafo. Escrever um
	 * abaixo do outro diria que um leva ao outro.
	 */
	bool IsPrimaryExecInput(UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		return Node && Pin && FindExecInput(Node) == Pin;
	}

	/** A saida que `$nome` sozinho alcanca. Mesma regra do builder. */
	UEdGraphPin* FindPrimaryOutput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
			{
				return Pin;
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	/**
	 * true quando `$nome` sozinho, sem `.Pino`, alcanca esse pino sem chute.
	 *
	 * `FindPrimaryOutput` responde "o primeiro pino de saida" quando nao ha'
	 * `ReturnValue`, e isso e' uma escolha, nao um fato: num Break de struct os
	 * pinos so' existem para os campos marcados como visiveis, entao o "primeiro"
	 * muda de node para node e muda de novo quando alguem marca outro campo.
	 *
	 * Escrever `$x` puro nesse caso produzia um texto que, colado de volta, ligava
	 * no primeiro campo do node *recriado* -- que tem todos os campos visiveis, e
	 * portanto pode ser outro campo. Ligacao plausivel e errada, em silencio.
	 *
	 * Por isso a regra aqui e' mais estreita que a do builder de proposito: so'
	 * `ReturnValue`, ou saida unica. Qualquer outra coisa sai nomeada.
	 */
	bool IsUnambiguousPrimaryOutput(UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		if (!Node || !Pin)
		{
			return false;
		}

		if (Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
		{
			return true;
		}

		// Break de struct nunca conta como saida unica, mesmo quando so' um campo
		// esta' visivel -- e' justamente ai' que a armadilha mora. Os pinos de um
		// Break sao os campos *marcados* no painel do node, e o Break recriado ao
		// colar nasce com todos marcados: `$x` puro sairia do campo escondido e
		// entraria no primeiro campo da struct, que e' outro.
		if (Node->IsA<UK2Node_BreakStruct>())
		{
			return false;
		}

		int32 DataOutputs = 0;
		for (const UEdGraphPin* Other : Node->Pins)
		{
			if (Other->Direction == EGPD_Output && !IsExecPin(Other) && !Other->bHidden)
			{
				++DataOutputs;
			}

			// Um `ReturnValue` em qualquer lugar da lista ja' e' a saida principal,
			// e este pino nao e' ele.
			if (Other->Direction == EGPD_Output && Other->PinName == UEdGraphSchema_K2::PN_ReturnValue)
			{
				return false;
			}
		}

		return DataOutputs == 1;
	}

	/**
	 * true quando o valor precisa mudar de tipo para entrar no pino.
	 *
	 * Um `int32` num pino `int64` compila e roda; a diferenca entre ter passado
	 * por essa conversao ou nao e' a diferenca entre a chave certa de um mapa e
	 * uma chave que colide. Como o node de conversao e' atravessado, sem esta
	 * marca a leitura ficaria igual nos dois casos.
	 *
	 * Categoria diferente basta. Objeto para objeto de outra classe nao entra:
	 * passar um `PlayerController` num pino de `Actor` e' heranca, nao conversao,
	 * e marcaria quase toda linha do grafo sem dizer nada.
	 */
	bool NeedsConversionNote(const UEdGraphPin* Source, const UEdGraphPin* Target)
	{
		if (!Source || !Target)
		{
			return false;
		}

		const FEdGraphPinType& From = Source->PinType;
		const FEdGraphPinType& To = Target->PinType;

		// Wildcard e' o pino que ainda vai virar o tipo de quem se ligar nele
		// (`Find` num TMap, corpo de macro). Ali nao ha' conversao nenhuma.
		if (From.PinCategory == UEdGraphSchema_K2::PC_Wildcard
			|| To.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			return false;
		}

		if (From.PinCategory != To.PinCategory)
		{
			return true;
		}

		// Struct para struct de outro tipo tambem e' conversao (Vector -> Vector2D).
		if (From.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			return From.PinSubCategoryObject != To.PinSubCategoryObject;
		}

		return false;
	}

	/** true quando o node participa do fluxo de execucao (tem fio branco). */
	bool IsExecNode(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Nome de tipo que o builder sabe ler de volta.
	 *
	 * `UEdGraphSchema_K2::TypeToText` devolve o que a interface mostra em painel
	 * de detalhes -- `IntProperty`, `Timer Handle Structure` --, e nenhum desses
	 * volta: o builder espera `Integer` e `TimerHandle`. Esta funcao e' o espelho
	 * exato de `ResolvePinTypeFromName`, entao o que sai daqui entra la'.
	 *
	 * Mora em NodeScribePropertyText porque a ficha de propriedades precisa
	 * escrever tipo com o mesmo vocabulario -- dois nomes para o mesmo tipo
	 * seriam duas linguagens para o usuario aprender.
	 */
	using NodeScribePropertyText::DescribePinType;

	/**
	 * true quando separar o nome em palavras vai errar.
	 *
	 * `NameToDisplayString` decide onde cabe espaco olhando a troca de caixa, e
	 * sigla quebra essa regra: `JSL4UControllerInfo` sai como `JSL4UController
	 * Info`, com a divisao no lugar errado. Sigla ou numero no nome e' o sinal de
	 * que nao da' para saber onde as palavras comecam -- e nesse caso o nome cru
	 * e' melhor que um chute.
	 */
	bool HasAcronymOrDigit(const FString& Name)
	{
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			if (FChar::IsDigit(Name[Index]))
			{
				return true;
			}

			if (Index > 0 && FChar::IsUpper(Name[Index]) && FChar::IsUpper(Name[Index - 1]))
			{
				return true;
			}
		}

		return false;
	}

	/** O nome que o usuario ve' e digita, que raramente e' o nome interno. */
	FString GetWrittenPinName(const UEdGraphPin* Pin)
	{
		if (!Pin->PinFriendlyName.IsEmpty())
		{
			return Pin->PinFriendlyName.ToString();
		}

		// Sem nome de tela, sobra o nome do parametro em C++: `InString`,
		// `TargetMap`, `bIsChecked`. A tela mostra `In String`, `Target Map` e
		// `Is Checked`, e e' assim que alguem escreve ao conferir a linha. A busca
		// do builder normaliza as duas formas para a mesma coisa, entao o texto
		// volta igual -- o que muda e' ele parecer o grafo que descreve.
		//
		// `bIsBool` tira o `b` de `bAimMode`, do mesmo jeito que a interface tira;
		// o builder tem o caso espelho para reencontrar o pino.
		const FString Name = Pin->PinName.ToString();
		if (HasAcronymOrDigit(Name))
		{
			return Name;
		}

		const bool bIsBool = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
		return FName::NameToDisplayString(Name, bIsBool);
	}

	/** Aspas: mesma regra para grafo e para ficha, definida uma vez so'. */
	using NodeScribePropertyText::NeedsQuotes;
	using NodeScribePropertyText::TryQuote;

	/**
	 * true quando o builder resolveria esse nome como struct, nao como funcao.
	 *
	 * `Break Vector` e' o caso: existe a funcao `UKismetMathLibrary::BreakVector`
	 * e existe o node de Break da struct FVector, com o mesmo nome de tela e
	 * pinos diferentes. O builder testa a forma de struct antes do catalogo,
	 * entao escrever o nome puro devolveria o outro node.
	 */
	bool IsShadowedByStructForm(const FString& DisplayName)
	{
		FString StructName;

		if (DisplayName.StartsWith(TEXT("Make "), ESearchCase::IgnoreCase))
		{
			StructName = DisplayName.RightChop(5);
		}
		else if (DisplayName.StartsWith(TEXT("Break "), ESearchCase::IgnoreCase))
		{
			StructName = DisplayName.RightChop(6);
		}
		else
		{
			return false;
		}

		const FString Normalized = FNodeScribeCatalog::Normalize(StructName);
		if (Normalized.IsEmpty())
		{
			return false;
		}

		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			if (FNodeScribeCatalog::Normalize(StructIt->GetName()) == Normalized
				|| FNodeScribeCatalog::Normalize(StructIt->GetDisplayNameText().ToString()) == Normalized)
			{
				return true;
			}
		}

		return false;
	}

	/** Tira o verbo da expressao antes de virar nome: `Get Vida` -> `Vida`. */
	FString MakeNameBase(const FString& Expression)
	{
		static const TCHAR* const Verbs[] = {
			TEXT("event "), TEXT("Get "), TEXT("Set "), TEXT("Cast to "),
			TEXT("Make "), TEXT("Break ")
		};

		FString Base = Expression;
		for (const TCHAR* Verb : Verbs)
		{
			if (Base.StartsWith(Verb, ESearchCase::IgnoreCase))
			{
				Base = Base.RightChop(FCString::Strlen(Verb));
				break;
			}
		}

		Base.RemoveFromEnd(TEXT("__DelegateSignature"));

		// `OnKeySelected of SelecionarInputKey` vira `onKeySelected`: o dono do
		// dispatcher ja' esta' na linha, repeti-lo no nome so' faz ruido.
		FString BeforeOwner;
		FString Ignored;
		if (Base.Split(TEXT(" of "), &BeforeOwner, &Ignored, ESearchCase::IgnoreCase)
			|| Base.Split(TEXT(" de "), &BeforeOwner, &Ignored, ESearchCase::IgnoreCase))
		{
			Base = BeforeOwner;
		}

		return Base;
	}

	/** Primeira letra minuscula, so' alfanumerico: vira um `$nome` legivel. */
	FString ToIdentifier(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());

		for (const TCHAR C : In)
		{
			if (FChar::IsAlnum(C))
			{
				Out.AppendChar(C);
			}
		}

		if (Out.IsEmpty())
		{
			return TEXT("value");
		}

		if (FChar::IsDigit(Out[0]))
		{
			Out.InsertAt(0, TEXT('v'));
		}

		Out[0] = FChar::ToLower(Out[0]);
		return Out;
	}
}

// ---------------------------------------------------------------------------

/**
 * Estado de uma leitura. Percorre as cadeias de execucao em profundidade, na
 * mesma ordem em que o builder as desenharia, e vai empilhando linhas.
 */
class FNodeScribeReadContext
{
public:
	FNodeScribeReadContext(const TArray<UEdGraphNode*>& InNodes, UBlueprint* InBlueprint);

	void Run();

	FNodeScribeReader::FResult Result;

private:
	void EmitLine(int32 Indent, const FString& Text);

	void AddInfo(const FString& Message);
	void AddWarning(const FString& Message);

	/**
	 * Marca, antes de emitir qualquer linha, quais nodes de execucao tem uma
	 * saida de dado consumida por alguem. Sem essa passagem previa a linha ja'
	 * teria saido sem o `nome =`, e o consumidor la' na frente acabaria
	 * emitindo o node de novo -- o que, ao colar de volta, criaria dois.
	 */
	void CollectConsumedNodes();

	void EmitExecChain(UEdGraphNode* Node, int32 Indent);

	/**
	 * Um fio de execucao saindo de `ExecOutput`: continua a cadeia, volta para
	 * uma ancora, ou entra de lado num node que vive em outro lugar do grafo.
	 */
	void EmitExecLink(UEdGraphPin* ExecOutput, int32 Indent);

	/** O numero de ancora de um node, reservando um se ele ainda nao tiver. */
	int32 AnchorFor(UEdGraphNode* Node);

	/** Escreve `# ancora N` na linha de cada node ancorado, no fim de tudo. */
	void StampAnchors();

	void EmitNodeLine(UEdGraphNode* Node, int32 Indent);

	/** Emite a linha de um node puro de dado e devolve o nome dado a ele. */
	FString EmitDataNode(UEdGraphNode* Node, int32 Indent);

	/** O token que aponta para esse pino: `$nome` ou `$nome.Pino`. */
	FString MakeReferenceTo(UEdGraphPin* SourcePin, int32 Indent);

	/** Devolve vazio quando o node volta igual; senao, o motivo de nao voltar. */
	FString DescribeNode(UEdGraphNode* Node, FString& OutRoundTripIssue);
	FString BuildArgumentList(UEdGraphNode* Node, int32 Indent);
	FString DescribeLiteral(UEdGraphPin* Pin, bool& bOutRepresentable);

	/** Rotulo de saida de execucao que volta como o mesmo pino ao ser lido. */
	FString DescribeExecLabel(UEdGraphPin* Pin);

	FString MakeUniqueName(const FString& Base);

	/** Nome curto e estavel para o titulo do node, usado em diagnostico. */
	FString ShortTitle(UEdGraphNode* Node) const;

	TArray<UEdGraphNode*> Nodes;
	UBlueprint* Blueprint = nullptr;

	TSet<UEdGraphNode*> Scope;
	TSet<UEdGraphNode*> EmittedExec;
	TSet<UEdGraphNode*> NeedsName;
	TMap<UEdGraphNode*, FString> DataNames;
	TSet<FString> UsedNames;

	/** Em que linha do texto cada node de execucao saiu, para ancorar a volta. */
	TMap<UEdGraphNode*, int32> ExecLineIndex;

	/** Numero da ancora de cada node que recebe execucao de mais de um lugar. */
	TMap<UEdGraphNode*, int32> AnchorOf;
	int32 NextAnchor = 1;

	/** Conversoes implicitas que foram atravessadas: nao sao orfas. */
	TSet<UEdGraphNode*> TraversedConversions;

	TArray<FString> Lines;
};

FNodeScribeReadContext::FNodeScribeReadContext(const TArray<UEdGraphNode*>& InNodes, UBlueprint* InBlueprint)
	: Nodes(InNodes)
	, Blueprint(InBlueprint)
{
	for (UEdGraphNode* Node : Nodes)
	{
		// Reroute nao vira linha: e' atravessado na hora de seguir os fios.
		if (Node && !Node->IsA<UK2Node_Knot>())
		{
			Scope.Add(Node);
		}
	}
}

void FNodeScribeReadContext::EmitLine(int32 Indent, const FString& Text)
{
	FString Line;
	for (int32 Level = 0; Level < Indent; ++Level)
	{
		Line += IndentUnit;
	}
	Lines.Add(Line + Text);
}

void FNodeScribeReadContext::AddInfo(const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Info, 0, Message);
}

void FNodeScribeReadContext::AddWarning(const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Warning, 0, Message);
	++Result.WarningCount;
}

FString FNodeScribeReadContext::ShortTitle(UEdGraphNode* Node) const
{
	FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	Title.ReplaceInline(TEXT("\n"), TEXT(" "));
	Title.TrimStartAndEndInline();
	return Title;
}

FString FNodeScribeReadContext::MakeUniqueName(const FString& Base)
{
	FString Candidate = ToIdentifier(Base);

	int32 Suffix = 2;
	while (UsedNames.Contains(Candidate))
	{
		Candidate = ToIdentifier(Base) + FString::FromInt(Suffix++);
	}

	UsedNames.Add(Candidate);
	return Candidate;
}

// ---------------------------------------------------------------------------
// Nome do node
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::DescribeNode(UEdGraphNode* Node, FString& OutRoundTripIssue)
{
	OutRoundTripIssue.Reset();

	// Evento vinculado ao dispatcher de outro objeto: "On Key Selected
	// (SelecionarInputKey)" no grafo. Vem antes de Event porque deriva dele.
	if (const UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
	{
		return FString::Printf(TEXT("event %s of %s"),
			*BoundEvent->DelegatePropertyName.ToString(),
			*BoundEvent->GetComponentPropertyName().ToString());
	}

	// CustomEvent antes de Event: o primeiro deriva do segundo.
	if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
	{
		return TEXT("event ") + CustomEvent->CustomFunctionName.ToString();
	}

	if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
	{
		const FString EventName = FNodeScribeCatalog::StripEventPrefix(
			Event->EventReference.GetMemberName().ToString());

		// Override de evento da classe pai volta igual. Evento ligado a um
		// dispatcher ou a um delegate nao: `evento X` recriaria um Custom Event
		// solto, sem o vinculo, e o node ficaria parecido e morto.
		if (!Event->bOverrideFunction)
		{
			OutRoundTripIssue = FString::Printf(
				TEXT("`%s` esta' ligado a um dispatcher/delegate. O formato ainda nao tem forma para esse vinculo: ")
				TEXT("colar de volta criaria um Custom Event solto, que nunca dispara. Recrie esse node na mao."),
				*ShortTitle(Node));
		}

		return TEXT("event ") + EventName;
	}

	if (Node->IsA<UK2Node_IfThenElse>())
	{
		return TEXT("Branch");
	}

	if (Node->IsA<UK2Node_ExecutionSequence>())
	{
		return TEXT("Sequence");
	}

	if (Node->IsA<UK2Node_Select>())
	{
		return TEXT("Select");
	}

	if (Node->IsA<UK2Node_Self>())
	{
		return TEXT("Self");
	}

	if (const UK2Node_BaseMCDelegate* Delegate = Cast<UK2Node_BaseMCDelegate>(Node))
	{
		const FString DelegateName = Delegate->GetPropertyName().ToString();

		if (Node->IsA<UK2Node_CallDelegate>())   { return TEXT("Call ") + DelegateName; }
		if (Node->IsA<UK2Node_AddDelegate>())    { return TEXT("Bind ") + DelegateName; }
		if (Node->IsA<UK2Node_RemoveDelegate>()) { return TEXT("Unbind ") + DelegateName; }
		if (Node->IsA<UK2Node_ClearDelegate>())  { return TEXT("Clear ") + DelegateName; }

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` mexe num dispatcher de um jeito que o formato ainda nao tem."), *ShortTitle(Node));

		return ShortTitle(Node);
	}

	if (const UK2Node_SwitchEnum* SwitchEnum = Cast<UK2Node_SwitchEnum>(Node))
	{
		if (const UEnum* Enum = SwitchEnum->GetEnum())
		{
			return TEXT("Switch on ") + Enum->GetName();
		}

		OutRoundTripIssue = TEXT("Switch de enum sem enum definido.");
		return TEXT("Switch on ?");
	}

	if (Node->IsA<UK2Node_SwitchInteger>())
	{
		return TEXT("Switch on Int");
	}

	if (Node->IsA<UK2Node_SwitchString>())
	{
		return TEXT("Switch on String");
	}

	if (Node->IsA<UK2Node_SwitchName>())
	{
		return TEXT("Switch on Name");
	}

	if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		if (UClass* TargetType = CastNode->TargetType)
		{
			FString ClassName = TargetType->GetName();
			ClassName.RemoveFromEnd(TEXT("_C"));
			return TEXT("Cast to ") + ClassName;
		}

		OutRoundTripIssue = TEXT("Cast sem classe de destino definida.");
		return TEXT("Cast to ?");
	}

	if (const UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node))
	{
		return TEXT("Set ") + Setter->VariableReference.GetMemberName().ToString();
	}

	if (const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
	{
		return TEXT("Get ") + Getter->VariableReference.GetMemberName().ToString();
	}

	if (Node->IsA<UK2Node_GetSubsystem>())
	{
		// `CustomClass` e' protegido no node, mas o tipo do pino de saida
		// carrega a mesma informacao e e' publico.
		if (const UEdGraphPin* Output = FindPrimaryOutput(Node))
		{
			if (const UClass* SubsystemClass = Cast<UClass>(Output->PinType.PinSubCategoryObject.Get()))
			{
				return TEXT("Get ") + SubsystemClass->GetName();
			}
		}

		OutRoundTripIssue = TEXT("Node de subsistema sem classe definida.");
		return TEXT("Get ?");
	}

	// MakeStruct antes de BreakStruct: nao ha' heranca entre eles, mas os dois
	// carregam StructType e a ordem deixa a leitura obvia.
	if (const UK2Node_MakeStruct* MakeStruct = Cast<UK2Node_MakeStruct>(Node))
	{
		if (const UScriptStruct* Struct = MakeStruct->StructType)
		{
			return TEXT("Make ") + Struct->GetName();
		}

		OutRoundTripIssue = TEXT("Make de struct sem struct definida.");
		return TEXT("Make ?");
	}

	if (const UK2Node_BreakStruct* BreakStruct = Cast<UK2Node_BreakStruct>(Node))
	{
		if (const UScriptStruct* Struct = BreakStruct->StructType)
		{
			return TEXT("Break ") + Struct->GetName();
		}

		OutRoundTripIssue = TEXT("Break de struct sem struct definida.");
		return TEXT("Break ?");
	}

	// Evento de Input Action. A classe vem por nome porque o modulo dela nao e'
	// dependencia deste plugin -- Enhanced Input pode nem estar instalado.
	if (Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction"))
	{
		if (const FObjectProperty* ActionProperty =
			FindFProperty<FObjectProperty>(Node->GetClass(), TEXT("InputAction")))
		{
			if (const UObject* Action = ActionProperty->GetObjectPropertyValue_InContainer(Node))
			{
				// Caminho completo: o nome curto so' resolve se o asset ja'
				// estiver carregado, e um texto colado dias depois nao garante isso.
				return TEXT("EnhancedInputAction ") + Action->GetPathName();
			}
		}

		OutRoundTripIssue = TEXT("Node de Input Action sem action definida.");
		return TEXT("EnhancedInputAction ?");
	}

	if (Node->IsA<UK2Node_ConstructObjectFromClass>())
	{
		// O titulo muda com a classe escolhida ("Create WBP_X Widget"), entao
		// nao serve de nome. A classe do node e' estavel e diz a mesma coisa;
		// qual classe construir sai no argumento `Class`.
		const FString NodeClassName = Node->GetClass()->GetName();

		if (NodeClassName == TEXT("K2Node_CreateWidget"))
		{
			return TEXT("Create Widget");
		}

		if (NodeClassName == TEXT("K2Node_SpawnActorFromClass"))
		{
			return TEXT("Spawn Actor from Class");
		}

		if (NodeClassName == TEXT("K2Node_ConstructObjectFromClass"))
		{
			return TEXT("Construct Object from Class");
		}

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` constroi objeto a partir de classe, mas o formato so' conhece Create Widget, ")
			TEXT("Spawn Actor from Class e Construct Object from Class."),
			*ShortTitle(Node));

		return ShortTitle(Node);
	}

	// Node de acao assincrona: `Wait For Any Controller Changes` e companhia.
	//
	// O titulo ja' era o nome certo -- o que faltava era o leitor saber disso e
	// parar de avisar que o node nao volta. Um plugin que expoe varios
	// `UBlueprintAsyncActionBase` tinha todos os grafos marcados como nao
	// colaveis por causa desta linha.
	if (const UK2Node_BaseAsyncTask* AsyncNode = Cast<UK2Node_BaseAsyncTask>(Node))
	{
		if (UFunction* Factory = AsyncNode->GetFactoryFunction())
		{
			// Subclasse com node proprio (task de GAS, por exemplo) nao volta: o
			// builder cria `UK2Node_AsyncAction`, que e' outro node.
			if (Node->GetClass() != UK2Node_AsyncAction::StaticClass())
			{
				OutRoundTripIssue = FString::Printf(
					TEXT("`%s` usa o node `%s`, que tem logica propria. O formato recria acao assincrona ")
					TEXT("como `UK2Node_AsyncAction`: colar de volta daria um node parecido e diferente. ")
					TEXT("Recrie esse na mao."),
					*ShortTitle(Node), *Node->GetClass()->GetName());
			}

			const FString DisplayName = Factory->GetDisplayNameText().ToString();

			UClass* SelfClass = Blueprint
				? (Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get())
				: nullptr;

			const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(DisplayName, SelfClass, nullptr);
			if (Lookup.Function == Factory)
			{
				return DisplayName;
			}

			// Ambiguo pelo nome de tela: qualificar pela classe da fabrica e' o
			// mesmo caminho de `Classe.Funcao` de uma chamada comum.
			if (const UClass* Owner = Factory->GetOwnerClass())
			{
				return Owner->GetName() + TEXT(".") + Factory->GetName();
			}

			return DisplayName;
		}

		OutRoundTripIssue = TEXT("Node de acao assincrona sem funcao de fabrica definida.");
		return ShortTitle(Node);
	}

	// O `Return Node` de um grafo de funcao. O builder ja' sabia cria-lo; o
	// leitor e' que nao sabia nomea-lo, e ele saia com aviso de node estranho.
	if (Node->IsA<UK2Node_FunctionResult>())
	{
		return TEXT("Return");
	}

	// `To Text` de qualquer coisa. E' um node proprio, e nao a funcao de mesmo
	// nome -- a funcao e' `BlueprintInternalUseOnly` e nao existe para o
	// catalogo, entao a linha caia no fallback com aviso.
	if (Node->IsA<UK2Node_GenericToText>())
	{
		return TEXT("To Text");
	}

	// A entrada de uma funcao nao se recria por texto: ela nasce com a funcao.
	// O que da' para fazer e' dizer a assinatura, para quem le' criar a funcao
	// certa e colar o resto dentro dela.
	if (Node->IsA<UK2Node_FunctionEntry>())
	{
		TArray<FString> Parameters;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && !IsExecPin(Pin) && !Pin->bHidden)
			{
				Parameters.Add(FString::Printf(TEXT("%s : %s"),
					*GetWrittenPinName(Pin), *DescribePinType(Pin->PinType)));
			}
		}

		OutRoundTripIssue = FString::Printf(
			TEXT("`%s` e' a entrada da funcao, e nasce junto com ela -- nao ha' linha que a crie. ")
			TEXT("Crie a funcao com os parametros (%s) e cole o resto dentro dela."),
			*ShortTitle(Node),
			Parameters.Num() > 0 ? *FString::Join(Parameters, TEXT(", ")) : TEXT("nenhum"));

		return TEXT("Function Entry");
	}

	if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
	{
		if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
		{
			return MacroGraph->GetName();
		}

		OutRoundTripIssue = TEXT("Macro sem grafo definido.");
		return TEXT("Macro ?");
	}

	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Function = Call->GetTargetFunction())
		{
			const FString DisplayName = Function->GetDisplayNameText().ToString();

			// O nome curto so' serve se ele voltar para esta mesma funcao.
			// `Apply Settings` existe em GameUserSettings e em
			// EnhancedInputUserSettings; escrever o nome ambiguo aqui seria
			// jogar fora uma informacao que so' nos temos, para o builder
			// descobrir na hora de colar que nao da' para decidir.
			UClass* SelfClass = Blueprint
				? (Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get())
				: nullptr;

			const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(DisplayName, SelfClass, nullptr);

			// Nome de tela com parenteses nao volta, e sao comuns: `SetText
			// (Text)` esta' em todo grafo de UI, `To Integer64 (Integer)` em toda
			// conversao. O parser corta a linha no primeiro `(` para achar os
			// argumentos, entao `SetText (Text) (Target = $x)` voltaria como um
			// node chamado `SetText` recebendo um argumento chamado `Text`.
			// A forma qualificada nao tem parenteses e resolve para a mesma funcao.
			const bool bNameBreaksParsing = DisplayName.Contains(TEXT("("))
				|| DisplayName.Contains(TEXT(")"));

			// O catalogo achar a funcao nao basta: o builder testa as formas
			// especiais antes dele, e `Break Vector` cairia na struct.
			if (Lookup.Function == Function
				&& !IsShadowedByStructForm(DisplayName)
				&& !bNameBreaksParsing)
			{
				return DisplayName;
			}

			if (const UClass* Owner = Function->GetOwnerClass())
			{
				// `SKEL_WBP_X_C.PreencherLista` nao volta: essa classe e' um
				// artefato de compilacao e nao existe para quem le'. Funcao do
				// proprio Blueprint sai pelo nome puro, que o builder resolve
				// olhando a classe de destino.
				const FString OwnerName = Owner->GetName();
				const bool bIsCompilationArtifact =
					OwnerName.StartsWith(TEXT("SKEL_"))
					|| OwnerName.StartsWith(TEXT("REINST_"))
					|| OwnerName.StartsWith(TEXT("TRASHCLASS_"));

				if (!bIsCompilationArtifact && Owner != SelfClass)
				{
					return OwnerName + TEXT(".") + Function->GetName();
				}

				return Function->GetName();
			}

			return DisplayName;
		}
	}

	// Sobrou algo que o builder nao sabe recriar a partir do nome.
	const FString Title = ShortTitle(Node);
	OutRoundTripIssue = FString::Printf(
		TEXT("Nao sei escrever `%s` de um jeito que volte igual: a linha saiu com o titulo do node, ")
		TEXT("que o builder pode nao encontrar."),
		*Title);

	return Title;
}

FString FNodeScribeReadContext::DescribeExecLabel(UEdGraphPin* Pin)
{
	// O rotulo e' comparado com o nome interno do pino depois de passar pelos
	// apelidos. `then` aceita `True`, entao preferimos o nome amigavel, que e'
	// o que aparece na tela -- desde que ele volte para o mesmo pino.
	//
	// So' no Branch. `then` tambem e' o nome do pino "continua daqui" de um node
	// de acao assincrona, e ali escrever `True:` inventa uma condicao que nao
	// existe: quem le' entende que ha' um teste, e o ramo de baixo passa a
	// parecer o `False` dele. O rotulo `then` volta pelo apelido do formato.
	const FString Friendly = GetWrittenPinName(Pin);
	const FString Internal = Pin->PinName.ToString();

	if (Pin->GetOwningNode() && Pin->GetOwningNode()->IsA<UK2Node_IfThenElse>())
	{
		static const TMap<FString, FString> KnownFriendly = {
			{ TEXT("then"), TEXT("True") },
			{ TEXT("else"), TEXT("False") }
		};

		if (const FString* Preferred = KnownFriendly.Find(FNodeScribeCatalog::Normalize(Internal)))
		{
			return *Preferred;
		}
	}

	return Friendly.IsEmpty() ? Internal : Friendly;
}

// ---------------------------------------------------------------------------
// Valores
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::DescribeLiteral(UEdGraphPin* Pin, bool& bOutRepresentable)
{
	bOutRepresentable = true;

	if (IsObjectLikePin(Pin))
	{
		if (const UObject* Asset = Pin->DefaultObject)
		{
			return Asset->GetPathName();
		}

		// Pino de objeto vazio e' exatamente o buraco que `?` declara.
		return TEXT("?");
	}

	FString Value = Pin->DefaultValue;
	if (Value.IsEmpty() && !Pin->DefaultTextValue.IsEmpty())
	{
		Value = Pin->DefaultTextValue.ToString();
	}

	if (Value.IsEmpty())
	{
		// Vazio aqui nao e' "nao sei dizer": e' o valor. Quem chama so' chega
		// nesta funcao quando o pino difere do padrao de fabrica, e nesse caso o
		// valor foi apagado de proposito. Devolver nada fazia a linha sumir, e o
		// texto voltava com o padrao de novo no pino -- outro comportamento.
		return TEXT("\"\"");
	}

	// `100.000000` no meio de uma linha esconde o numero no ruido. A ficha ja'
	// formatava assim; o leitor de grafo nao, e era a mesma decisao aplicada em
	// metade do plugin.
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && Value.IsNumeric())
	{
		Value = NodeScribePropertyText::FormatFloat(FCString::Atod(*Value));
	}

	if (!NeedsQuotes(Value))
	{
		return Value;
	}

	FString Quoted;
	if (!TryQuote(Value, Quoted))
	{
		bOutRepresentable = false;
		return FString();
	}

	return Quoted;
}

FString FNodeScribeReadContext::BuildArgumentList(UEdGraphNode* Node, int32 Indent)
{
	TArray<FString> Args;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin) || Pin->bHidden)
		{
			continue;
		}

		if (UEdGraphPin* SourcePin = FollowToSourcePin(Pin, &TraversedConversions))
		{
			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

			if (!Scope.Contains(SourceNode))
			{
				AddWarning(FString::Printf(
					TEXT("`%s` recebe `%s` de um node que ficou fora da selecao. ")
					TEXT("Esse pino saiu sem valor -- selecione o grafo inteiro ou ligue na mao."),
					*ShortTitle(Node), *GetWrittenPinName(Pin)));
				continue;
			}

			FString Reference = MakeReferenceTo(SourcePin, Indent);
			if (!Reference.IsEmpty())
			{
				// A conversao entre os dois tipos e' feita por um node que a
				// Unreal poe sozinha e que nao vira linha. Sem esta marca, um
				// `Device Id` (int32) promovido e um `Connection Id` (int64)
				// escrevem exatamente a mesma linha -- e um dos dois faz todo
				// controle colidir na mesma chave do mapa.
				//
				// Entre parenteses depois da referencia: e' anotacao de leitura,
				// e o parser a descarta ao colar de volta.
				if (NeedsConversionNote(SourcePin, Pin))
				{
					Reference += FString::Printf(TEXT(" (%s -> %s)"),
						*DescribePinType(SourcePin->PinType), *DescribePinType(Pin->PinType));
				}

				Args.Add(FString::Printf(TEXT("%s = %s"), *GetWrittenPinName(Pin), *Reference));
			}
			continue;
		}

		// Pino do tipo `self` sem ligacao e' o alvo implicito: nao se escreve.
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			continue;
		}

		// Um valor identico ao padrao do pino nao acrescenta nada a' linha.
		if (Pin->DefaultValue == Pin->AutogeneratedDefaultValue && !Pin->DefaultObject)
		{
			continue;
		}

		bool bRepresentable = true;
		const FString Literal = DescribeLiteral(Pin, bRepresentable);

		if (!bRepresentable)
		{
			AddWarning(FString::Printf(
				TEXT("O valor do pino `%s` em `%s` tem aspas dos dois tipos e nao cabe numa linha. ")
				TEXT("Copie esse valor na mao."),
				*GetWrittenPinName(Pin), *ShortTitle(Node)));
			continue;
		}

		if (!Literal.IsEmpty())
		{
			Args.Add(FString::Printf(TEXT("%s = %s"), *GetWrittenPinName(Pin), *Literal));
		}
	}

	if (Args.Num() == 0)
	{
		return FString();
	}

	return TEXT(" (") + FString::Join(Args, TEXT(", ")) + TEXT(")");
}

// ---------------------------------------------------------------------------
// Nodes de dado
// ---------------------------------------------------------------------------

FString FNodeScribeReadContext::EmitDataNode(UEdGraphNode* Node, int32 Indent)
{
	FString RoundTripIssue;
	const FString Expression = DescribeNode(Node, RoundTripIssue);

	if (!RoundTripIssue.IsEmpty())
	{
		AddWarning(RoundTripIssue);
	}

	// Os argumentos podem emitir mais linhas de dado, que precisam vir antes
	// desta -- por isso montamos a lista antes de emitir.
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Name = MakeUniqueName(MakeNameBase(Expression));
	DataNames.Add(Node, Name);

	EmitLine(Indent, FString::Printf(TEXT("%s = %s%s"), *Name, *Expression, *Arguments));
	++Result.NodeCount;

	return Name;
}

FString FNodeScribeReadContext::MakeReferenceTo(UEdGraphPin* SourcePin, int32 Indent)
{
	UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

	FString Name;

	if (const FString* Existing = DataNames.Find(SourceNode))
	{
		Name = *Existing;
	}
	else if (const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(SourceNode))
	{
		// Get de variavel propria nao precisa de linha: `$Vida` sozinho ja' faz
		// o builder criar o node. Uma linha a menos por variavel usada.
		if (Getter->VariableReference.IsSelfContext())
		{
			Name = Getter->VariableReference.GetMemberName().ToString();
			DataNames.Add(SourceNode, Name);
			UsedNames.Add(Name);
		}
	}

	if (Name.IsEmpty())
	{
		// Node de execucao nunca vira linha de dado: ele tem cadeia propria, e
		// emiti-lo aqui criaria um segundo node ao colar. A pre-passagem ja'
		// deveria ter dado nome a ele.
		if (IsExecNode(SourceNode))
		{
			AddWarning(FString::Printf(
				TEXT("`%s` participa da execucao e nao recebeu nome. ")
				TEXT("Essa ligacao de dado saiu do texto -- religue na mao."),
				*ShortTitle(SourceNode)));
			return FString();
		}

		Name = EmitDataNode(SourceNode, Indent);
	}

	FString Token = TEXT("$") + Name;

	// Node com mais de uma saida de dado precisa dizer qual delas: `$nome.Pino`.
	if (!IsUnambiguousPrimaryOutput(SourceNode, SourcePin))
	{
		const FString PinName = GetWrittenPinName(SourcePin);

		if (PinName.IsEmpty())
		{
			// Existe pino sem nome de tela nem nome interno. Dizer isso e' o unico
			// caminho honesto: escrever `$nome` puro aqui pareceria a saida
			// principal e ligaria em outro pino ao voltar.
			AddWarning(FString::Printf(
				TEXT("`%s` alimenta `%s` por um pino sem nome. Escrevi `<pino desconhecido>` no lugar: ")
				TEXT("essa ligacao voce refaz na mao."),
				*ShortTitle(SourceNode), *Name));

			return Token + TEXT(".<pino desconhecido>");
		}

		Token += TEXT(".") + PinName;
	}

	return Token;
}

// ---------------------------------------------------------------------------
// Cadeia de execucao
// ---------------------------------------------------------------------------

void FNodeScribeReadContext::EmitNodeLine(UEdGraphNode* Node, int32 Indent)
{
	FString RoundTripIssue;
	const FString Expression = DescribeNode(Node, RoundTripIssue);

	if (!RoundTripIssue.IsEmpty())
	{
		AddWarning(RoundTripIssue);
	}

	// O nome ja' foi decidido na pre-passagem, quando se soube que alguem
	// consome a saida deste node.
	const FString AssignedName = DataNames.FindRef(Node);

	// Os argumentos podem emitir linhas de dado, que precisam vir antes desta.
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Prefix = AssignedName.IsEmpty()
		? FString()
		: AssignedName + TEXT(" = ");

	// Guardado antes de emitir: se outra cadeia cair neste node la' na frente, e'
	// nesta linha que a ancora vai ser escrita.
	ExecLineIndex.Add(Node, Lines.Num());

	EmitLine(Indent, Prefix + Expression + Arguments);
	++Result.NodeCount;
}

void FNodeScribeReadContext::EmitExecChain(UEdGraphNode* Node, int32 Indent)
{
	if (!Node || !Scope.Contains(Node))
	{
		return;
	}

	if (EmittedExec.Contains(Node))
	{
		// Duas cadeias caindo no mesmo node. O formato e' uma arvore e nao sabe
		// dizer "volta para aquele ali" -- mas dizer *para onde* volta e' o que
		// separa "esse ramo nao foi ligado" de "esse ramo continua ali em cima".
		//
		// Sem isto o ramo saia com o rotulo e nada embaixo, do jeito que um ramo
		// vazio de verdade sai, e as duas coisas eram indistinguiveis. A ancora
		// vai como comentario nas duas pontas: o parser descarta, entao o texto
		// continua colavel exatamente como estava.
		const int32 Anchor = AnchorFor(Node);

		EmitLine(Indent, FString::Printf(TEXT("# -> volta para a ancora %d (`%s`)"),
			Anchor, *ShortTitle(Node)));

		AddWarning(FString::Printf(
			TEXT("A execucao volta a `%s` (ancora %d), que ja' apareceu antes. ")
			TEXT("O formato de texto so' descreve arvore: essa reconvergencia se perde ao colar e voce religa na mao."),
			*ShortTitle(Node), Anchor));
		return;
	}

	EmittedExec.Add(Node);
	EmitNodeLine(Node, Indent);

	const TArray<UEdGraphPin*> ExecOutputs = GetExecOutputs(Node);

	if (ExecOutputs.Num() == 1)
	{
		EmitExecLink(ExecOutputs[0], Indent);
		return;
	}

	for (UEdGraphPin* Output : ExecOutputs)
	{
		if (!FollowExecTargetPin(Output))
		{
			continue;
		}

		EmitLine(Indent, DescribeExecLabel(Output) + TEXT(":"));
		EmitExecLink(Output, Indent + 1);
	}
}

void FNodeScribeReadContext::EmitExecLink(UEdGraphPin* ExecOutput, int32 Indent)
{
	UEdGraphPin* TargetPin = FollowExecTargetPin(ExecOutput);
	if (!TargetPin)
	{
		return;
	}

	UEdGraphNode* Target = TargetPin->GetOwningNode();
	if (!Target || !Scope.Contains(Target))
	{
		return;
	}

	if (IsPrimaryExecInput(Target, TargetPin))
	{
		EmitExecChain(Target, Indent);
		return;
	}

	// Entrada lateral: `Reset` de um Do Once, `Stop` de uma Timeline, `Close` de
	// um Gate. O node do outro lado nao e' a continuacao desta cadeia -- ele tem
	// a propria linha em outro ponto do texto, e o que este fio faz e' mandar um
	// comando para la'. Descer nele escreveria uma cadeia que nao existe: era
	// assim que dois Do Once resetando um ao outro saiam empilhados, como se o
	// primeiro levasse ao segundo.
	//
	// A ancora e' a mesma da reconvergencia, e aqui ela pode apontar para uma
	// linha que ainda nao saiu -- por isso o numero e' reservado agora e escrito
	// no node no fim de tudo.
	const int32 Anchor = AnchorFor(Target);
	const FString PinLabel = GetWrittenPinName(TargetPin);

	EmitLine(Indent, FString::Printf(TEXT("# -> entra em `%s` pelo pino `%s` (ancora %d)"),
		*ShortTitle(Target), *PinLabel, Anchor));

	AddWarning(FString::Printf(
		TEXT("A execucao entra em `%s` (ancora %d) pelo pino `%s`, que nao e' a entrada principal dele. ")
		TEXT("O formato de texto so' descreve arvore: essa ligacao se perde ao colar e voce religa na mao."),
		*ShortTitle(Target), Anchor, *PinLabel));
}

int32 FNodeScribeReadContext::AnchorFor(UEdGraphNode* Node)
{
	if (const int32* Existing = AnchorOf.Find(Node))
	{
		return *Existing;
	}

	const int32 Anchor = NextAnchor++;
	AnchorOf.Add(Node, Anchor);
	return Anchor;
}

void FNodeScribeReadContext::StampAnchors()
{
	// No fim, quando toda linha ja' existe. Uma entrada lateral pode apontar para
	// um node que so' vai ser emitido depois -- marcar na hora perderia essas.
	for (const TTuple<UEdGraphNode*, int32>& Pair : AnchorOf)
	{
		if (const int32* LineIndex = ExecLineIndex.Find(Pair.Key))
		{
			Lines[*LineIndex] += FString::Printf(TEXT("  # ancora %d"), Pair.Value);
		}
	}
}

// ---------------------------------------------------------------------------

void FNodeScribeReadContext::CollectConsumedNodes()
{
	for (UEdGraphNode* Node : Scope)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin))
			{
				continue;
			}

			UEdGraphPin* SourcePin = FollowToSourcePin(Pin, &TraversedConversions);
			if (!SourcePin)
			{
				continue;
			}

			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

			// Node de execucao consumido como dado ganha nome AGORA, antes de
			// qualquer linha sair. Atribuir na hora da emissao nao bastava:
			// um evento referenciado por uma cadeia anterior a' dele ainda nao
			// teria nome, e acabava emitido duas vezes -- uma como dado, outra
			// como raiz. Colar esse texto de volta dava "o evento ja' existe".
			if (Scope.Contains(SourceNode) && IsExecNode(SourceNode) && !DataNames.Contains(SourceNode))
			{
				NeedsName.Add(SourceNode);

				FString RoundTripIssue;
				const FString Expression = DescribeNode(SourceNode, RoundTripIssue);
				DataNames.Add(SourceNode, MakeUniqueName(MakeNameBase(Expression)));
			}
		}
	}
}

void FNodeScribeReadContext::Run()
{
	if (Scope.Num() == 0)
	{
		AddInfo(TEXT("Nada para ler: nenhum node selecionado."));
		return;
	}

	CollectConsumedNodes();

	// Cabecalho: quem le' esse texto num chat nao tem como saber de onde ele
	// veio, e "qual asset e' esse" e' a primeira pergunta. Sai como comentario,
	// entao o parser ignora quando o texto volta.
	if (UEdGraph* SourceGraph = Nodes.Num() > 0 && Nodes[0] ? Nodes[0]->GetGraph() : nullptr)
	{
		FString Header = FString::Printf(TEXT("# %s -> %s"),
			Blueprint ? *Blueprint->GetName() : TEXT("?"),
			*SourceGraph->GetName());

		// Sem isso, um trecho parece um grafo inteiro com nodes faltando.
		if (Nodes.Num() < SourceGraph->Nodes.Num())
		{
			Header += TEXT(" (selecao parcial)");
		}

		Lines.Add(Header);

		// O caminho exato de volta, pronto para copiar.
		//
		// O nome do grafo nao e' adivinhavel (`EventGraph` num asset,
		// `Gameplay Ability Graph` em outro), e a raiz de conteudo tambem nao: um
		// asset de plugin mora em `/NomeDoPlugin/`, nao em `/Game/`. Errar
		// qualquer um dos dois da' a mesma mensagem seca de objeto invalido, que
		// vem da Engine antes deste plugin rodar -- entao a unica forma de nao
		// gastar turnos adivinhando e' o caminho certo estar aqui.
		if (const UObject* Outer = SourceGraph->GetOuter())
		{
			Lines.Add(FString::Printf(TEXT("# refPath: %s:%s"),
				*Outer->GetPathName(), *SourceGraph->GetName()));
		}

		// As variaveis do Blueprint junto. Sem elas, quem le' o texto nao tem
		// como saber se `$Slot` existe, qual o tipo dele, nem que outras ha' --
		// e acaba escrevendo texto que referencia coisa que nao existe.
		UClass* OwnClass = nullptr;
		if (Blueprint)
		{
			// O esqueleto tem as variaveis criadas sem compilar ainda.
			if (Blueprint->SkeletonGeneratedClass)
			{
				OwnClass = Blueprint->SkeletonGeneratedClass;
			}
			else
			{
				OwnClass = Blueprint->GeneratedClass.Get();
			}
		}

		if (OwnClass)
		{
			// Widget do Designer nao se declara: sai como comentario, para quem
			// le' saber que existe sem o texto tentar recria-la ao voltar.
			const UClass* WidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.Widget"));
			const UClass* UserWidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.UserWidget"));

			const bool bIsWidgetBlueprint = UserWidgetClass
				&& Blueprint->ParentClass
				&& Blueprint->ParentClass->IsChildOf(UserWidgetClass);

			TArray<FString> Declarations;
			TArray<FString> DesignerWidgets;

			// ExcludeSuper: so' o que este Blueprint declara. Com a heranca
			// junto seriam centenas de linhas da Engine, e nenhuma util aqui.
			for (TFieldIterator<FProperty> PropertyIt(OwnClass, EFieldIteratorFlags::ExcludeSuper);
				PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
				{
					continue;
				}

				// GetDefault, nao construcao na pilha: classe UObject nao pode
				// ser instanciada assim -- o construtor chama
				// FObjectInitializer::Get(), que so' vale dentro de um
				// construtor de UObject, e derruba o editor na hora.
				FEdGraphPinType PinType;
				if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
				{
					continue;
				}

				const FString TypeName = DescribePinType(PinType);
				if (TypeName.IsEmpty())
				{
					continue;
				}

				bool bIsDesignerWidget = false;
				if (bIsWidgetBlueprint && WidgetClass)
				{
					if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
					{
						bIsDesignerWidget = ObjectProperty->PropertyClass
							&& ObjectProperty->PropertyClass->IsChildOf(WidgetClass);
					}
				}

				if (bIsDesignerWidget)
				{
					DesignerWidgets.Add(FString::Printf(TEXT("#   %s : %s"), *Property->GetName(), *TypeName));
				}
				else
				{
					// O valor padrao vive no CDO da classe compilada -- e' de la'
					// que o painel de detalhes le'. `NewVariables[].DefaultValue`
					// so' serve de semente na criacao e fica vazio depois.
					//
					// Sem ele, colar num Blueprint vazio cria tudo zerado e o
					// grafo se comporta diferente sem nenhum aviso.
					FString DefaultValue;
					if (UClass* CompiledClass = Blueprint->GeneratedClass.Get())
					{
						if (const FProperty* Compiled = CompiledClass->FindPropertyByName(Property->GetFName()))
						{
							if (UObject* DefaultObject = CompiledClass->GetDefaultObject())
							{
								const void* Value = Compiled->ContainerPtrToValuePtr<void>(DefaultObject);

								// Zero do tipo nao acrescenta nada a' linha, e
								// escreveria `= 0` em toda variavel intocada.
								void* Zero = FMemory::Malloc(Compiled->GetSize(), Compiled->GetMinAlignment());
								Compiled->InitializeValue(Zero);
								const bool bIsZero = Compiled->Identical(Value, Zero);
								Compiled->DestroyValue(Zero);
								FMemory::Free(Zero);

								if (!bIsZero)
								{
									Compiled->ExportTextItem_Direct(DefaultValue, Value, nullptr, nullptr, PPF_None);
								}
							}
						}
					}

					FString Line = FString::Printf(TEXT("variavel %s : %s"), *Property->GetName(), *TypeName);

					if (!DefaultValue.IsEmpty())
					{
						FString Quoted;
						if (!NeedsQuotes(DefaultValue))
						{
							Line += TEXT(" = ") + DefaultValue;
						}
						else if (TryQuote(DefaultValue, Quoted))
						{
							Line += TEXT(" = ") + Quoted;
						}
					}

					Declarations.Add(Line);
				}
			}

			if (DesignerWidgets.Num() > 0)
			{
				Lines.Add(TEXT("# do Designer (crie na tela, marcando Is Variable):"));
				Lines.Append(DesignerWidgets);
			}

			Lines.Append(Declarations);
		}

		Lines.Add(FString());
	}

	// Caixas de comentario primeiro: sao contexto, nao passo de execucao.
	TArray<UEdGraphNode*> Comments;
	for (UEdGraphNode* Node : Nodes)
	{
		if (Node && Node->IsA<UEdGraphNode_Comment>() && Scope.Contains(Node))
		{
			Comments.Add(Node);
		}
	}

	Comments.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return A.NodePosY != B.NodePosY ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});

	for (UEdGraphNode* Comment : Comments)
	{
		Scope.Remove(Comment);

		FString Text = Comment->NodeComment;
		Text.ReplaceInline(TEXT("\n"), TEXT(" "));
		Text.TrimStartAndEndInline();

		if (!Text.IsEmpty())
		{
			EmitLine(0, TEXT("Comment ") + Text);
		}
	}

	if (Comments.Num() > 0 && Scope.Num() > 0)
	{
		Lines.Add(FString());
	}

	// Raizes: quem tem fio branco mas nao recebe execucao de ninguem. Eventos
	// caem aqui naturalmente, porque nao tem pino de entrada de execucao.
	TArray<UEdGraphNode*> Roots;
	for (UEdGraphNode* Node : Scope)
	{
		if (!IsExecNode(Node))
		{
			continue;
		}

		// Raiz e' quem nao recebe execucao de dentro do que foi selecionado.
		//
		// Olhar so' "o pino esta' ligado?" quebrava Copiar selecionado: numa
		// selecao no meio da cadeia, o primeiro node recebe execucao de fora
		// dela, entao nenhum node era raiz e o texto saia vazio.
		UEdGraphPin* ExecInput = FindExecInput(Node);
		if (!ExecInput)
		{
			Roots.Add(Node);
			continue;
		}

		UEdGraphPin* ExecSource = FollowToSourcePin(ExecInput);
		if (!ExecSource || !Scope.Contains(ExecSource->GetOwningNode()))
		{
			Roots.Add(Node);
		}
	}

	// Evento sem nada ligado nao diz nada, e colar o texto de volta esbarraria
	// no guard de evento duplicado. Os stubs desabilitados que todo Blueprint
	// novo traz -- BeginPlay, Tick, ActorBeginOverlap -- caem exatamente aqui.
	//
	// Sao anotados porque a contagem de orfaos, la' embaixo, pega tudo que nao
	// virou linha: sem isto eles apareciam como "3 nodes de dado nao alimentam
	// nada" -- que os descreve errado (nao sao de dado) e conta como perda o que
	// foi omitido de proposito. Um evento vazio nao carrega comportamento nenhum.
	TSet<UEdGraphNode*> EventosVazios;

	Roots.RemoveAll([this, &EventosVazios](UEdGraphNode* Node)
	{
		if (!Node->IsA<UK2Node_Event>() || NeedsName.Contains(Node))
		{
			return false;
		}

		for (UEdGraphPin* Pin : GetExecOutputs(Node))
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				return false;
			}
		}

		EventosVazios.Add(Node);
		return true;
	});

	// Quem e' referenciado por outra cadeia sai primeiro: `$aterrissar` so'
	// existe depois da linha que nomeia aquele evento, entao a ordem no texto
	// tem que respeitar a dependencia -- se nao, o texto nao volta.
	//
	// Depois disso, eventos antes do resto e de cima para baixo, que e' a ordem
	// de leitura de quem olha o grafo.
	Roots.Sort([this](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		const bool bAReferenced = NeedsName.Contains(const_cast<UEdGraphNode*>(&A));
		const bool bBReferenced = NeedsName.Contains(const_cast<UEdGraphNode*>(&B));

		if (bAReferenced != bBReferenced)
		{
			return bAReferenced;
		}

		const bool bAEvent = A.IsA<UK2Node_Event>();
		const bool bBEvent = B.IsA<UK2Node_Event>();

		if (bAEvent != bBEvent)
		{
			return bAEvent;
		}

		return A.NodePosY != B.NodePosY ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
	});

	for (int32 Index = 0; Index < Roots.Num(); ++Index)
	{
		if (Index > 0)
		{
			Lines.Add(FString());
		}
		EmitExecChain(Roots[Index], 0);
	}

	// Nodes de dado que ninguem consome nao entram em cadeia nenhuma. Dizer
	// *quais* e' o que transforma a nota em diagnostico: sete getters de widget
	// ligados num pino `Target`, que aceita uma ligacao so', deixam seis soltos
	// -- e a lista de nomes mostra isso na hora, sem uma ida e volta para
	// descobrir quem eram.
	TArray<FString> Orphans;
	for (UEdGraphNode* Node : Scope)
	{
		if (EmittedExec.Contains(Node) || DataNames.Contains(Node))
		{
			continue;
		}

		// Conversao atravessada alimenta sim -- ela so' nao virou linha.
		if (TraversedConversions.Contains(Node))
		{
			continue;
		}

		// Evento vazio foi omitido de proposito, e nao ha' o que perder nele.
		if (EventosVazios.Contains(Node))
		{
			continue;
		}

		Orphans.Add(ShortTitle(Node));
	}

	// Contados a' parte da nota: quem quer apagar o grafo e recolar precisa saber
	// que estes nao voltam, e uma nota nao e' verificavel por codigo.
	Result.LostNodeCount = Orphans.Num();

	if (Orphans.Num() > 0)
	{
		// Uma tela cheia de nomes nao ajuda mais que os primeiros; o que importa
		// e' reconhecer o grupo.
		TArray<FString> Shown = Orphans;
		if (Shown.Num() > 8)
		{
			Shown.SetNum(8);
		}

		FString Names = FString::Join(Shown, TEXT(", "));
		if (Orphans.Num() > Shown.Num())
		{
			Names += FString::Printf(TEXT(" e mais %d"), Orphans.Num() - Shown.Num());
		}

		AddInfo(FString::Printf(
			TEXT("%d node(s) de dado nao alimentam nada e ficaram de fora do texto: %s."),
			Orphans.Num(), *Names));
	}

	StampAnchors();

	Result.Text = FString::Join(Lines, TEXT("\n"));
}

// ---------------------------------------------------------------------------

FNodeScribeReader::FResult FNodeScribeReader::Read(const TArray<UEdGraphNode*>& Nodes, UBlueprint* Blueprint)
{
	FNodeScribeReadContext Context(Nodes, Blueprint);
	Context.Run();
	return MoveTemp(Context.Result);
}

FNodeScribeReader::FResult FNodeScribeReader::ReadGraph(UEdGraph* Graph, UBlueprint* Blueprint)
{
	if (!Graph)
	{
		FResult Empty;
		Empty.Diagnostics.Emplace(ENodeScribeSeverity::Error, 0, TEXT("Nenhum grafo aberto."));
		Empty.ErrorCount = 1;
		return Empty;
	}

	return Read(Graph->Nodes, Blueprint);
}
