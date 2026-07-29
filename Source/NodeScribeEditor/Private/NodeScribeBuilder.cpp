#include "NodeScribeBuilder.h"

#include "NodeScribeCatalog.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Espacamento horizontal entre nodes de uma mesma cadeia de execucao. */
	constexpr int32 ColumnWidth = 340;

	/** Espacamento vertical entre blocos irmaos (os ramos de um Branch). */
	constexpr int32 BranchRowHeight = 300;

	/** Deslocamento de um node de dado criado implicitamente para o seu consumidor. */
	constexpr int32 DataNodeOffsetX = -260;
	constexpr int32 DataNodeOffsetY = 130;

	const TCHAR* const StandardMacrosPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros");

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/** Um pino cujo valor so' pode vir de uma escolha de asset, nao de texto. */
	bool IsObjectLikePin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return false;
		}

		const FName Category = Pin->PinType.PinCategory;
		return Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_SoftClass
			|| Category == UEdGraphSchema_K2::PC_Interface;
	}

	/** Busca de classe por nome curto, aceitando tanto `BP_Boss` quanto `BP_Boss_C`. */
	UClass* FindClassByFriendlyName(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		const FString NormalizedWithSuffix = Normalized + TEXT("c");

		UClass* Fallback = nullptr;

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (Class->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				continue;
			}

			const FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")) || ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

			const FString NormalizedClassName = FNodeScribeCatalog::Normalize(ClassName);

			if (NormalizedClassName == Normalized)
			{
				return Class;
			}

			// `BP_Boss` digitado pelo usuario aponta para a classe gerada `BP_Boss_C`.
			if (NormalizedClassName == NormalizedWithSuffix && !Fallback)
			{
				Fallback = Class;
			}
		}

		return Fallback;
	}

	/** Acha um grafo de macro da biblioteca padrao (ForEachLoop, DoOnce, Gate...). */
	UEdGraph* FindStandardMacroGraph(const FString& Query)
	{
		UBlueprint* MacroLibrary = LoadObject<UBlueprint>(nullptr, StandardMacrosPath);
		if (!MacroLibrary)
		{
			return nullptr;
		}

		const FString Normalized = FNodeScribeCatalog::Normalize(Query);

		for (UEdGraph* MacroGraph : MacroLibrary->MacroGraphs)
		{
			if (MacroGraph && FNodeScribeCatalog::Normalize(MacroGraph->GetName()) == Normalized)
			{
				return MacroGraph;
			}
		}

		return nullptr;
	}

	/**
	 * Apelidos PT/EN para rotulos de saida de execucao.
	 * Mapeia para o nome interno do pino, que quase nunca e' o que se ve na tela:
	 * o pino "True" de um Branch se chama, internamente, "then".
	 */
	FString ResolveLabelAlias(const FString& Label)
	{
		static const TMap<FString, FString> Aliases = {
			{ TEXT("verdadeiro"), TEXT("then") },
			{ TEXT("true"),       TEXT("then") },
			{ TEXT("sim"),        TEXT("then") },
			{ TEXT("entao"),      TEXT("then") },
			{ TEXT("then"),       TEXT("then") },
			{ TEXT("depois"),     TEXT("then") },
			{ TEXT("falso"),      TEXT("else") },
			{ TEXT("false"),      TEXT("else") },
			{ TEXT("nao"),        TEXT("else") },
			{ TEXT("senao"),      TEXT("else") },
			{ TEXT("else"),       TEXT("else") },
			{ TEXT("corpo"),      TEXT("loopbody") },
			{ TEXT("cada"),       TEXT("loopbody") },
			{ TEXT("loop"),       TEXT("loopbody") },
			{ TEXT("loopbody"),   TEXT("loopbody") },
			{ TEXT("completo"),   TEXT("completed") },
			{ TEXT("concluido"),  TEXT("completed") },
			{ TEXT("completed"),  TEXT("completed") },
		};

		const FString Normalized = FNodeScribeCatalog::Normalize(Label);
		if (const FString* Found = Aliases.Find(Normalized))
		{
			return *Found;
		}

		return Normalized;
	}

	/** Apelidos para nomes de pino de entrada. */
	FString ResolvePinAlias(const FString& PinName)
	{
		static const TMap<FString, FString> Aliases = {
			{ TEXT("alvo"),      TEXT("self") },
			{ TEXT("target"),    TEXT("self") },
			{ TEXT("self"),      TEXT("self") },
			{ TEXT("condicao"),  TEXT("condition") },
			{ TEXT("condition"), TEXT("condition") },
		};

		const FString Normalized = FNodeScribeCatalog::Normalize(PinName);
		if (const FString* Found = Aliases.Find(Normalized))
		{
			return *Found;
		}

		return Normalized;
	}
}

