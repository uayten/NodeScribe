#include "NodeScribeBuilder.h"

#include "NodeScribeCatalog.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
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
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/Subsystem.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Espacamento horizontal entre nodes de uma mesma cadeia de execucao. */
	constexpr int32 ColumnWidth = 620;

	/** Distancia da linha de execucao ate' o primeiro node de dado, abaixo dela. */
	constexpr int32 DataRowOffsetY = 200;

	/** Cada node de dado ganha a propria linha, empilhando para baixo. */
	constexpr int32 DataRowHeight = 170;

	/** A pilha de dados fica um pouco a' esquerda da coluna de execucao. */
	constexpr int32 DataColumnOffsetX = -40;

	/** Quanto mais fundo na cadeia de dados, mais para a esquerda. */
	constexpr int32 DataDepthIndentX = 30;

	/** Espacamento vertical entre blocos irmaos (os ramos de um Branch). */
	constexpr int32 BranchRowHeight = 300;

	/** Deslocamento de um node de dado criado implicitamente para o seu consumidor. */

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

	/** true para o que aparece na lista de eventos sobrescreviveis do grafo. */
	bool IsOverridableEvent(const UFunction* Function)
	{
		// BlueprintNativeEvent com retorno e' funcao, nao evento: nao tem pino de exec.
		return Function
			&& Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)
			&& Function->GetReturnProperty() == nullptr;
	}

	/**
	 * Procura um evento de Blueprint com esse nome em qualquer classe carregada.
	 *
	 * Separa dois casos que merecem tratamento bem diferente: "voce inventou um
	 * evento novo" (legitimo, e' o que `evento MinhaHabilidadeAtivou` faz) e
	 * "esse evento existe, mas nao nesta classe" -- `BeginPlay` num Widget, por
	 * exemplo. O segundo e' quase sempre engano, e o Custom Event resultante
	 * compila sem reclamar e nunca dispara.
	 */
	UClass* FindClassOwningBlueprintEvent(const FString& EventName)
	{
		const TArray<FString> Attempts = {
			EventName,
			FString(TEXT("Receive")) + EventName,
			FString(TEXT("K2_")) + EventName
		};

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			for (const FString& Attempt : Attempts)
			{
				const UFunction* Found = ClassIt->FindFunctionByName(FName(*Attempt), EIncludeSuperFlag::ExcludeSuper);
				if (IsOverridableEvent(Found))
				{
					return *ClassIt;
				}
			}
		}

		return nullptr;
	}

	/** Os eventos que a classe realmente oferece, para o usuario escolher outro. */
	FString ListAvailableEvents(UClass* Class, int32 MaxCount)
	{
		if (!Class)
		{
			return FString();
		}

		TArray<FString> Names;
		for (TFieldIterator<UFunction> FunctionIt(Class); FunctionIt; ++FunctionIt)
		{
			if (IsOverridableEvent(*FunctionIt))
			{
				Names.AddUnique(FNodeScribeCatalog::StripEventPrefix(FunctionIt->GetName()));
			}
		}

		if (Names.Num() == 0)
		{
			return FString();
		}

		Names.Sort();

		const bool bTruncated = Names.Num() > MaxCount;
		if (bTruncated)
		{
			Names.SetNum(MaxCount);
		}

		return FString::Join(Names, TEXT(", ")) + (bTruncated ? TEXT(", ...") : TEXT(""));
	}

	/**
	 * Nodes que constroem um objeto a partir de uma classe.
	 *
	 * Os pinos deles dependem do valor do pino `Class`: os campos marcados como
	 * *Expose on Spawn* so' aparecem depois que a classe e' escolhida. Por isso
	 * este e' o unico tipo de node em que a ordem dos argumentos importa.
	 *
	 * Resolvidos por caminho, e nao por include: `K2Node_CreateWidget` mora no
	 * Private do UMGEditor, e linkar aquele modulo inteiro por causa de um
	 * ponteiro de classe seria caro demais.
	 */
	struct FConstructNodeForm
	{
		const TCHAR* Name;
		const TCHAR* NodeClassPath;
	};

	const FConstructNodeForm ConstructNodeForms[] = {
		{ TEXT("createwidget"),            TEXT("/Script/UMGEditor.K2Node_CreateWidget") },
		{ TEXT("criarwidget"),             TEXT("/Script/UMGEditor.K2Node_CreateWidget") },
		{ TEXT("spawnactorfromclass"),     TEXT("/Script/BlueprintGraph.K2Node_SpawnActorFromClass") },
		{ TEXT("spawnactor"),              TEXT("/Script/BlueprintGraph.K2Node_SpawnActorFromClass") },
		{ TEXT("constructobjectfromclass"),TEXT("/Script/BlueprintGraph.K2Node_ConstructObjectFromClass") }
	};

	UClass* FindConstructNodeClass(const FString& NormalizedExpression)
	{
		for (const FConstructNodeForm& Form : ConstructNodeForms)
		{
			if (NormalizedExpression == Form.Name)
			{
				return FindObject<UClass>(nullptr, Form.NodeClassPath);
			}
		}

		return nullptr;
	}

	/**
	 * Asset de um tipo qualquer, por caminho completo ou por nome curto.
	 *
	 * O nome curto so' alcanca o que ja' esta' carregado -- o suficiente para o
	 * asset que voce acabou de abrir, e por isso vale a conveniencia. Quando
	 * falha, quem chama manda usar o caminho, que sempre funciona.
	 */
	UObject* FindAssetByPathOrName(const TCHAR* ClassPath, const FString& Value)
	{
		if (Value.StartsWith(TEXT("/")))
		{
			return LoadObject<UObject>(nullptr, *Value);
		}

		UClass* AssetClass = FindObject<UClass>(nullptr, ClassPath);
		if (!AssetClass)
		{
			return nullptr;
		}

		const FString Normalized = FNodeScribeCatalog::Normalize(Value);

		for (TObjectIterator<UObject> ObjectIt; ObjectIt; ++ObjectIt)
		{
			UObject* Candidate = *ObjectIt;
			if (Candidate->IsA(AssetClass)
				&& FNodeScribeCatalog::Normalize(Candidate->GetName()) == Normalized)
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	/** Busca de struct por nome, aceitando `MapPlayerKeyArgs` e `Map Player Key Args`. */
	UScriptStruct* FindStructByFriendlyName(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		if (Normalized.IsEmpty())
		{
			return nullptr;
		}

		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			UScriptStruct* Struct = *StructIt;

			if (FNodeScribeCatalog::Normalize(Struct->GetName()) == Normalized
				|| FNodeScribeCatalog::Normalize(Struct->GetDisplayNameText().ToString()) == Normalized)
			{
				return Struct;
			}
		}

		return nullptr;
	}

	/**
	 * O node de Get muda conforme onde o subsistema vive: um de LocalPlayer
	 * precisa do PlayerController, um de Engine nao precisa de nada. Escolher
	 * errado da' um node que nem compila.
	 */
	UClass* ChooseSubsystemNodeClass(UClass* SubsystemClass)
	{
		if (SubsystemClass->IsChildOf(ULocalPlayerSubsystem::StaticClass()))
		{
			return UK2Node_GetSubsystemFromPC::StaticClass();
		}

		if (SubsystemClass->IsChildOf(UEngineSubsystem::StaticClass()))
		{
			return UK2Node_GetEngineSubsystem::StaticClass();
		}

		// UEditorSubsystem vive num modulo que este plugin nao linka; procuramos
		// a classe pelo caminho para nao criar a dependencia so' por isso.
		static const UClass* EditorSubsystemClass =
			FindObject<UClass>(nullptr, TEXT("/Script/EditorSubsystem.EditorSubsystem"));

		if (EditorSubsystemClass && SubsystemClass->IsChildOf(EditorSubsystemClass))
		{
			return UK2Node_GetEditorSubsystem::StaticClass();
		}

		return UK2Node_GetSubsystem::StaticClass();
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

	/**
	 * Um evento com a mesma identidade ja' no grafo.
	 *
	 * A Unreal permite um node por evento: dois `BeginPlay`, ou dois eventos do
	 * mesmo dispatcher, deixam o Blueprint sem compilar. Colar por cima de um
	 * grafo que ja' tem o evento e' o jeito mais facil de cair nisso.
	 */
	UEdGraphNode* FindExistingEvent(const TFunctionRef<bool(UEdGraphNode*)>& Matches) const;

	UEdGraphNode* CreateDuplicateEventComment(const FNodeScribeStatement& Statement, const FString& EventLabel);

	// --- Ligacao ----------------------------------------------------------

	void ApplyArguments(UEdGraphNode* Node, const FNodeScribeStatement& Statement);
	void ApplyLiteral(UEdGraphPin* Pin, const FString& Value, int32 Line);

	/** @param ConsumerPin  onde o valor vai entrar; e' dele que sai o tipo de uma variavel nova. */
	UEdGraphPin* ResolveReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin);

	/** Parte antes do ponto de `$nome.Pino`: resolve o node, nao o pino final. */
	UEdGraphPin* ResolveBaseReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin);

	void RegisterOutput(const FString& Name, UEdGraphNode* Node, int32 Line);

	UEdGraphPin* FindPinByFuzzyName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction) const;

	/**
	 * Divide um pino de struct quando o texto pede uma parte dele.
	 *
	 * No grafo, `Selected Key` pode aparecer dividido em `Selected Key Key`,
	 * `Selected Key Shift` e afins. O node nasce sempre inteiro; sem dividir,
	 * a metade pedida simplesmente nao existe e a ligacao se perde.
	 */
	UEdGraphPin* TrySplitToFindPin(UEdGraphNode* Node, const FString& PinPath);
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

	/** Classe do objeto apontado por `Target = $algo`, quando a linha tem um. */
	UClass* FindTargetClassFromArgs(const FNodeScribeStatement& Statement) const;

	/** Propriedade por nome tolerante: `Show Mouse Cursor` acha `bShowMouseCursor`. */
	static FProperty* FindPropertyByFriendlyName(UClass* Class, const FString& Name);

	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	FVector2D Origin = FVector2D::ZeroVector;
	const UEdGraphSchema_K2* Schema = nullptr;

	TArray<FFrame> Frames;

	/** Saidas nomeadas com `nome = ...`, disponiveis para `$nome`. */
	TMap<FString, FPinRef> NamedOutputs;

	/** Nomes cuja linha nao resolveu, e em que linha isso aconteceu. */
	TMap<FString, int32> FailedOutputs;

	/**
	 * Nodes de varias saidas de execucao ainda sem rotulo.
	 *
	 * O aviso so' pode sair no fim: na hora em que o node nasce, o rotulo que
	 * o resolve costuma estar na linha seguinte, e avisar ali seria alarme
	 * falso em todo Branch e todo laco bem escrito.
	 */
	struct FUnbranchedNode
	{
		UEdGraphNode* Node = nullptr;
		int32 Line = 0;
		FString Outputs;
	};

	TArray<FUnbranchedNode> UnbranchedNodes;

	/** true quando o node nao participa do fluxo de execucao: e' so' um valor. */
	bool IsPureDataNode(UEdGraphNode* Node) const;

	/**
	 * Segue os fios de dado para a frente ate' achar quem consome o valor.
	 * @param OutDepth  saltos ate' la'. 1 = alimenta o node de execucao direto.
	 */
	UEdGraphNode* FindConsumingExecNode(UEdGraphNode* Node, int32& OutDepth) const;

	/**
	 * Posiciona todos os nodes de dado, depois que o grafo inteiro existe.
	 *
	 * Tem que ser no fim: um Get criado por `$Variavel` nasce durante a
	 * ligacao, e o node que o consome pode ainda nao ter posicao nenhuma. Era
	 * o que jogava esse Get para longe -- ele era ancorado num (0,0).
	 */
	void LayoutDataNodes();
};

