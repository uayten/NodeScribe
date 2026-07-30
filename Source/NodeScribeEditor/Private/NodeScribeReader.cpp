#include "NodeScribeReader.h"

#include "NodeScribeCatalog.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CustomEvent.h"
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
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

namespace
{
	/** Dois espacos por nivel: e' o que FNodeScribeParser::MeasureIndent conta. */
	const TCHAR* const IndentUnit = TEXT("  ");

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/**
	 * Nodes de reroute nao existem no formato de texto -- sao decoracao de
	 * layout. Atravessamos ate' o pino de verdade em vez de emitir uma linha
	 * que o builder nao saberia recriar.
	 */
	UEdGraphPin* FollowToSourcePin(UEdGraphPin* InputPin)
	{
		UEdGraphPin* Current = InputPin;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Source = Current->LinkedTo[0];
			if (!Source)
			{
				return nullptr;
			}

			const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Source->GetOwningNode());
			if (!Knot)
			{
				return Source;
			}

			Current = Knot->GetInputPin();
		}

		return nullptr;
	}

	/** Idem, no sentido da execucao. */
	UEdGraphNode* FollowExecTarget(UEdGraphPin* ExecOutput)
	{
		UEdGraphPin* Current = ExecOutput;

		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphPin* Next = Current->LinkedTo[0];
			if (!Next)
			{
				return nullptr;
			}

			UEdGraphNode* Node = Next->GetOwningNode();
			const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node);
			if (!Knot)
			{
				return Node;
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

	/** O nome que o usuario ve' e digita, que raramente e' o nome interno. */
	FString GetWrittenPinName(const UEdGraphPin* Pin)
	{
		if (!Pin->PinFriendlyName.IsEmpty())
		{
			return Pin->PinFriendlyName.ToString();
		}
		return Pin->PinName.ToString();
	}

	bool IsObjectLikePin(const UEdGraphPin* Pin)
	{
		const FName Category = Pin->PinType.PinCategory;
		return Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_SoftClass
			|| Category == UEdGraphSchema_K2::PC_Interface;
	}

	/**
	 * O parser nao tem sequencia de escape: uma aspa dentro do valor quebraria a
	 * linha. Escolhemos a aspa que nao aparece no texto; se as duas aparecerem,
	 * quem chama avisa em vez de emitir algo que nao volta.
	 */
	bool TryQuote(const FString& Value, FString& OutQuoted)
	{
		const bool bHasDouble = Value.Contains(TEXT("\""));
		const bool bHasSingle = Value.Contains(TEXT("'"));

		if (bHasDouble && bHasSingle)
		{
			return false;
		}

		const TCHAR* Quote = bHasDouble ? TEXT("'") : TEXT("\"");
		OutQuoted = Quote + Value + Quote;
		return true;
	}

	/** Caracteres que mudariam o sentido da linha se ficassem soltos. */
	bool NeedsQuotes(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return true;
		}

		// Caminho de asset e' lido cru pelo builder e nao tem separador dentro.
		if (Value.StartsWith(TEXT("/")) && !Value.Contains(TEXT(" ")))
		{
			return false;
		}

		if (Value.StartsWith(TEXT("$")))
		{
			return true;
		}

		for (const TCHAR C : Value)
		{
			if (C == TEXT(' ') || C == TEXT(',') || C == TEXT('(') || C == TEXT(')')
				|| C == TEXT('#') || C == TEXT('=') || C == TEXT('"') || C == TEXT('\''))
			{
				return true;
			}
		}

		return Value.Contains(TEXT("//"));
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
			if (Lookup.Function == Function)
			{
				return DisplayName;
			}

			if (const UClass* Owner = Function->GetOwnerClass())
			{
				return Owner->GetName() + TEXT(".") + Function->GetName();
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
	const FString Friendly = GetWrittenPinName(Pin);
	const FString Internal = Pin->PinName.ToString();

	static const TMap<FString, FString> KnownFriendly = {
		{ TEXT("then"), TEXT("True") },
		{ TEXT("else"), TEXT("False") }
	};

	if (const FString* Preferred = KnownFriendly.Find(FNodeScribeCatalog::Normalize(Internal)))
	{
		return *Preferred;
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
		return FString();
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

		if (UEdGraphPin* SourcePin = FollowToSourcePin(Pin))
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

			const FString Reference = MakeReferenceTo(SourcePin, Indent);
			if (!Reference.IsEmpty())
			{
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
		if (EmittedExec.Contains(SourceNode))
		{
			// A pre-passagem deveria ter nomeado esse node antes de emiti-lo.
			// Emitir a linha de novo aqui criaria um segundo node ao colar.
			AddWarning(FString::Printf(
				TEXT("`%s` ja' apareceu na cadeia de execucao e nao recebeu nome. ")
				TEXT("Essa ligacao de dado saiu do texto -- religue na mao."),
				*ShortTitle(SourceNode)));
			return FString();
		}

		Name = EmitDataNode(SourceNode, Indent);
	}

	FString Token = TEXT("$") + Name;

	// Node com varias saidas de dado precisa dizer qual delas: `$nome.Pino`.
	if (SourcePin != FindPrimaryOutput(SourceNode))
	{
		Token += TEXT(".") + GetWrittenPinName(SourcePin);
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

	// O nome tem que ser decidido antes da linha sair, mesmo que quem consome
	// so' apareca bem mais adiante no texto.
	FString AssignedName;
	if (NeedsName.Contains(Node) && !DataNames.Contains(Node))
	{
		AssignedName = MakeUniqueName(MakeNameBase(Expression));
		DataNames.Add(Node, AssignedName);
	}

	// Os argumentos podem emitir linhas de dado, que precisam vir antes desta.
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Prefix = AssignedName.IsEmpty()
		? FString()
		: AssignedName + TEXT(" = ");

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
		// dizer "volta para aquele ali", entao paramos e falamos.
		AddWarning(FString::Printf(
			TEXT("A execucao volta a `%s`, que ja' apareceu antes. ")
			TEXT("O formato de texto so' descreve arvore: essa reconvergencia se perde e voce religa na mao."),
			*ShortTitle(Node)));
		return;
	}

	EmittedExec.Add(Node);
	EmitNodeLine(Node, Indent);

	const TArray<UEdGraphPin*> ExecOutputs = GetExecOutputs(Node);

	if (ExecOutputs.Num() == 1)
	{
		EmitExecChain(FollowExecTarget(ExecOutputs[0]), Indent);
		return;
	}

	for (UEdGraphPin* Output : ExecOutputs)
	{
		UEdGraphNode* Target = FollowExecTarget(Output);
		if (!Target)
		{
			continue;
		}

		EmitLine(Indent, DescribeExecLabel(Output) + TEXT(":"));
		EmitExecChain(Target, Indent + 1);
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

			UEdGraphPin* SourcePin = FollowToSourcePin(Pin);
			if (!SourcePin)
			{
				continue;
			}

			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

			// Nodes puros ganham nome naturalmente na hora em que sao emitidos.
			// O caso que precisa de aviso previo e' o node de execucao, cuja
			// linha ja' teria passado quando o consumidor aparece.
			if (Scope.Contains(SourceNode) && IsExecNode(SourceNode))
			{
				NeedsName.Add(SourceNode);
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

	// Eventos antes do resto, depois de cima para baixo: e' a ordem de leitura
	// de quem olha o grafo.
	Roots.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
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

	// Nodes de dado que ninguem consome nao entram em cadeia nenhuma. Em vez de
	// perde-los em silencio, avisamos quantos ficaram de fora.
	int32 Orphans = 0;
	for (UEdGraphNode* Node : Scope)
	{
		if (!EmittedExec.Contains(Node) && !DataNames.Contains(Node))
		{
			++Orphans;
		}
	}

	if (Orphans > 0)
	{
		AddInfo(FString::Printf(
			TEXT("%d node(s) de dado nao alimentam nada e ficaram de fora do texto."), Orphans));
	}

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