/**
 * Referencia estavel a um pino.
 *
 * Guardar `UEdGraphPin*` cru nao serve: ligar um pino faz o node avisar seus
 * vizinhos, e alguns K2Nodes (wildcard, cast) se reconstroem nesse momento,
 * destruindo os pinos antigos. Um ponteiro guardado de uma linha anterior
 * viraria acesso a memoria liberada. Guardamos node + nome e resolvemos na hora.
 */
struct FPinRef
{
	TWeakObjectPtr<UEdGraphNode> Node;
	FName PinName;
	EEdGraphPinDirection Direction = EGPD_Output;

	FPinRef() = default;

	explicit FPinRef(UEdGraphPin* Pin)
	{
		if (Pin && Pin->GetOwningNodeUnchecked())
		{
			Node = Pin->GetOwningNode();
			PinName = Pin->PinName;
			Direction = Pin->Direction;
		}
	}

	bool IsSet() const { return Node.IsValid() && !PinName.IsNone(); }

	UEdGraphPin* Resolve() const
	{
		UEdGraphNode* OwningNode = Node.Get();
		return OwningNode ? OwningNode->FindPin(PinName, Direction) : nullptr;
	}
};

/** Estado de uma transcricao. Vive apenas durante Build(). */
class FNodeScribeBuildContext
{
public:
	FNodeScribeBuildContext(UEdGraph* InGraph, UBlueprint* InBlueprint, const FVector2D& InOrigin)
		: Graph(InGraph)
		, Blueprint(InBlueprint)
		, Origin(InOrigin)
		, Schema(GetDefault<UEdGraphSchema_K2>())
	{
	}

	void Run(const TArray<FNodeScribeStatement>& Statements);

	FNodeScribeBuilder::FResult Result;

private:
	/** Um nivel de aninhamento: o corpo de um ramo, ou o nivel raiz. */
	struct FFrame
	{
		int32 Indent = 0;

		/** De onde sai a proxima ligacao de execucao. Invalido = cadeia interrompida. */
		FPinRef PendingExec;

		/** Ultimo node criado neste nivel, dono dos rotulos que vierem a seguir. */
		UEdGraphNode* LastNode = nullptr;

		int32 BaseX = 0;
		int32 BaseY = 0;
		int32 Column = 0;

		/** Quantos ramos ja' foram abertos a partir de LastNode, para empilhar em Y. */
		int32 BranchesOpened = 0;
	};

	// --- Criacao de nodes -------------------------------------------------

	template <typename TNode>
	TNode* AllocateNode()
	{
		TNode* Node = NewObject<TNode>(Graph);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		return Node;
	}

	static void FinalizeNode(UEdGraphNode* Node)
	{
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
	}

	UEdGraphNode* CreateNodeForStatement(const FNodeScribeStatement& Statement);
	UEdGraphNode* TryCreateSpecialNode(const FNodeScribeStatement& Statement, bool& bOutHandled);
	UEdGraphNode* CreateErrorComment(const FNodeScribeStatement& Statement, const FString& Reason);

	// --- Ligacao ----------------------------------------------------------

	void ApplyArguments(UEdGraphNode* Node, const FNodeScribeStatement& Statement);
	void ApplyLiteral(UEdGraphPin* Pin, const FString& Value, int32 Line);