bool FNodeScribeBuildContext::IsPureDataNode(UEdGraphNode* Node) const
{
	return Node
		&& !Node->IsA<UEdGraphNode_Comment>()
		&& !FindExecInput(Node)
		&& GetExecOutputs(Node).Num() == 0;
}

UEdGraphNode* FNodeScribeBuildContext::FindConsumingExecNode(UEdGraphNode* Node, int32& OutDepth) const
{
	OutDepth = 0;
	UEdGraphNode* Current = Node;

	// O grafo de dados e' aciclico, mas um texto torto pode fechar um ciclo;
	// o teto evita travar o editor quando isso acontece.
	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		UEdGraphNode* Next = nullptr;

		for (UEdGraphPin* Pin : Current->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin) && Pin->LinkedTo.Num() > 0)
			{
				Next = Pin->LinkedTo[0]->GetOwningNodeUnchecked();
				break;
			}
		}

		if (!Next)
		{
			return nullptr;
		}

		++OutDepth;

		if (!IsPureDataNode(Next))
		{
			return Next;
		}

		Current = Next;
	}

	return nullptr;
}

void FNodeScribeBuildContext::LayoutDataNodes()
{
	struct FDataNode
	{
		UEdGraphNode* Node = nullptr;
		int32 Depth = 0;
	};

	TMap<UEdGraphNode*, TArray<FDataNode>> ByConsumer;
	TArray<UEdGraphNode*> Orphans;

	for (UEdGraphNode* Node : Result.CreatedNodes)
	{
		if (!IsPureDataNode(Node))
		{
			continue;
		}

		int32 Depth = 0;
		if (UEdGraphNode* Consumer = FindConsumingExecNode(Node, Depth))
		{
			ByConsumer.FindOrAdd(Consumer).Add({ Node, Depth });
		}
		else
		{
			Orphans.Add(Node);
		}
	}

	for (TPair<UEdGraphNode*, TArray<FDataNode>>& Pair : ByConsumer)
	{
		TArray<FDataNode>& Group = Pair.Value;

		// Quem alimenta a execucao diretamente fica logo abaixo dela; as
		// dependencias mais fundas descem, recuando para a esquerda, de modo
		// que os fios de dado corram todos para cima e para a direita.
		Group.Sort([](const FDataNode& A, const FDataNode& B) { return A.Depth < B.Depth; });

		for (int32 Row = 0; Row < Group.Num(); ++Row)
		{
			Group[Row].Node->NodePosX =
				Pair.Key->NodePosX + DataColumnOffsetX - (Group[Row].Depth * DataDepthIndentX);

			Group[Row].Node->NodePosY =
				Pair.Key->NodePosY + DataRowOffsetY + (Row * DataRowHeight);
		}
	}

	// Valor que ninguem consome nao tem embaixo de quem ficar. Vai para uma
	// coluna propria depois do fim da cadeia, em vez de empilhar em (0,0).
	if (Orphans.Num() > 0 && Frames.Num() > 0)
	{
		const FFrame& Frame = Frames[0];

		for (int32 Index = 0; Index < Orphans.Num(); ++Index)
		{
			Orphans[Index]->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Orphans[Index]->NodePosY = Frame.BaseY + (Index * DataRowHeight);
		}
	}
}

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

	// O esqueleto reflete o Blueprint como ele esta' agora, com as variaveis e
	// dispatchers que voce acabou de criar. GeneratedClass so' alcanca o que ja'
	// foi compilado, e ninguem compila antes de colar -- e' o esqueleto que o
	// proprio editor usa para montar o grafo.
	if (Blueprint->SkeletonGeneratedClass)
	{
		return Blueprint->SkeletonGeneratedClass;
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

UClass* FNodeScribeBuildContext::FindTargetClassFromArgs(const FNodeScribeStatement& Statement) const
{
	for (const FNodeScribeArg& Arg : Statement.Args)
	{
		if (!Arg.bIsReference)
		{
			continue;
		}

		const FString PinName = FNodeScribeCatalog::Normalize(Arg.PinName);
		if (PinName != TEXT("target") && PinName != TEXT("alvo") && PinName != TEXT("self"))
		{
			continue;
		}

		// Consulta sem efeito colateral: `ResolveReference` criaria nodes, e
		// aqui ainda estamos decidindo se este node existe.
		FString BaseName = Arg.Value;
		FString Ignored;
		BaseName.Split(TEXT("."), &BaseName, &Ignored);

		const FPinRef* Ref = NamedOutputs.Find(BaseName);
		if (!Ref)
		{
			continue;
		}

		UEdGraphPin* Pin = const_cast<FPinRef*>(Ref)->Resolve();
		if (!Pin)
		{
			continue;
		}

		if (UClass* Class = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			return Class;
		}
	}

	return nullptr;
}

FProperty* FNodeScribeBuildContext::FindPropertyByFriendlyName(UClass* Class, const FString& Name)
{
	if (!Class)
	{
		return nullptr;
	}

	const FString Wanted = FNodeScribeCatalog::Normalize(Name);

	for (TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			continue;
		}

		// O nome interno de um bool leva `b` na frente, e o nome que aparece na
		// tela nao. `Show Mouse Cursor` tem que achar `bShowMouseCursor`.
		if (FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted
			|| FNodeScribeCatalog::Normalize(Property->GetDisplayNameText().ToString()) == Wanted)
		{
			return Property;
		}
	}

	return nullptr;
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

UEdGraphPin* FNodeScribeBuildContext::TrySplitToFindPin(UEdGraphNode* Node, const FString& PinPath)
{
	const FString Wanted = FNodeScribeCatalog::Normalize(PinPath);

	// Copia: dividir um pino mexe em Node->Pins durante a iteracao.
	TArray<UEdGraphPin*> Candidates = Node->Pins;

	for (UEdGraphPin* Pin : Candidates)
	{
		if (!Pin || Pin->Direction != EGPD_Output || IsExecPin(Pin) || Pin->SubPins.Num() > 0)
		{
			continue;
		}

		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			continue;
		}

		// `Selected Key Key` comeca com `Selected Key`: e' parte dessa struct.
		const FString PinName = FNodeScribeCatalog::Normalize(Pin->PinName.ToString());
		const FString FriendlyName = Pin->PinFriendlyName.IsEmpty()
			? PinName
			: FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString());

		if (!Wanted.StartsWith(PinName) && !Wanted.StartsWith(FriendlyName))
		{
			continue;
		}

		if (!Schema->CanSplitStructPin(*Pin))
		{
			continue;
		}

		Schema->SplitPin(Pin, false);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Output))
		{
			return Found;
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

		// Pino de classe aceita nome curto: `WBP_LinhaRemapear` acha a classe
		// gerada `WBP_LinhaRemapear_C`. Mesmo atalho que `Cast to BP_Boss` usa.
		const FName Category = Pin->PinType.PinCategory;
		if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			if (UClass* Found = FindClassByFriendlyName(Value))
			{
				Schema->TrySetDefaultObject(*Pin, Found);
				return;
			}
		}

		// Sem caminho completo nao da' para saber qual asset e'. Nao chutamos.
		AddWarning(Line, FString::Printf(
			TEXT("`%s` nao e' um caminho de asset. Escolha no pino `%s` do node."),
			*Value, *Pin->PinName.ToString()));
		return;
	}

	Schema->TrySetDefaultValue(*Pin, Value);
}

