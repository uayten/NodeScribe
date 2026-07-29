#include "NodeScribeReader.h"

#include "NodeScribeCatalog.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
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

	void EmitExecChain(UEdGraphNode* Node, int32 Indent);
	void EmitNodeLine(UEdGraphNode* Node, int32 Indent, const FString& AssignedName);

	/** Garante que o node de dado ja' tenha linha e nome, e devolve `$nome`. */
	FString EnsureDataReference(UEdGraphNode* Node, int32 Indent);

	FString DescribeNode(UEdGraphNode* Node, bool& bOutRoundTrips);
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

FString FNodeScribeReadContext::DescribeNode(UEdGraphNode* Node, bool& bOutRoundTrips)
{
	bOutRoundTrips = true;

	// CustomEvent antes de Event: o primeiro deriva do segundo.
	if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
	{
		return TEXT("event ") + CustomEvent->CustomFunctionName.ToString();
	}

	if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
	{
		return TEXT("event ") + FNodeScribeCatalog::StripEventPrefix(Event->EventReference.GetMemberName().ToString());
	}

	if (Node->IsA<UK2Node_IfThenElse>())
	{
		return TEXT("Branch");
	}

	if (Node->IsA<UK2Node_ExecutionSequence>())
	{
		return TEXT("Sequence");
	}

	if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		if (UClass* TargetType = CastNode->TargetType)
		{
			FString ClassName = TargetType->GetName();
			ClassName.RemoveFromEnd(TEXT("_C"));
			return TEXT("Cast to ") + ClassName;
		}

		bOutRoundTrips = false;
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

	if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
	{
		if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
		{
			return MacroGraph->GetName();
		}

		bOutRoundTrips = false;
		return TEXT("Macro ?");
	}

	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (const UFunction* Function = Call->GetTargetFunction())
		{
			return Function->GetDisplayNameText().ToString();
		}
	}

	// Sobrou algo que o builder nao sabe recriar a partir do nome.
	bOutRoundTrips = false;
	return ShortTitle(Node);
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

			const FString Reference = EnsureDataReference(SourceNode, Indent);
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

FString FNodeScribeReadContext::EnsureDataReference(UEdGraphNode* Node, int32 Indent)
{
	if (const FString* Existing = DataNames.Find(Node))
	{
		return TEXT("$") + *Existing;
	}

	// Get de variavel propria nao precisa de linha: `$Vida` sozinho ja' faz o
	// builder criar o node. Uma linha a menos por variavel usada.
	if (const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
	{
		if (Getter->VariableReference.IsSelfContext())
		{
			const FString VariableName = Getter->VariableReference.GetMemberName().ToString();
			DataNames.Add(Node, VariableName);
			UsedNames.Add(VariableName);
			return TEXT("$") + VariableName;
		}
	}

	// Um node de dado alimentado por outro precisa que o de tras venha antes.
	bool bRoundTrips = true;
	const FString Expression = DescribeNode(Node, bRoundTrips);
	const FString Arguments = BuildArgumentList(Node, Indent);

	const FString Name = MakeUniqueName(Expression);
	DataNames.Add(Node, Name);

	if (!bRoundTrips)
	{
		AddWarning(FString::Printf(
			TEXT("Nao sei escrever `%s` de um jeito que volte igual. ")
			TEXT("A linha saiu com o titulo do node; confira antes de reusar."),
			*ShortTitle(Node)));
	}

	EmitLine(Indent, FString::Printf(TEXT("%s = %s%s"), *Name, *Expression, *Arguments));
	++Result.NodeCount;

	return TEXT("$") + Name;
}

// ---------------------------------------------------------------------------
// Cadeia de execucao
// ---------------------------------------------------------------------------

void FNodeScribeReadContext::EmitNodeLine(UEdGraphNode* Node, int32 Indent, const FString& AssignedName)
{
	bool bRoundTrips = true;
	const FString Expression = DescribeNode(Node, bRoundTrips);

	if (!bRoundTrips)
	{
		AddWarning(FString::Printf(
			TEXT("Nao sei escrever `%s` de um jeito que volte igual. ")
			TEXT("A linha saiu com o titulo do node; confira antes de reusar."),
			*ShortTitle(Node)));
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

	// Um node de execucao com saida de dado nomeada e' reusado adiante; so'
	// damos nome quando alguem de fato consome, o que o builder decide sozinho.
	EmitNodeLine(Node, Indent, DataNames.FindRef(Node));

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

void FNodeScribeReadContext::Run()
{
	if (Scope.Num() == 0)
	{
		AddInfo(TEXT("Nada para ler: nenhum node selecionado."));
		return;
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

		UEdGraphPin* ExecInput = FindExecInput(Node);
		if (!ExecInput || FollowToSourcePin(ExecInput) == nullptr)
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