	UEdGraphPin* ResolveReference(const FString& Name, int32 Line, UEdGraphNode* Consumer);
	void RegisterOutput(const FString& Name, UEdGraphNode* Node, int32 Line);

	UEdGraphPin* FindPinByFuzzyName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction) const;
	static UEdGraphPin* FindExecInput(UEdGraphNode* Node);
	static TArray<UEdGraphPin*> GetExecOutputs(UEdGraphNode* Node);
	static UEdGraphPin* FindPrimaryOutput(UEdGraphNode* Node);

	void Connect(UEdGraphPin* From, UEdGraphPin* To, int32 Line);

	// --- Diagnosticos -----------------------------------------------------

	void AddInfo(int32 Line, const FString& Message);
	void AddWarning(int32 Line, const FString& Message);
	void AddError(int32 Line, const FString& Message);

	UClass* GetSelfClass() const;

	/** true se o Blueprint (ou um pai dele) tem uma variavel visivel com esse nome. */
	bool IsBlueprintVariable(const FString& Name) const;

	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	FVector2D Origin = FVector2D::ZeroVector;
	const UEdGraphSchema_K2* Schema = nullptr;

	TArray<FFrame> Frames;

	/** Saidas nomeadas com `nome = ...`, disponiveis para `$nome`. */
	TMap<FString, FPinRef> NamedOutputs;
};

// ---------------------------------------------------------------------------
// Diagnosticos
// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::AddInfo(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Info, Line, Message);
}

void FNodeScribeBuildContext::AddWarning(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Warning, Line, Message);
	++Result.WarningCount;
}

void FNodeScribeBuildContext::AddError(int32 Line, const FString& Message)
{
	Result.Diagnostics.Emplace(ENodeScribeSeverity::Error, Line, Message);
	++Result.ErrorCount;
}

UClass* FNodeScribeBuildContext::GetSelfClass() const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	return Blueprint->GeneratedClass ? Blueprint->GeneratedClass.Get() : Blueprint->ParentClass.Get();
}

bool FNodeScribeBuildContext::IsBlueprintVariable(const FString& Name) const
{
	if (Name.IsEmpty())
	{
		return false;
	}

	// Olhar a classe (e nao so' a lista de variaveis do Blueprint) faz com que
	// variaveis herdadas de um pai em C++ tambem sejam reconhecidas.
	UClass* SelfClass = GetSelfClass();
	if (!SelfClass)
	{
		return false;
	}

	const FProperty* Property = SelfClass->FindPropertyByName(FName(*Name));
	return Property != nullptr && Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
}

// ---------------------------------------------------------------------------
// Pinos
// ---------------------------------------------------------------------------

UEdGraphPin* FNodeScribeBuildContext::FindExecInput(UEdGraphNode* Node)
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

TArray<UEdGraphPin*> FNodeScribeBuildContext::GetExecOutputs(UEdGraphNode* Node)
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

UEdGraphPin* FNodeScribeBuildContext::FindPrimaryOutput(UEdGraphNode* Node)
{
	// `ReturnValue` e' o nome canonico do resultado de uma chamada de funcao.
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

UEdGraphPin* FNodeScribeBuildContext::FindPinByFuzzyName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction) const
{
	const FString Wanted = ResolvePinAlias(Name);

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction != Direction || Pin->bHidden)
		{
			continue;
		}

		if (FNodeScribeCatalog::Normalize(Pin->PinName.ToString()) == Wanted)
		{
			return Pin;
		}

		if (!Pin->PinFriendlyName.IsEmpty()
			&& FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString()) == Wanted)
		{
			return Pin;
		}
	}

	return nullptr;
}

void FNodeScribeBuildContext::Connect(UEdGraphPin* From, UEdGraphPin* To, int32 Line)
{
	if (!From || !To)
	{
		return;
	}

	// TryCreateConnection do schema K2 insere sozinho um node de conversao
	// quando os tipos sao compativeis por cast implicito (int -> float, etc).
	if (!Schema->TryCreateConnection(From, To))
	{
		AddWarning(Line, FString::Printf(
			TEXT("Nao consegui ligar `%s` em `%s` (tipos incompativeis). Ligue na mao."),
			*From->PinName.ToString(), *To->PinName.ToString()));
	}
}