UEdGraphPin* FNodeScribeBuildContext::ResolveReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin)
{
	// `$nome` sozinho pega a saida principal do node. Quando o node tem varias
	// saidas de dado -- um evento com parametros, um Break de struct, uma funcao
	// com out params -- `$nome.Pino` diz qual delas.
	FString BaseName = Name;
	FString PinPath;

	if (Name.Split(TEXT("."), &BaseName, &PinPath))
	{
		BaseName.TrimStartAndEndInline();
		PinPath.TrimStartAndEndInline();
	}

	UEdGraphPin* BasePin = ResolveBaseReference(BaseName, Line, ConsumerPin);
	if (!BasePin || PinPath.IsEmpty())
	{
		return BasePin;
	}

	UEdGraphNode* SourceNode = BasePin->GetOwningNodeUnchecked();
	if (!SourceNode)
	{
		return nullptr;
	}

	if (UEdGraphPin* Chosen = FindPinByFuzzyName(SourceNode, PinPath, EGPD_Output))
	{
		return Chosen;
	}

	if (UEdGraphPin* Split = TrySplitToFindPin(SourceNode, PinPath))
	{
		return Split;
	}

	TArray<FString> Available;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && !IsExecPin(Pin) && !Pin->bHidden)
		{
			Available.Add(Pin->PinName.ToString());
		}
	}

	AddError(Line, FString::Printf(
		TEXT("`$%s` nao tem saida `%s`. Saidas de dado: %s"),
		*BaseName, *PinPath, *FString::Join(Available, TEXT(", "))));

	return nullptr;
}