// ---------------------------------------------------------------------------
// Valores e referencias
// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::ApplyLiteral(UEdGraphPin* Pin, const FString& Value, int32 Line)
{
	// `?` e' um buraco declarado: "essa escolha e' sua, nao minha".
	// Deixamos o pino vazio de proposito -- se for obrigatorio, o Blueprint
	// nao compila, e a pendencia aparece sozinha em vez de virar bug silencioso.
	if (Value == TEXT("?") || Value.StartsWith(TEXT("?")))
	{
		AddWarning(Line, FString::Printf(
			TEXT("Pino `%s` ficou vazio esperando sua escolha."), *Pin->PinName.ToString()));
		return;
	}

	if (IsObjectLikePin(Pin))
	{
		if (Value.StartsWith(TEXT("/")))
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *Value))
			{
				Schema->TrySetDefaultObject(*Pin, Asset);
				return;
			}

			AddWarning(Line, FString::Printf(
				TEXT("Nao achei o asset `%s`. Escolha no pino `%s`."), *Value, *Pin->PinName.ToString()));
			return;
		}

		// Sem caminho completo nao da' para saber qual asset e'. Nao chutamos.
		AddWarning(Line, FString::Printf(
			TEXT("`%s` nao e' um caminho de asset. Escolha no pino `%s` do node."),
			*Value, *Pin->PinName.ToString()));
		return;
	}

	Schema->TrySetDefaultValue(*Pin, Value);
}

UEdGraphPin* FNodeScribeBuildContext::ResolveReference(const FString& Name, int32 Line, UEdGraphNode* Consumer)
{
	if (const FPinRef* Existing = NamedOutputs.Find(Name))
	{
		if (UEdGraphPin* Pin = Existing->Resolve())
		{
			return Pin;
		}
	}

	// Conveniencia: `$Health` sem declaracao previa vira um Get da variavel do
	// Blueprint, criado e posicionado automaticamente.
	if (IsBlueprintVariable(Name))
	{
		UK2Node_VariableGet* GetNode = AllocateNode<UK2Node_VariableGet>();
		GetNode->VariableReference.SetSelfMember(FName(*Name));
		FinalizeNode(GetNode);

		if (Consumer)
		{
			GetNode->NodePosX = Consumer->NodePosX + DataNodeOffsetX;
			GetNode->NodePosY = Consumer->NodePosY + DataNodeOffsetY;
		}

		Result.CreatedNodes.Add(GetNode);

		UEdGraphPin* OutputPin = FindPrimaryOutput(GetNode);
		if (OutputPin)
		{
			NamedOutputs.Add(Name, FPinRef(OutputPin));
		}

		return OutputPin;
	}

	AddError(Line, FString::Printf(
		TEXT("`$%s` nao existe: nao e' saida de nenhuma linha anterior nem variavel deste Blueprint."), *Name));

	return nullptr;
}

void FNodeScribeBuildContext::RegisterOutput(const FString& Name, UEdGraphNode* Node, int32 Line)
{
	UEdGraphPin* OutputPin = FindPrimaryOutput(Node);
	if (!OutputPin)
	{
		AddWarning(Line, FString::Printf(
			TEXT("`%s` nao foi registrado: esse node nao tem saida de dado."), *Name));
		return;
	}

	if (NamedOutputs.Contains(Name))
	{
		AddWarning(Line, FString::Printf(TEXT("`%s` foi definido de novo; vale o ultimo."), *Name));
	}

	NamedOutputs.Add(Name, FPinRef(OutputPin));
}

void FNodeScribeBuildContext::ApplyArguments(UEdGraphNode* Node, const FNodeScribeStatement& Statement)
{
	// Pinos de entrada elegiveis, na ordem, para resolver argumentos posicionais.
	TArray<UEdGraphPin*> PositionalPins;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == EGPD_Input && !IsExecPin(Pin) && !Pin->bHidden)
		{
			PositionalPins.Add(Pin);
		}
	}

	int32 NextPositional = 0;

	for (const FNodeScribeArg& Arg : Statement.Args)
	{
		UEdGraphPin* Pin = nullptr;

		if (!Arg.PinName.IsEmpty())
		{
			Pin = FindPinByFuzzyName(Node, Arg.PinName, EGPD_Input);

			if (!Pin)
			{
				TArray<FString> Available;
				for (UEdGraphPin* Candidate : PositionalPins)
				{
					Available.Add(Candidate->PinName.ToString());
				}

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("O node nao tem pino `%s`. Pinos de entrada: %s"),
					*Arg.PinName, *FString::Join(Available, TEXT(", "))));
				continue;
			}
		}
		else
		{
			if (!PositionalPins.IsValidIndex(NextPositional))
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Argumento a mais (`%s`): o node nao tem tantos pinos de entrada."), *Arg.Value));
				continue;
			}

			Pin = PositionalPins[NextPositional++];
		}

		if (Arg.bIsReference)
		{
			if (UEdGraphPin* Source = ResolveReference(Arg.Value, Statement.LineNumber, Node))
			{
				Connect(Source, Pin, Statement.LineNumber);
			}
		}
		else
		{
			ApplyLiteral(Pin, Arg.Value, Statement.LineNumber);
		}
	}
}

// ---------------------------------------------------------------------------
// Criacao de nodes
// ---------------------------------------------------------------------------

UEdGraphNode* FNodeScribeBuildContext::CreateErrorComment(const FNodeScribeStatement& Statement, const FString& Reason)
{
	UEdGraphNode_Comment* Comment = AllocateNode<UEdGraphNode_Comment>();
	FinalizeNode(Comment);

	Comment->NodeComment = FString::Printf(TEXT("NodeScribe nao resolveu:\n%s\n\n%s"), *Statement.RawLine, *Reason);
	Comment->CommentColor = FLinearColor(0.65f, 0.12f, 0.12f);
	Comment->NodeWidth = 460;
	Comment->NodeHeight = 160;
	Comment->bCommentBubbleVisible = false;

	// Quem registra em CreatedNodes e' o Run(), que recebe este node de volta
	// como resultado da linha. Adicionar aqui duplicaria a contagem.
	return Comment;
}