UEdGraphPin* FNodeScribeBuildContext::ResolveBaseReference(const FString& Name, int32 Line, UEdGraphPin* ConsumerPin)
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

		// A posicao fica para LayoutDataNodes(): aqui o node que consome este
		// Get pode ainda nem ter sido posicionado.
		Result.CreatedNodes.Add(GetNode);

		UEdGraphPin* OutputPin = FindPrimaryOutput(GetNode);
		if (OutputPin)
		{
			NamedOutputs.Add(Name, FPinRef(OutputPin));
		}

		return OutputPin;
	}

	// A causa ja' foi relatada la' atras; repetir "nao existe" aqui mandaria o
	// usuario procurar o problema no lugar errado.
	if (const int32* FailedLine = FailedOutputs.Find(Name))
	{
		AddError(Line, FString::Printf(
			TEXT("`$%s` vem da linha %d, que nao resolveu. Corrija aquela linha primeiro."),
			*Name, *FailedLine));

		return nullptr;
	}

	// A variavel ainda nao existe. Em vez de descartar a ligacao, criamos o Get
	// nao resolvido -- e' o que a Unreal faz ao colar nodes entre Blueprints
	// diferentes. O grafo acusa o erro e o botao direito no node oferece criar
	// a variavel, o que e' um clique contra reconstruir a cadeia na mao.
	//
	// Isso nao afrouxa a regra de nao chutar: o Blueprint continua sem
	// compilar ate' voce agir. So' muda onde a pendencia fica visivel.
	if (ConsumerPin)
	{
		UK2Node_VariableGet* GetNode = AllocateNode<UK2Node_VariableGet>();
		GetNode->VariableReference.SetSelfMember(FName(*Name));
		FinalizeNode(GetNode);

		// Sem a propriedade, AllocateDefaultPins nao cria pino nenhum. O tipo
		// vem de quem vai consumir o valor: e' a unica fonte disponivel aqui, e
		// e' exatamente o tipo que a variavel precisa ter.
		UEdGraphPin* OutputPin = FindPrimaryOutput(GetNode);
		if (!OutputPin)
		{
			OutputPin = GetNode->CreatePin(EGPD_Output, ConsumerPin->PinType, FName(*Name));
		}

		Result.CreatedNodes.Add(GetNode);

		AddWarning(Line, FString::Printf(
			TEXT("`%s` nao existe neste Blueprint. Criei o Get assim mesmo: o grafo vai acusar o erro, ")
			TEXT("e o botao direito no node oferece criar a variavel."), *Name));

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
			if (UEdGraphPin* Source = ResolveReference(Arg.Value, Statement.LineNumber, Pin))
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

UEdGraphNode* FNodeScribeBuildContext::FindExistingEvent(const TFunctionRef<bool(UEdGraphNode*)>& Matches) const
{
	for (UEdGraphNode* Existing : Graph->Nodes)
	{
		if (Existing && Matches(Existing))
		{
			return Existing;
		}
	}

	return nullptr;
}

UEdGraphNode* FNodeScribeBuildContext::CreateDuplicateEventComment(
	const FNodeScribeStatement& Statement,
	const FString& EventLabel)
{
	AddError(Statement.LineNumber, FString::Printf(
		TEXT("O evento `%s` ja' existe neste grafo; a Unreal so' permite um."), *EventLabel));

	return CreateErrorComment(Statement, FString::Printf(
		TEXT("`%s` ja' existe neste grafo.\n\n")
		TEXT("A Unreal so' permite um node por evento, entao nao criei outro nem mexi no que ja' estava la'.\n\n")
		TEXT("A cadeia desta linha ficou sem inicio: ligue ela no evento que ja' existe, ")
		TEXT("ou apague aquele node antes de colar."),
		*EventLabel));
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
				const FName WantedEvent = EventFunction->GetFName();

				if (FindExistingEvent([&](UEdGraphNode* Existing)
					{
						const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Existing);
						return EventNode
							&& EventNode->bOverrideFunction
							&& EventNode->EventReference.GetMemberName() == WantedEvent;
					}))
				{
					return CreateDuplicateEventComment(Statement, EventName);
				}

				UK2Node_Event* Node = AllocateNode<UK2Node_Event>();
				Node->EventReference.SetExternalMember(EventFunction->GetFName(), EventFunction->GetOwnerClass());
				Node->bOverrideFunction = true;
				FinalizeNode(Node);
				return Node;
			}

			// --- evento <Dispatcher> de <Variavel> -----------------------
			// A forma que faltava: o node vermelho de antes dizia "recrie a
			// mao" justamente porque este bloco nao existia.
			{
				FString DelegateName;
				FString ComponentName;

				const bool bHasOwner =
					EventName.Split(TEXT(" de "), &DelegateName, &ComponentName, ESearchCase::IgnoreCase)
					|| EventName.Split(TEXT(" of "), &DelegateName, &ComponentName, ESearchCase::IgnoreCase);

				if (bHasOwner)
				{
					DelegateName.TrimStartAndEndInline();
					ComponentName.TrimStartAndEndInline();

					UClass* SelfClass = GetSelfClass();

					FObjectProperty* ComponentProperty = SelfClass
						? FindFProperty<FObjectProperty>(SelfClass, FName(*ComponentName))
						: nullptr;

					if (!ComponentProperty)
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` nao e' uma variavel de objeto deste Blueprint."), *ComponentName));

						return CreateErrorComment(Statement, FString::Printf(
							TEXT("`%s` precisa ser uma variavel deste Blueprint que aponte para outro objeto."),
							*ComponentName));
					}

					FMulticastDelegateProperty* DelegateProperty =
						FindFProperty<FMulticastDelegateProperty>(ComponentProperty->PropertyClass, FName(*DelegateName));

					if (!DelegateProperty)
					{
						TArray<FString> Available;
						for (TFieldIterator<FMulticastDelegateProperty> It(ComponentProperty->PropertyClass); It; ++It)
						{
							Available.Add(It->GetName());
						}

						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` nao tem dispatcher `%s`. Dispatchers: %s"),
							*ComponentName, *DelegateName,
							Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("nenhum")));

						return CreateErrorComment(Statement, FString::Printf(
							TEXT("`%s` nao expoe um dispatcher chamado `%s`."), *ComponentName, *DelegateName));
					}

					const FName WantedDelegate = DelegateProperty->GetFName();
					const FName WantedComponent = ComponentProperty->GetFName();

					if (FindExistingEvent([&](UEdGraphNode* Existing)
						{
							const UK2Node_ComponentBoundEvent* Bound = Cast<UK2Node_ComponentBoundEvent>(Existing);
							return Bound
								&& Bound->DelegatePropertyName == WantedDelegate
								&& Bound->GetComponentPropertyName() == WantedComponent;
						}))
					{
						return CreateDuplicateEventComment(Statement,
							FString::Printf(TEXT("%s de %s"), *DelegateName, *ComponentName));
					}

					UK2Node_ComponentBoundEvent* Node = AllocateNode<UK2Node_ComponentBoundEvent>();
					Node->InitializeComponentBoundEventParams(ComponentProperty, DelegateProperty);
					FinalizeNode(Node);
					return Node;
				}
			}

			// `__DelegateSignature` e' o sufixo que a Engine poe na funcao de
			// assinatura de um delegate. Um Custom Event com esse nome nao e'
			// algo que alguem escreveria: e' um evento de dispatcher que perdeu
			// o vinculo. Criar mesmo assim daria um node plausivel e morto, que
			// e' exatamente o resultado que este plugin recusa a produzir.
			if (EventName.EndsWith(TEXT("__DelegateSignature"), ESearchCase::CaseSensitive))
			{
				FString DispatcherName = EventName;
				DispatcherName.RemoveFromEnd(TEXT("__DelegateSignature"));

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("`%s` e' um evento de dispatcher/delegate. O formato ainda nao sabe recriar esse vinculo."),
					*DispatcherName));

				return CreateErrorComment(Statement, FString::Printf(
					TEXT("`%s` e' um evento ligado a um dispatcher/delegate.\n\n")
					TEXT("Um Custom Event com esse nome compilaria e nunca dispararia, entao nao criei nenhum.\n\n")
					TEXT("Para fazer a mao: botao direito no grafo, procure `%s`, e escolha a opcao de evento."),
					*DispatcherName, *DispatcherName));
			}

			const FName WantedCustomEvent(*EventName);

			if (FindExistingEvent([&](UEdGraphNode* Existing)
				{
					const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Existing);
					return CustomEvent && CustomEvent->CustomFunctionName == WantedCustomEvent;
				}))
			{
				return CreateDuplicateEventComment(Statement, EventName);
			}

			UK2Node_CustomEvent* Node = AllocateNode<UK2Node_CustomEvent>();
			Node->CustomFunctionName = FName(*EventName);
			FinalizeNode(Node);

			UClass* SelfParentClass = Blueprint ? Blueprint->ParentClass.Get() : nullptr;

			if (UClass* OwningClass = FindClassOwningBlueprintEvent(EventName))
			{
				// O evento existe -- so' que em outra hierarquia. Um Custom Event com
				// o nome de um evento da Engine compila, fica plausivel no grafo e
				// nunca dispara. Isso e' aviso, nao nota de rodape.
				FString Message = FString::Printf(
					TEXT("`%s` e' evento de `%s`, mas este Blueprint deriva de `%s`. ")
					TEXT("Criei um Custom Event com esse nome, e ele nunca vai disparar sozinho."),
					*EventName,
					*OwningClass->GetName(),
					SelfParentClass ? *SelfParentClass->GetName() : TEXT("?"));

				const FString Available = ListAvailableEvents(SelfParentClass, 8);
				if (!Available.IsEmpty())
				{
					Message += FString::Printf(TEXT(" Aqui existem: %s."), *Available);
				}

				AddWarning(Statement.LineNumber, Message);
			}
			else
			{
				AddInfo(Statement.LineNumber, FString::Printf(
					TEXT("`%s` nao existe na classe pai; criei um Custom Event com esse nome."), *EventName));
			}

			return Node;
		}
	}

	// --- Select ----------------------------------------------------------
	if (Normalized == TEXT("select") || Normalized == TEXT("selecionar"))
	{
		UK2Node_Select* Node = AllocateNode<UK2Node_Select>();
		FinalizeNode(Node);
		return Node;
	}

	// --- Chamar / Vincular / Desvincular / Limpar dispatcher -------------
	{
		struct FDelegateForm
		{
			const TCHAR* Prefix;
			int32 PrefixLength;
			UClass* (*MakeClass)();
		};

		static const FDelegateForm DelegateForms[] = {
			{ TEXT("Chamar "),      7,  []() { return UK2Node_CallDelegate::StaticClass(); } },
			{ TEXT("Call "),        5,  []() { return UK2Node_CallDelegate::StaticClass(); } },
			{ TEXT("Vincular "),    9,  []() { return UK2Node_AddDelegate::StaticClass(); } },
			{ TEXT("Bind "),        5,  []() { return UK2Node_AddDelegate::StaticClass(); } },
			{ TEXT("Desvincular "), 12, []() { return UK2Node_RemoveDelegate::StaticClass(); } },
			{ TEXT("Unbind "),      7,  []() { return UK2Node_RemoveDelegate::StaticClass(); } },
			{ TEXT("Limpar "),      7,  []() { return UK2Node_ClearDelegate::StaticClass(); } },
			{ TEXT("Clear "),       6,  []() { return UK2Node_ClearDelegate::StaticClass(); } }
		};

		for (const FDelegateForm& Form : DelegateForms)
		{
			if (!Expression.StartsWith(Form.Prefix, ESearchCase::IgnoreCase))
			{
				continue;
			}

			FString DelegateName = Expression.RightChop(Form.PrefixLength);
			DelegateName.TrimStartAndEndInline();

			// Sem `Target`, o dispatcher e' deste Blueprint. Com ele, e' do
			// objeto apontado -- mesma regra de `Set X (Target = $obj)`.
			UClass* OwnerClass = FindTargetClassFromArgs(Statement);
			const bool bSelfContext = (OwnerClass == nullptr);

			if (bSelfContext)
			{
				OwnerClass = GetSelfClass();
			}

			FMulticastDelegateProperty* DelegateProperty = OwnerClass
				? FindFProperty<FMulticastDelegateProperty>(OwnerClass, FName(*DelegateName))
				: nullptr;

			if (!DelegateProperty)
			{
				// Nao e' dispatcher: pode ser uma funcao que so' comeca com
				// "Clear" ou "Call". Deixa o catalogo tentar.
				break;
			}

			UK2Node_BaseMCDelegate* Node = static_cast<UK2Node_BaseMCDelegate*>(
				NewObject<UEdGraphNode>(Graph, Form.MakeClass()));

			Graph->AddNode(Node, false, false);
			Node->CreateNewGuid();
			Node->SetFromProperty(DelegateProperty, bSelfContext, OwnerClass);
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- Switch on <Enum|Int|String|Name> --------------------------------
	{
		FString SwitchOn;

		if (Expression.StartsWith(TEXT("Switch on "), ESearchCase::IgnoreCase))
		{
			SwitchOn = Expression.RightChop(10);
		}
		else if (Expression.StartsWith(TEXT("Switch "), ESearchCase::IgnoreCase))
		{
			SwitchOn = Expression.RightChop(7);
		}

		SwitchOn.TrimStartAndEndInline();

		if (!SwitchOn.IsEmpty())
		{
			const FString NormalizedSwitch = FNodeScribeCatalog::Normalize(SwitchOn);

			if (NormalizedSwitch == TEXT("int") || NormalizedSwitch == TEXT("integer"))
			{
				UK2Node_SwitchInteger* Node = AllocateNode<UK2Node_SwitchInteger>();
				FinalizeNode(Node);
				return Node;
			}

			if (NormalizedSwitch == TEXT("string"))
			{
				UK2Node_SwitchString* Node = AllocateNode<UK2Node_SwitchString>();
				FinalizeNode(Node);
				return Node;
			}

			if (NormalizedSwitch == TEXT("name"))
			{
				UK2Node_SwitchName* Node = AllocateNode<UK2Node_SwitchName>();
				FinalizeNode(Node);
				return Node;
			}

			// Sobrou enum. Cada valor dele vira uma saida de execucao, entao o
			// enum precisa estar posto antes de alocar os pinos.
			UEnum* Enum = nullptr;
			for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
			{
				if (FNodeScribeCatalog::Normalize(EnumIt->GetName()) == NormalizedSwitch)
				{
					Enum = *EnumIt;
					break;
				}
			}

			if (!Enum)
			{
				AddError(Statement.LineNumber, FString::Printf(
					TEXT("Nao achei o enum `%s`."), *SwitchOn));

				return CreateErrorComment(Statement, FString::Printf(
					TEXT("Enum `%s` nao encontrado.\n\nUse o nome exato dele, como `EJSL4UBatteryLevel`."),
					*SwitchOn));
			}

			UK2Node_SwitchEnum* Node = AllocateNode<UK2Node_SwitchEnum>();
			Node->SetEnum(Enum);
			FinalizeNode(Node);
			return Node;
		}
	}

	// --- EnhancedInputAction <Action> ------------------------------------
	if (Expression.StartsWith(TEXT("EnhancedInputAction "), ESearchCase::IgnoreCase))
	{
		FString ActionName = Expression.RightChop(20);
		ActionName.TrimStartAndEndInline();

		// Resolvido por caminho para nao arrastar o modulo InputBlueprintNodes
		// como dependencia: o plugin nao deve exigir Enhanced Input instalado.
		UClass* NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
		if (!NodeClass)
		{
			AddError(Statement.LineNumber, TEXT("O plugin Enhanced Input nao esta' habilitado neste projeto."));
			return CreateErrorComment(Statement, TEXT("Enhanced Input nao esta' habilitado."));
		}

		UObject* Action = FindAssetByPathOrName(TEXT("/Script/EnhancedInput.InputAction"), ActionName);
		if (!Action)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Nao achei a Input Action `%s`. Use o caminho completo se ela nao estiver aberta."), *ActionName));

			return CreateErrorComment(Statement, FString::Printf(
				TEXT("Input Action `%s` nao encontrada.\n\nUse o caminho completo, algo como\n/Game/.../IA_Ataque.IA_Ataque"),
				*ActionName));
		}

		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();

		// A action define quais pinos de gatilho existem (Started, Triggered,
		// Completed...), entao ela precisa estar posta antes de alocar pinos.
		if (FObjectProperty* ActionProperty = FindFProperty<FObjectProperty>(NodeClass, TEXT("InputAction")))
		{
			ActionProperty->SetObjectPropertyValue_InContainer(Node, Action);
		}

		FinalizeNode(Node);
		return Node;
	}

	// --- Create Widget / Spawn Actor from Class --------------------------
	if (UClass* NodeClass = FindConstructNodeClass(Normalized))
	{
		UK2Node_ConstructObjectFromClass* Node = NewObject<UK2Node_ConstructObjectFromClass>(Graph, NodeClass);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		FinalizeNode(Node);

		// A classe tem que entrar antes dos outros argumentos: e' ela que faz
		// os pinos de Expose on Spawn existirem. Aplicada na ordem normal, os
		// outros argumentos chegariam antes dos pinos deles e virariam
		// "o node nao tem pino X" -- um erro que nao explicaria nada.
		FString ClassValue;
		for (const FNodeScribeArg& Arg : Statement.Args)
		{
			const FString PinName = FNodeScribeCatalog::Normalize(Arg.PinName);
			if (PinName == TEXT("class") || PinName == TEXT("classe"))
			{
				ClassValue = Arg.Value;
				break;
			}
		}

		// Sem nome de pino, o primeiro argumento e' a classe: `Create Widget (WBP_X)`.
		if (ClassValue.IsEmpty() && Statement.Args.Num() > 0 && Statement.Args[0].PinName.IsEmpty())
		{
			ClassValue = Statement.Args[0].Value;
		}

		if (ClassValue.IsEmpty() || ClassValue.StartsWith(TEXT("?")))
		{
			AddWarning(Statement.LineNumber,
				TEXT("Sem `Class`, este node nao tem os pinos de Expose on Spawn. Escolha a classe nele."));
			return Node;
		}

		UClass* SpawnClass = ClassValue.StartsWith(TEXT("/"))
			? LoadObject<UClass>(nullptr, *ClassValue)
			: FindClassByFriendlyName(ClassValue);

		if (!SpawnClass)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("Nao achei a classe `%s`, entao os pinos de Expose on Spawn nao existem."), *ClassValue));
			return Node;
		}

		if (UEdGraphPin* ClassPin = Node->GetClassPin())
		{
			Schema->TrySetDefaultObject(*ClassPin, SpawnClass);

			// `PinDefaultValueChanged` e' o hook que o editor dispara quando voce
			// escolhe a classe no dropdown, e e' ele que cria os pinos de Expose
			// on Spawn. `ReconstructNode` sozinho refaz o node com os pinos
			// padrao -- o titulo ate' muda, o que engana, mas os pinos nao vem.
			Node->PinDefaultValueChanged(ClassPin);
		}

		return Node;
	}

	// --- Make / Break de struct ------------------------------------------
	{
		FString StructName;
		bool bIsBreak = false;

		if (Expression.StartsWith(TEXT("Make "), ESearchCase::IgnoreCase))
		{
			StructName = Expression.RightChop(5);
		}
		else if (Expression.StartsWith(TEXT("Break "), ESearchCase::IgnoreCase))
		{
			StructName = Expression.RightChop(6);
			bIsBreak = true;
		}

		StructName.TrimStartAndEndInline();

		// So' vira node de struct se a struct existir mesmo. `Make Literal Int`
		// e afins continuam caindo no catalogo de funcoes, que e' onde moram.
		if (UScriptStruct* Struct = FindStructByFriendlyName(StructName))
		{
			if (bIsBreak)
			{
				UK2Node_BreakStruct* Node = AllocateNode<UK2Node_BreakStruct>();
				Node->StructType = Struct;
				Node->bMadeAfterOverridePinRemoval = true;
				FinalizeNode(Node);
				return Node;
			}

			UK2Node_MakeStruct* Node = AllocateNode<UK2Node_MakeStruct>();
			Node->StructType = Struct;
			Node->bMadeAfterOverridePinRemoval = true;
			FinalizeNode(Node);
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

		// Variavel de OUTRO objeto: `Set Show Mouse Cursor (Target = $pc, ...)`.
		// A classe sai do pino que `Target` aponta -- e' a mesma informacao que
		// o editor usa quando voce arrasta de um pino de objeto e pede o Set.
		if (!VariableName.IsEmpty())
		{
			if (UClass* TargetClass = FindTargetClassFromArgs(Statement))
			{
				if (FProperty* Property = FindPropertyByFriendlyName(TargetClass, VariableName))
				{
					if (bIsSetter)
					{
						UK2Node_VariableSet* Node = AllocateNode<UK2Node_VariableSet>();
						Node->VariableReference.SetExternalMember(Property->GetFName(), TargetClass);
						FinalizeNode(Node);
						return Node;
					}

					UK2Node_VariableGet* Node = AllocateNode<UK2Node_VariableGet>();
					Node->VariableReference.SetExternalMember(Property->GetFName(), TargetClass);
					FinalizeNode(Node);
					return Node;
				}
			}
		}

		// Nao e' variavel, mas pode ser subsistema: `Get EnhancedInputLocalPlayerSubsystem`.
		// Esses nodes nao sao chamada de funcao e nunca apareceriam no catalogo.
		if (!bIsSetter && !VariableName.IsEmpty())
		{
			if (UClass* SubsystemClass = FindClassByFriendlyName(VariableName))
			{
				if (SubsystemClass->IsChildOf(USubsystem::StaticClass()))
				{
					UClass* NodeClass = ChooseSubsystemNodeClass(SubsystemClass);

					UK2Node_GetSubsystem* Node = NewObject<UK2Node_GetSubsystem>(Graph, NodeClass);
					Graph->AddNode(Node, false, false);
					Node->CreateNewGuid();
					Node->Initialize(SubsystemClass);
					FinalizeNode(Node);
					return Node;
				}
			}
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

	const FNodeScribeLookup Lookup = FNodeScribeCatalog::Get().FindFunction(
		Statement.NodeExpression, GetSelfClass(), nullptr);

	// Macros da biblioteca padrao (ForEachLoop, DoOnce, Gate...).
	//
	// Nomear a saida (`x = ...`) faz uma funcao PURA de mesmo nome ter
	// prioridade: `valido = Is Valid (...)` quer o bool, nao a macro. Mas so'
	// quando essa funcao existe de fato -- `loop = For Each Loop (...)` nomeia
	// a saida para poder escrever `$loop.Array Element`, e nao ha' funcao
	// nenhuma com esse nome. Antes o nome sozinho descartava a macro, e o
	// ForEachLoop nomeado simplesmente nao era encontrado.
	const bool bPureFunctionWins = !Statement.OutputName.IsEmpty()
		&& Lookup.IsConfident()
		&& Lookup.Function->HasAnyFunctionFlags(FUNC_BlueprintPure);

	if (!bPureFunctionWins)
	{
		if (UEdGraph* MacroGraph = FindStandardMacroGraph(Statement.NodeExpression))
		{
			UK2Node_MacroInstance* Node = AllocateNode<UK2Node_MacroInstance>();
			Node->SetMacroGraph(MacroGraph);
			FinalizeNode(Node);
			return Node;
		}
	}

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

			// O node anterior nao resolveu e virou comentario. Reclamar que o
			// rotulo nao casa com as saidas dele mandaria procurar o problema
			// na linha errada -- a causa ja' foi relatada uma linha acima.
			if (Owner->IsA<UEdGraphNode_Comment>())
			{
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

			// Este node ganhou rotulo: sai da lista de "parou sem escolher ramo".
			UnbranchedNodes.RemoveAll([Owner](const FUnbranchedNode& Entry)
			{
				return Entry.Node == Owner;
			});

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

		// Nodes de dado nao ocupam coluna: eles descem em pilha embaixo de quem
		// os consome, e quem cuida disso e' LayoutDataNodes(), no fim.
		if (!IsPureDataNode(Node))
		{
			Node->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Node->NodePosY = Frame.BaseY;
			++Frame.Column;
		}

		Result.CreatedNodes.Add(Node);

		// Um comentario ocupa o lugar de uma linha que nao resolveu; aplicar os
		// argumentos dela geraria uma segunda leva de erros sobre o mesmo problema.
		if (!Node->IsA<UEdGraphNode_Comment>())
		{
			ApplyArguments(Node, Statement);
		}

		if (!Statement.OutputName.IsEmpty())
		{
			if (Node->IsA<UEdGraphNode_Comment>())
			{
				// A linha nao virou node. Guardar o nome evita duas mensagens
				// que nao ajudam ninguem: "nao tem saida de dado" aqui, e
				// "`$nome` nao existe" em toda linha que o usasse depois.
				FailedOutputs.Add(Statement.OutputName, Statement.LineNumber);
			}
			else
			{
				RegisterOutput(Statement.OutputName, Node, Statement.LineNumber);
			}
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
			// Node puro: e' so' um valor, nao um passo. Ja' esta' na fila de
			// posicionamento e nao participa da cadeia de execucao.
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

			UnbranchedNodes.Add({ Node, Statement.LineNumber, FString::Join(Names, TEXT(", ")) });
		}
		else
		{
			Frame.PendingExec = FPinRef();
		}

		Frame.LastNode = Node;
	}

	// So' agora da' para saber quais ficaram mesmo sem rotulo.
	for (const FUnbranchedNode& Entry : UnbranchedNodes)
	{
		AddInfo(Entry.Line, FString::Printf(
			TEXT("Este node tem varias saidas (%s) e nenhum rotulo indentado. A cadeia parou aqui."),
			*Entry.Outputs));
	}

	LayoutDataNodes();
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