UEdGraphNode* FNodeScribeBuildContext::TryCreateSpecialNode(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = true;

	const FString Expression = Statement.NodeExpression.TrimStartAndEnd();
	const FString Normalized = FNodeScribeCatalog::Normalize(Expression);

	// --- Branch -----------------------------------------------------------
	if (Normalized == TEXT("branch") || Normalized == TEXT("if") || Normalized == TEXT("se"))
	{
		UK2Node_IfThenElse* Node = AllocateNode<UK2Node_IfThenElse>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Sequence ---------------------------------------------------------
	if (Normalized == TEXT("sequence") || Normalized == TEXT("sequencia"))
	{
		UK2Node_ExecutionSequence* Node = AllocateNode<UK2Node_ExecutionSequence>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Return -----------------------------------------------------------
	if (Normalized == TEXT("return") || Normalized == TEXT("retornar") || Normalized == TEXT("retorno"))
	{
		UK2Node_FunctionResult* Node = AllocateNode<UK2Node_FunctionResult>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Self -------------------------------------------------------------
	if (Normalized == TEXT("self") || Normalized == TEXT("eu"))
	{
		UK2Node_Self* Node = AllocateNode<UK2Node_Self>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Comentario livre -------------------------------------------------
	if (Expression.StartsWith(TEXT("Comment "), ESearchCase::IgnoreCase)
		|| Expression.StartsWith(TEXT("Comentario "), ESearchCase::IgnoreCase))
	{
		const int32 SpaceIndex = Expression.Find(TEXT(" "));
		UEdGraphNode_Comment* Node = AllocateNode<UEdGraphNode_Comment>();
		FinalizeNode(Node);
		Node->NodeComment = Expression.Mid(SpaceIndex + 1);
		Node->NodeWidth = 400;
		Node->NodeHeight = 140;
		return Node;
	}

	// --- Cast to <Classe> -------------------------------------------------
	{
		FString ClassName;
		if (Expression.StartsWith(TEXT("Cast to "), ESearchCase::IgnoreCase))
		{
			ClassName = Expression.RightChop(8);
		}
		else if (Expression.StartsWith(TEXT("Converter para "), ESearchCase::IgnoreCase))
		{
			ClassName = Expression.RightChop(15);
		}

		if (!ClassName.IsEmpty())
		{
			ClassName.TrimStartAndEndInline();
			UClass* TargetClass = FindClassByFriendlyName(ClassName);

			if (!TargetClass)
			{
				return CreateErrorComment(Statement,
					FString::Printf(TEXT("Nao achei a classe `%s`."), *ClassName));
			}

			UK2Node_DynamicCast* Node = AllocateNode<UK2Node_DynamicCast>();
			Node->TargetType = TargetClass;
			Node->SetPurity(false);
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- Event <Nome> -----------------------------------------------------
	{
		FString EventName;
		if (Expression.StartsWith(TEXT("Event "), ESearchCase::IgnoreCase))
		{
			EventName = Expression.RightChop(6);
		}
		else if (Expression.StartsWith(TEXT("Evento "), ESearchCase::IgnoreCase))
		{
			EventName = Expression.RightChop(7);
		}

		if (!EventName.IsEmpty())
		{
			EventName.TrimStartAndEndInline();

			UFunction* EventFunction = nullptr;
			if (UClass* ParentClass = Blueprint ? Blueprint->ParentClass.Get() : nullptr)
			{
				// A Engine prefixa os eventos implementaveis; o usuario escreve `BeginPlay`,
				// a funcao de verdade se chama `ReceiveBeginPlay`.
				const TArray<FString> Attempts = {
					EventName,
					FString(TEXT("Receive")) + EventName,
					FString(TEXT("K2_")) + EventName
				};

				for (const FString& Attempt : Attempts)
				{
					if (UFunction* Found = ParentClass->FindFunctionByName(FName(*Attempt)))
					{
						if (Found->HasAnyFunctionFlags(FUNC_BlueprintEvent))
						{
							EventFunction = Found;
							break;
						}
					}
				}
			}

			if (EventFunction)
			{
				UK2Node_Event* Node = AllocateNode<UK2Node_Event>();
				Node->EventReference.SetExternalMember(EventFunction->GetFName(), EventFunction->GetOwnerClass());
				Node->bOverrideFunction = true;
				FinalizeNode(Node);
				return Node;
			}

			UK2Node_CustomEvent* Node = AllocateNode<UK2Node_CustomEvent>();
			Node->CustomFunctionName = FName(*EventName);
			FinalizeNode(Node);

			AddInfo(Statement.LineNumber, FString::Printf(
				TEXT("`%s` nao existe na classe pai; criei um Custom Event com esse nome."), *EventName));
			return Node;
		}
	}

	// --- Get / Set de variavel -------------------------------------------
	{
		FString VariableName;
		bool bIsSetter = false;

		if (Expression.StartsWith(TEXT("Get "), ESearchCase::IgnoreCase))
		{
			VariableName = Expression.RightChop(4);
		}
		else if (Expression.StartsWith(TEXT("Set "), ESearchCase::IgnoreCase))
		{
			VariableName = Expression.RightChop(4);
			bIsSetter = true;
		}
		else if (Expression.StartsWith(TEXT("Definir "), ESearchCase::IgnoreCase))
		{
			VariableName = Expression.RightChop(8);
			bIsSetter = true;
		}

		VariableName.TrimStartAndEndInline();

		// So' trata como variavel se ela existir de fato. Assim `Get Player Controller`
		// continua caindo no catalogo de funcoes, que e' onde ele mora.
		if (IsBlueprintVariable(VariableName))
		{
			if (bIsSetter)
			{
				UK2Node_VariableSet* Node = AllocateNode<UK2Node_VariableSet>();
				Node->VariableReference.SetSelfMember(FName(*VariableName));
				FinalizeNode(Node);
				return Node;
			}

			UK2Node_VariableGet* Node = AllocateNode<UK2Node_VariableGet>();
			Node->VariableReference.SetSelfMember(FName(*VariableName));
			FinalizeNode(Node);
			return Node;
		}
	}

	bOutHandled = false;
	return nullptr;
}

UEdGraphNode* FNodeScribeBuildContext::CreateNodeForStatement(const FNodeScribeStatement& Statement)
{
	bool bHandled = false;
	if (UEdGraphNode* Special = TryCreateSpecialNode(Statement, bHandled))
	{
		return Special;
	}

	if (bHandled)
	{
		// Um handler especial reconheceu a forma mas ja' reportou o problema.
		return nullptr;
	}

	// Macros da biblioteca padrao (ForEachLoop, DoOnce, Gate...).
	// Quando a linha nomeia uma saida (`x = ...`), o usuario quer um valor,
	// entao a funcao pura tem prioridade sobre a macro de mesmo nome.
	if (Statement.OutputName.IsEmpty())
	{
		if (UEdGraph* MacroGraph = FindStandardMacroGraph(Statement.NodeExpression))
		{
			UK2Node_MacroInstance* Node = AllocateNode<UK2Node_MacroInstance>();
			Node->MacroGraphReference.SetGraph(MacroGraph);
			FinalizeNode(Node);
			return Node;
		}
	}

	const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(
		Statement.NodeExpression, GetSelfClass(), nullptr);

	if (Lookup.IsConfident())
	{
		UK2Node_CallFunction* Node = AllocateNode<UK2Node_CallFunction>();
		Node->SetFromFunction(Lookup.Function);
		FinalizeNode(Node);
		return Node;
	}

	if (Lookup.IsAmbiguous())
	{
		const FString Reason = FString::Printf(
			TEXT("Mais de uma funcao combina. Escreva o nome exato de uma delas:\n  %s"),
			*FString::Join(Lookup.Candidates, TEXT("\n  ")));

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` e' ambiguo. Candidatos: %s"),
			*Statement.NodeExpression, *FString::Join(Lookup.Candidates, TEXT(" | "))));

		return CreateErrorComment(Statement, Reason);
	}

	AddError(Statement.LineNumber, FString::Printf(
		TEXT("Nao achei nenhum node chamado `%s`."), *Statement.NodeExpression));

	return CreateErrorComment(Statement, TEXT("Nenhum node com esse nome foi encontrado."));
}

// ---------------------------------------------------------------------------
// Percurso
// ---------------------------------------------------------------------------

void FNodeScribeBuildContext::Run(const TArray<FNodeScribeStatement>& Statements)
{
	FFrame Root;
	Root.Indent = -1;
	Root.BaseX = FMath::RoundToInt(Origin.X);
	Root.BaseY = FMath::RoundToInt(Origin.Y);
	Frames.Add(Root);

	for (const FNodeScribeStatement& Statement : Statements)
	{
		if (Statement.bIsLabel)
		{
			// Um rotulo fecha qualquer bloco no mesmo nivel ou mais fundo,
			// para que `verdadeiro:` e `falso:` sejam irmaos, nao aninhados.
			while (Frames.Num() > 1 && Frames.Top().Indent >= Statement.Indent)
			{
				Frames.Pop();
			}

			FFrame& Parent = Frames.Top();
			UEdGraphNode* Owner = Parent.LastNode;

			if (!Owner)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("O rotulo `%s:` nao tem node antes dele."), *Statement.Label));
				continue;
			}

			const TArray<UEdGraphPin*> ExecOutputs = GetExecOutputs(Owner);
			const FString Wanted = ResolveLabelAlias(Statement.Label);

			UEdGraphPin* Chosen = nullptr;
			for (UEdGraphPin* Pin : ExecOutputs)
			{
				if (FNodeScribeCatalog::Normalize(Pin->PinName.ToString()) == Wanted)
				{
					Chosen = Pin;
					break;
				}

				if (!Pin->PinFriendlyName.IsEmpty()
					&& FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString()) == Wanted)
				{
					Chosen = Pin;
					break;
				}
			}

			if (!Chosen)
			{
				TArray<FString> Available;
				for (UEdGraphPin* Pin : ExecOutputs)
				{
					Available.Add(Pin->PinName.ToString());
				}

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s:` nao e' uma saida deste node. Saidas: %s"),
					*Statement.Label, *FString::Join(Available, TEXT(", "))));
				continue;
			}

			FFrame Branch;
			Branch.Indent = Statement.Indent;
			Branch.PendingExec = FPinRef(Chosen);
			Branch.BaseX = Owner->NodePosX + ColumnWidth;
			Branch.BaseY = Parent.BaseY + (Parent.BranchesOpened * BranchRowHeight);
			++Parent.BranchesOpened;

			Frames.Add(Branch);
			continue;
		}

		while (Frames.Num() > 1 && Statement.Indent <= Frames.Top().Indent)
		{
			Frames.Pop();
		}

		UEdGraphNode* Node = CreateNodeForStatement(Statement);
		if (!Node)
		{
			continue;
		}

		FFrame& Frame = Frames.Top();

		Node->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
		Node->NodePosY = Frame.BaseY;
		++Frame.Column;

		Result.CreatedNodes.Add(Node);

		// Um comentario ocupa o lugar de uma linha que nao resolveu; aplicar os
		// argumentos dela geraria uma segunda leva de erros sobre o mesmo problema.
		if (!Node->IsA<UEdGraphNode_Comment>())
		{
			ApplyArguments(Node, Statement);
		}

		if (!Statement.OutputName.IsEmpty())
		{
			RegisterOutput(Statement.OutputName, Node, Statement.LineNumber);
		}

		UEdGraphPin* ExecIn = FindExecInput(Node);
		const TArray<UEdGraphPin*> ExecOutputs = GetExecOutputs(Node);

		if (ExecIn)
		{
			if (UEdGraphPin* PreviousExec = Frame.PendingExec.Resolve())
			{
				Connect(PreviousExec, ExecIn, Statement.LineNumber);
			}
		}
		else if (ExecOutputs.Num() == 0)
		{
			// Node puro (nem entrada nem saida de execucao): e' so' um valor.
			// Fica acima da cadeia e nao consome uma coluna dela.
			Node->NodePosY = Frame.BaseY - 180;
			--Frame.Column;

			Frame.LastNode = Node;
			continue;
		}
		// Sem entrada mas com saida = evento. Comeca uma cadeia nova em vez de
		// continuar a anterior, entao nao ha nada a ligar antes dele.

		if (ExecOutputs.Num() == 1)
		{
			Frame.PendingExec = FPinRef(ExecOutputs[0]);
		}
		else if (ExecOutputs.Num() > 1)
		{
			// Varias saidas: escolher uma seria adivinhar por qual caminho o
			// usuario quer seguir. A cadeia para aqui ate ele abrir um rotulo.
			Frame.PendingExec = FPinRef();

			TArray<FString> Names;
			for (UEdGraphPin* Pin : ExecOutputs)
			{
				Names.Add(Pin->PinName.ToString());
			}

			AddInfo(Statement.LineNumber, FString::Printf(
				TEXT("Este node tem varias saidas (%s). Use rotulos indentados para continuar."),
				*FString::Join(Names, TEXT(", "))));
		}
		else
		{
			Frame.PendingExec = FPinRef();
		}

		Frame.LastNode = Node;
	}
}

// ---------------------------------------------------------------------------

FNodeScribeBuilder::FResult FNodeScribeBuilder::Build(
	const TArray<FNodeScribeStatement>& Statements,
	UEdGraph* Graph,
	UBlueprint* Blueprint,
	const FVector2D& Origin)
{
	FNodeScribeBuildContext Context(Graph, Blueprint, Origin);
	Context.Run(Statements);
	return MoveTemp(Context.Result);
}
