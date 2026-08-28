#include "NodeScribeBuilder.h"

#include "NodeScribeAnimGraph.h"
#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "Animation/AnimBlueprint.h"
#include "K2Node_AnimGetter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_AsyncAction.h"
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
#include "K2Node_GenericToText.h"
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
#include "UObject/UnrealType.h"

using namespace NodeScribePropertyText;

namespace
{
	/**
	 * Um nome que alguem escreveu e o plugin nao casou com nada, numa linha, num
	 * arquivo so'.
	 *
	 * **Nao e' log de erro.** O erro ja' volta no retorno da chamada, ja' vai
	 * para o Message Log e ja' fica como comentario vermelho dentro do grafo --
	 * que e' onde ele serve, junto do problema, e de onde o proprio `read_graph`
	 * o traz de volta. Duplicar isso seria uma copia mais pobre.
	 *
	 * Isto responde outra pergunta, que hoje nao da' para responder de jeito
	 * nenhum: **quais apelidos faltam no catalogo**. Depois de algumas semanas de
	 * uso, `sort | uniq -c | sort -rn` neste arquivo e' uma lista de tarefas
	 * ordenada por frequencia -- "voce escreveu `Delay` sete vezes e o catalogo
	 * achou outra coisa".
	 *
	 * Nao ha' data na linha, de proposito: repetido precisa sair como linha
	 * *identica*, senao a contagem nao existe, e a ordem do arquivo ja' e'
	 * cronologica. Pelo mesmo motivo nao entram aqui os erros que nao sao de
	 * vocabulario -- tipo incompativel, pino vazio, evento duplicado. Esses tem
	 * causa conhecida, e virariam ruido numa contagem que existe para achar
	 * padrao.
	 */
	void AnotaNomeNaoResolvido(const TCHAR* Tipo, const FString& Nome, const FString& Contexto = FString())
	{
		FString Linha = FString(Tipo) + TEXT("  ") + Nome.TrimStartAndEnd();

		if (!Contexto.IsEmpty())
		{
			Linha += TEXT("  em  ") + Contexto.TrimStartAndEnd();
		}

		Linha += LINE_TERMINATOR;

		const FString Pasta = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"));
		IFileManager::Get().MakeDirectory(*Pasta, /*Tree*/ true);

		FFileHelper::SaveStringToFile(
			Linha,
			*FPaths::Combine(Pasta, TEXT("vocabulario.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			FILEWRITE_Append);
	}

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
			// SKEL_/REINST_/TRASHCLASS_ sao restos de compilacao de Blueprint.
			// Sem esse filtro o aviso dizia coisas como "`PreencherLista` e'
			// evento de `REINST_SKEL_WBP_TelaDeRemap_C_21`", que nao e' um nome
			// que exista para o usuario.
			const FString ClassName = ClassIt->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_"))
				|| ClassName.StartsWith(TEXT("REINST_"))
				|| ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

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

	/**
	 * Dispatcher por nome tolerante.
	 *
	 * O nome de um dispatcher e' digitado a mao e aceita espaco -- inclusive no
	 * fim, onde ninguem ve'. O parser apara os espacos da linha, entao a busca
	 * exata nunca acharia `OnVidaMudou ` a partir de `Call OnVidaMudou`.
	 */
	FMulticastDelegateProperty* FindDelegateByFriendlyName(UClass* Class, const FString& Name)
	{
		if (!Class || Name.IsEmpty())
		{
			return nullptr;
		}

		if (FMulticastDelegateProperty* Exact = FindFProperty<FMulticastDelegateProperty>(Class, FName(*Name)))
		{
			return Exact;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		for (TFieldIterator<FMulticastDelegateProperty> It(Class); It; ++It)
		{
			if (FNodeScribeCatalog::Normalize(It->GetName()) == Wanted)
			{
				return *It;
			}
		}

		return nullptr;
	}

	/**
	 * Nome de tipo escrito a mao -> tipo de pino da Unreal.
	 *
	 * Aceita o que aparece na interface (`Float`, `Timer Handle`,
	 * `EPlayerMappableKeySlot`), porque e' o que o usuario ve' -- e e' tambem o
	 * que o leitor escreve na volta.
	 */
	UScriptStruct* FindStructByFriendlyName(const FString& Name);
	UEnum* FindEnumByFriendlyName(const FString& Name);
	UClass* FindClassByFriendlyNameInternal(const FString& Name);

	bool ResolvePinTypeFromNameInternal(const FString& InTypeName, FEdGraphPinType& OutType)
	{
		FString TypeName = InTypeName.TrimStartAndEnd();
		bool bIsArray = false;

		// `BP_Pedra Class` e' a *classe*, nao uma instancia dela -- e' o que a
		// interface mostra e o que se usa para spawnar ator ou apontar
		// habilidade. Sem isto, so' dava para declarar referencia a objeto vivo,
		// e uma variavel de classe tinha que ser criada na mao.
		bool bIsClassReference = false;
		for (const TCHAR* Suffix : { TEXT(" Class"), TEXT(" Classe") })
		{
			if (TypeName.EndsWith(Suffix, ESearchCase::IgnoreCase))
			{
				TypeName = TypeName.LeftChop(FCString::Strlen(Suffix)).TrimEnd();
				bIsClassReference = true;
				break;
			}
		}

		// Mapa: `Mapa de Int64 para BP_Mirror`. As duas metades sao resolvidas
		// pela mesma funcao, entao valem os mesmos nomes de tipo de sempre.
		//
		// Vem antes do array porque a chave de um mapa nunca e' container: se a
		// linha disser `Mapa de Array de X para Y`, e' erro, e o `return false`
		// aqui embaixo e' que faz isso ser dito em voz alta.
		{
			static const TCHAR* const MapPrefixes[] = {
				TEXT("Mapa de "), TEXT("Map de "), TEXT("Map of "), TEXT("Mapa ")
			};

			for (const TCHAR* Prefix : MapPrefixes)
			{
				if (!TypeName.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					continue;
				}

				const FString Rest = TypeName.RightChop(FCString::Strlen(Prefix)).TrimStartAndEnd();

				FString KeyName;
				FString ValueName;
				if (!Rest.Split(TEXT(" para "), &KeyName, &ValueName, ESearchCase::IgnoreCase)
					&& !Rest.Split(TEXT(" to "), &KeyName, &ValueName, ESearchCase::IgnoreCase))
				{
					return false;
				}

				FEdGraphPinType KeyType;
				FEdGraphPinType ValueType;
				if (!ResolvePinTypeFromNameInternal(KeyName.TrimStartAndEnd(), KeyType)
					|| !ResolvePinTypeFromNameInternal(ValueName.TrimStartAndEnd(), ValueType))
				{
					return false;
				}

				if (KeyType.ContainerType != EPinContainerType::None
					|| ValueType.ContainerType != EPinContainerType::None)
				{
					return false;
				}

				OutType = KeyType;
				OutType.ContainerType = EPinContainerType::Map;
				OutType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
				return true;
			}
		}

		bool bIsSet = false;
		{
			static const TCHAR* const SetPrefixes[] = {
				TEXT("Conjunto de "), TEXT("Set de "), TEXT("Set of ")
			};

			for (const TCHAR* Prefix : SetPrefixes)
			{
				if (TypeName.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					TypeName = TypeName.RightChop(FCString::Strlen(Prefix)).TrimStartAndEnd();
					bIsSet = true;
					break;
				}
			}
		}

		static const TCHAR* const ArrayPrefixes[] = {
			TEXT("Array de "), TEXT("Array of "), TEXT("Lista de ")
		};

		for (const TCHAR* Prefix : ArrayPrefixes)
		{
			if (TypeName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				TypeName = TypeName.RightChop(FCString::Strlen(Prefix)).TrimStartAndEnd();
				bIsArray = true;
				break;
			}
		}

		static const TMap<FString, FName> Primitives = {
			{ TEXT("bool"),     UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("boolean"),  UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("booleano"), UEdGraphSchema_K2::PC_Boolean },
			{ TEXT("int"),      UEdGraphSchema_K2::PC_Int },
			{ TEXT("integer"),  UEdGraphSchema_K2::PC_Int },
			{ TEXT("inteiro"),  UEdGraphSchema_K2::PC_Int },
			{ TEXT("int64"),    UEdGraphSchema_K2::PC_Int64 },
			{ TEXT("byte"),     UEdGraphSchema_K2::PC_Byte },
			{ TEXT("float"),    UEdGraphSchema_K2::PC_Real },
			{ TEXT("double"),   UEdGraphSchema_K2::PC_Real },
			{ TEXT("real"),     UEdGraphSchema_K2::PC_Real },
			{ TEXT("string"),   UEdGraphSchema_K2::PC_String },
			{ TEXT("name"),     UEdGraphSchema_K2::PC_Name },
			{ TEXT("nome"),     UEdGraphSchema_K2::PC_Name },
			{ TEXT("text"),     UEdGraphSchema_K2::PC_Text },
			{ TEXT("texto"),    UEdGraphSchema_K2::PC_Text }
		};

		OutType = FEdGraphPinType();

		const FString Normalized = FNodeScribeCatalog::Normalize(TypeName);

		// Struct que casou so' pelo *nome de tela* perde para uma classe de nome
		// exato.
		//
		// `FTypedElementActorTag` e' declarada `USTRUCT(meta = (DisplayName =
		// "Actor"))`, e a busca de struct vem antes da de classe: sem esta regra,
		// `variavel X : Actor` criava aquela struct. Compila, parece certo no
		// painel, e nao e' um Actor -- o pior resultado possivel.
		//
		// So' o nome de tela cede. `Vector` e `TimerHandle` casam pelo nome
		// interno da struct e continuam ganhando de qualquer classe homonima, que
		// e' o comportamento que ja' servia. E a busca de classe so' conhece nome
		// interno (e o `_C` de Blueprint), entao "classe de nome exato" nao abre
		// uma segunda porta de nome de tela.
		UScriptStruct* Struct = FindStructByFriendlyName(TypeName);
		if (Struct
			&& FNodeScribeCatalog::Normalize(Struct->GetName()) != Normalized
			&& FindClassByFriendlyNameInternal(TypeName) != nullptr)
		{
			Struct = nullptr;
		}

		if (const FName* Category = Primitives.Find(Normalized))
		{
			OutType.PinCategory = *Category;

			if (*Category == UEdGraphSchema_K2::PC_Real)
			{
				OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}
		}
		else if (Struct)
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = Struct;
		}
		else if (UEnum* Enum = FindEnumByFriendlyName(TypeName))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutType.PinSubCategoryObject = Enum;
		}
		else if (UClass* Class = FindClassByFriendlyNameInternal(TypeName))
		{
			OutType.PinCategory = bIsClassReference
				? UEdGraphSchema_K2::PC_Class
				: UEdGraphSchema_K2::PC_Object;
			OutType.PinSubCategoryObject = Class;
		}
		else
		{
			return false;
		}

		// `Float Class` nao quer dizer nada: se o sufixo foi comido e o que
		// sobrou nao e' classe, o nome inteiro estava errado.
		if (bIsClassReference && OutType.PinCategory != UEdGraphSchema_K2::PC_Class)
		{
			return false;
		}

		if (bIsArray)
		{
			OutType.ContainerType = EPinContainerType::Array;
		}
		else if (bIsSet)
		{
			OutType.ContainerType = EPinContainerType::Set;
		}

		return true;
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

	UEnum* FindEnumByFriendlyName(const FString& Name)
	{
		const FString Normalized = FNodeScribeCatalog::Normalize(Name);
		if (Normalized.IsEmpty())
		{
			return nullptr;
		}

		for (TObjectIterator<UEnum> EnumIt; EnumIt; ++EnumIt)
		{
			if (FNodeScribeCatalog::Normalize(EnumIt->GetName()) == Normalized)
			{
				return *EnumIt;
			}
		}

		return nullptr;
	}

	/** Busca de classe por nome curto, aceitando tanto `BP_Boss` quanto `BP_Boss_C`. */
	UClass* FindClassByFriendlyNameInternal(const FString& Name)
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

namespace
{
	const UEdGraphSchema_K2* ResolveK2Schema(const UEdGraph* Graph)
	{
		const UEdGraphSchema_K2* Found = Graph ? Cast<UEdGraphSchema_K2>(Graph->GetSchema()) : nullptr;
		return Found ? Found : GetDefault<UEdGraphSchema_K2>();
	}
}

/** Estado de uma transcricao. Vive apenas durante Build(). */
class FNodeScribeBuildContext
{
public:
	FNodeScribeBuildContext(UEdGraph* InGraph, UBlueprint* InBlueprint, const FVector2D& InOrigin)
		: Graph(InGraph)
		, Blueprint(InBlueprint)
		, Origin(InOrigin)
		// O schema do proprio grafo, e nao o K2 generico: e' o
		// `UAnimationGraphSchema` que sabe ligar pose e que insere sozinho a
		// conversao entre espaco local e de componente. Para EventGraph e
		// funcao o schema do grafo ja' e' o K2, entao nada muda la'.
		, Schema(ResolveK2Schema(InGraph))
		, bAnimGraph(NodeScribeAnimGraph::IsAnimationGraph(InGraph))
		// Regra de transicao passa pelo mesmo schema, mas nao tem pose nenhuma:
		// e' logica booleana. Sem esta distincao o vocabulario de anim
		// competiria la' com o nome de uma funcao comum, e ganharia.
		, bPoseGraph(NodeScribeAnimGraph::IsAnimationGraph(InGraph)
			&& !(InGraph && InGraph->IsA<UAnimationTransitionGraph>()))
		// A regra de transicao tem um vocabulario que so' existe ali: os
		// getters de maquina de estado (`Time Remaining`, `Get Transition Time
		// Elapsed`). Eles nao sao chamada de funcao -- veja TryCreateAnimGetter.
		, bTransitionGraph(InGraph && InGraph->IsA<UAnimationTransitionGraph>())
	{
	}

	void Run(const TArray<FNodeScribeStatement>& Statements);

	FNodeScribeBuilder::FResult Result;

private:
	/** Um nivel de aninhamento: o corpo de um ramo, ou o nivel raiz. */
	struct FFrame
	{
		int32 Indent = 0;

		/** De onde sai a proxima ligacao de fluxo. Invalido = cadeia interrompida. */
		FPinRef PendingExec;

		/**
		 * O pino de entrada de pose que este bloco alimenta, no AnimGraph.
		 *
		 * A indentacao la' abre uma *entrada* -- `True Pose:` de um blend --, e
		 * quem enche essa entrada e' o resultado do bloco, ou seja o ultimo
		 * node dele. Em vez de guardar a ligacao para o fim, cada node liga por
		 * cima do anterior: o pino so' aceita um fio, entao o que sobra no fim
		 * e' exatamente o ultimo. Invalido no EventGraph e no nivel raiz.
		 */
		FPinRef FlowSink;

		/** Ultimo node criado neste nivel, dono dos rotulos que vierem a seguir. */
		UEdGraphNode* LastNode = nullptr;

		int32 BaseX = 0;
		int32 BaseY = 0;
		int32 Column = 0;

		/** Quantos ramos ja' foram abertos a partir de LastNode, para empilhar em Y. */
		int32 BranchesOpened = 0;
	};

	// --- Criacao de nodes -------------------------------------------------

	/**
	 * RF_Transactional e' o que faz o node entrar no sistema de undo.
	 *
	 * Sem ele, `FBlueprintEditorUtils::UpdateTransactionalFlags` conserta o node
	 * toda vez que o Blueprint abre e marca o asset como sujo -- e' de onde vem
	 * o "was updated to fix issues detected on load. Please resave." que
	 * reaparecia depois de compilar e salvar.
	 */
	template <typename TNode>
	TNode* AllocateNode(UEdGraph* Target = nullptr)
	{
		// O destino e' quase sempre o grafo da colagem; o parametro existe para
		// a maquina de estados, cujos estados nascem no sub-grafo dela.
		UEdGraph* Destination = Target ? Target : Graph;

		TNode* Node = NewObject<TNode>(Destination, NAME_None, RF_Transactional);
		Destination->AddNode(Node, false, false);
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

	/** Cria a variavel de uma linha `variavel Nome : Tipo = valor`. */
	void CreateDeclaredVariable(const FNodeScribeStatement& Statement);

	/**
	 * Cria todos os Custom Events antes de processar qualquer linha.
	 *
	 * Sem isto, chamar um evento definido mais abaixo no texto falha: a classe
	 * so' conhece a funcao depois que o node existe e o esqueleto e' regerado.
	 * No grafo a ordem nao importa, e no texto tambem nao deveria importar.
	 */
	void PreCreateCustomEvents(const TArray<FNodeScribeStatement>& Statements);

	/** Eventos ja' criados pela pre-passagem, por nome. */
	TMap<FString, UEdGraphNode*> PreCreatedEvents;

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

	/**
	 * Fluxo: execucao no EventGraph, pose no AnimGraph.
	 *
	 * Sao a mesma forma -- uma saida do node anterior entra no node seguinte --
	 * e por isso o percurso e' um so'. O que muda e' qual pino carrega o fluxo,
	 * e isso o grafo decide, nao a linha.
	 */
	bool IsFlowPin(const UEdGraphPin* Pin) const;
	UEdGraphPin* FindFlowInput(UEdGraphNode* Node) const;
	TArray<UEdGraphPin*> GetFlowOutputs(UEdGraphNode* Node) const;

	/** Nodes de anim criados nesta colagem, para conferir entrada de pose vazia no fim. */
	TArray<TWeakObjectPtr<UEdGraphNode>> AnimNodes;

	/**
	 * Nodes que ja' estavam no grafo e a colagem apenas reaproveitou.
	 *
	 * O Output Pose e' o caso: ele nasce com o AnimGraph e escrever a linha dele
	 * acha o que existe. Nao entra na contagem de criados nem e' reposicionado --
	 * mover o node que o usuario ja' arrumou seria estranho.
	 */
	TSet<UEdGraphNode*> AdoptedNodes;

	UEdGraphNode* TryCreateAnimNode(const FNodeScribeStatement& Statement, bool& bOutHandled);

	/**
	 * Os getters de maquina de estado, dentro de uma regra de transicao.
	 *
	 * `Time Remaining`, `Get Transition Time Elapsed`, `Get Relevant Anim Time
	 * Remaining`: no menu do editor eles aparecem como funcao, mas nao sao
	 * chamada de funcao. Sao `UK2Node_AnimGetter`, e o que os faz funcionar nao
	 * esta' em pino nenhum -- e' o estado de origem, guardado numa propriedade
	 * que o menu preenche ao criar o node.
	 *
	 * Sem este caminho, o nome caia no catalogo e achava a funcao homonima de
	 * `UAnimationStateMachineLibrary`, que existe, e' publica, entra no grafo, e
	 * pede dois pinos (`UpdateContext` e `Node`) que numa regra de transicao nao
	 * tem de onde vir. O resultado era um node plausivel que nao compila --
	 * exatamente o que o plugin promete nao fazer.
	 */
	UEdGraphNode* TryCreateAnimGetter(const FNodeScribeStatement& Statement, bool& bOutHandled);

	/**
	 * O bloco indentado debaixo de uma maquina de estados.
	 *
	 * Devolve o indice do primeiro statement que *nao* e' do bloco. Consome o
	 * bloco inteiro de uma vez, e nao linha a linha como o resto do percurso,
	 * porque estado e transicao nao vivem neste grafo: cada um tem o seu, e
	 * cada um desses e' construido por um contexto proprio.
	 */
	int32 BuildStateMachine(UAnimGraphNode_StateMachineBase* Machine, const TArray<FNodeScribeStatement>& Statements, int32 First);

	/**
	 * Constroi um sub-grafo com um contexto novo e traz de volta os diagnosticos.
	 *
	 * Devolve o node da ultima linha do bloco -- o resultado dele, pelo mesmo
	 * criterio de um galho de pose. Quem chama decide o que fazer com isso: a
	 * regra de uma transicao liga esse resultado no `Can Enter Transition`.
	 */
	UEdGraphNode* BuildSubGraph(UEdGraph* SubGraph, const TArray<FNodeScribeStatement>& Statements, int32 First, int32 End);

	/** Nodes criados dentro de sub-grafos. Entram no resultado so' no fim, ja' fora do layout. */
	TArray<UEdGraphNode*> NestedNodes;

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

	/** Uma opcao do painel de detalhes de um node, e onde o valor dela mora. */
	struct FNodeSetting
	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
	};

	/**
	 * A opcao de nome `Name`, no node ou dentro da struct que ele embrulha.
	 *
	 * `OutAvailable` sai preenchida em qualquer caso, com os nomes de tela de
	 * todas as opcoes -- e' o que a mensagem de erro precisa dizer quando o node
	 * nao tem pino nenhum.
	 */
	static bool FindNodeSetting(UEdGraphNode* Node, const FString& Name, FNodeSetting& Out, TArray<FString>& OutAvailable);

	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	FVector2D Origin = FVector2D::ZeroVector;
	const UEdGraphSchema_K2* Schema = nullptr;

	/** Grafo de animacao: AnimGraph, interior de estado ou regra de transicao. */
	bool bAnimGraph = false;

	/** Dos tres acima, os dois que de fato tem pose. */
	bool bPoseGraph = false;

	/** O terceiro: a regra de transicao, que e' logica booleana. */
	bool bTransitionGraph = false;

	TArray<FFrame> Frames;

	/** Saidas nomeadas com `nome = ...`, disponiveis para `$nome`. */
	TMap<FString, FPinRef> NamedOutputs;

	/** Nomes cuja linha nao resolveu, e em que linha isso aconteceu. */
	TMap<FString, int32> FailedOutputs;

	/**
	 * Linhas que criaram node e nao deram nome a' saida.
	 *
	 * Existe por causa de um erro que se comete o tempo todo: escrever
	 * `evento X` e depois `$X`, esquecendo que so' `nome = evento X` cria a
	 * referencia. Sem isso o plugin criava uma variavel chamada X, que nao e'
	 * nem de longe o que a pessoa quis dizer.
	 */
	struct FUnnamedLine
	{
		int32 Line = 0;
		FString Expression;
	};

	TArray<FUnnamedLine> UnnamedLines;

	/**
	 * Gets que o plugin criou sozinho por causa de um `$Variavel`.
	 *
	 * Se a ligacao que os justificava nao aconteceu -- tipo errado, pino que
	 * nao existe --, eles ficam no grafo sem servir para nada. Como nao foram
	 * pedidos por uma linha do texto, somem no fim em vez de virar entulho.
	 */
	TArray<UEdGraphNode*> AutoCreatedGets;

	/**
	 * Nodes de conversao que o schema inseriu sozinho dentro de Connect().
	 *
	 * Ninguem os escreveu, entao eles nao entram em CreatedNodes -- a contagem
	 * que o usuario ve' e' de linhas que ele mandou. Mas eles precisam ser
	 * posicionados como qualquer node de dado, senao ficam parados onde os dois
	 * lados estavam na hora da ligacao, que o layout depois abandona.
	 */
	TArray<UEdGraphNode*> ConversionNodes;

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

	/** Quantos saltos de dado ainda faltam ate' o fim da cadeia. 0 = ninguem consome. */
	int32 DataChainRemaining(UEdGraphNode* Node) const;

	/**
	 * Posiciona todos os nodes de dado, depois que o grafo inteiro existe.
	 *
	 * Tem que ser no fim: um Get criado por `$Variavel` nasce durante a
	 * ligacao, e o node que o consome pode ainda nao ter posicao nenhuma. Era
	 * o que jogava esse Get para longe -- ele era ancorado num (0,0).
	 */
	void LayoutDataNodes();

	/**
	 * Posiciona a arvore de pose do AnimGraph.
	 *
	 * O layout em colunas do EventGraph nao serve aqui: la' a cadeia e' uma
	 * fila, e aqui e' uma arvore que converge no Output Pose. A largura de um
	 * galho so' se sabe depois que ele inteiro existe, entao isto roda no fim,
	 * a partir da raiz, andando para tras pelos pinos de entrada.
	 */
	void LayoutAnimNodes();

	/** Coloca `Node` e o que o alimenta. Devolve o Y do node. */
	int32 PlaceAnimNode(UEdGraphNode* Node, int32 Depth, int32 RootX, int32 RootY, int32& NextRow, TSet<UEdGraphNode*>& Visited);

	/**
	 * Liga o fim da cadeia no Output Pose, quando o texto nao o escreveu.
	 *
	 * Um AnimGraph que so' diz `Idle` quis dizer "Idle e' a pose". Escrever
	 * `Output Pose` numa segunda linha e' ruido: ligar a saida no fim da cadeia
	 * e' do plugin, como toda ligacao que o formato nao escreve.
	 */
	void ConnectOutputPose();

	/** Reclama de entrada de pose vazia: pose vazia nao quebra nada, so' fica parada. */
	void ReportEmptyPoseInputs();
};

bool FNodeScribeBuildContext::IsPureDataNode(UEdGraphNode* Node) const
{
	return Node
		&& !Node->IsA<UEdGraphNode_Comment>()
		&& !FindFlowInput(Node)
		&& GetFlowOutputs(Node).Num() == 0;
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
			if (Pin->Direction == EGPD_Output && !IsFlowPin(Pin) && Pin->LinkedTo.Num() > 0)
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

int32 FNodeScribeBuildContext::DataChainRemaining(UEdGraphNode* Node) const
{
	int32 Hops = 0;
	UEdGraphNode* Current = Node;

	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		UEdGraphNode* Next = nullptr;

		for (UEdGraphPin* Pin : Current->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsFlowPin(Pin) && Pin->LinkedTo.Num() > 0)
			{
				Next = Pin->LinkedTo[0]->GetOwningNodeUnchecked();
				break;
			}
		}

		if (!Next)
		{
			return Hops;
		}

		++Hops;
		Current = Next;
	}

	return Hops;
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

	// Os de conversao entram junto com os que o texto pediu: para o layout eles
	// sao nodes de dado como quaisquer outros, e quem os consome ja' e' achado
	// pela mesma travessia. A lista e' explicita, e nao uma varredura do grafo
	// atras de nodes de dado sem posicao, porque uma varredura arrastaria
	// tambem o que ja' estava no grafo antes desta colagem.
	TArray<UEdGraphNode*> DataCandidates = Result.CreatedNodes;
	DataCandidates.Append(ConversionNodes);

	for (UEdGraphNode* Node : DataCandidates)
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

		// Quem alimenta a execucao diretamente fica logo abaixo dela, e as
		// dependencias descem a partir dai'. Lendo de cima para baixo voce vai
		// do valor pronto para de onde ele veio.
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
	//
	// Aqui nao ha' node de execucao para medir distancia, entao a ordem sai da
	// propria cadeia de dados: o node que ninguem consome fica no topo e as
	// dependencias descem -- mesma leitura do caso com execucao. Sem isso a
	// ordem era a de criacao, que e' a do texto, e saia de cabeca para baixo.
	if (Orphans.Num() > 0 && Frames.Num() > 0)
	{
		Orphans.Sort([this](UEdGraphNode& A, UEdGraphNode& B)
		{
			return DataChainRemaining(&A) < DataChainRemaining(&B);
		});

		const FFrame& Frame = Frames[0];

		for (int32 Index = 0; Index < Orphans.Num(); ++Index)
		{
			Orphans[Index]->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Orphans[Index]->NodePosY = Frame.BaseY + (Index * DataRowHeight);
		}
	}
}

int32 FNodeScribeBuildContext::PlaceAnimNode(
	UEdGraphNode* Node, int32 Depth, int32 RootX, int32 RootY, int32& NextRow, TSet<UEdGraphNode*>& Visited)
{
	if (!Node || Visited.Contains(Node))
	{
		return RootY;
	}
	Visited.Add(Node);

	// Um galho por pino de entrada, na ordem em que os pinos aparecem no node --
	// que e' a ordem que o usuario ve' na tela.
	TArray<int32> ChildRows;
	for (UEdGraphPin* Pin : NodeScribeAnimGraph::GetPoseInputs(Node))
	{
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			UEdGraphNode* Child = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
			if (Child && Result.CreatedNodes.Contains(Child))
			{
				ChildRows.Add(PlaceAnimNode(Child, Depth + 1, RootX, RootY, NextRow, Visited));
			}
		}
	}

	int32 Y = 0;
	if (ChildRows.Num() == 0)
	{
		// Ponta da arvore: ganha a proxima linha livre. E' daqui que a altura
		// do grafo inteiro sai.
		Y = RootY + (NextRow * BranchRowHeight);
		++NextRow;
	}
	else
	{
		// No meio, fica na altura media do que o alimenta -- o fio entra reto
		// quando ha' um so' galho, e centralizado quando ha' varios.
		int32 Sum = 0;
		for (int32 Row : ChildRows)
		{
			Sum += Row;
		}
		Y = Sum / ChildRows.Num();
	}

	// O node adotado (o Output Pose) fica onde esta': ele e' a origem.
	if (!AdoptedNodes.Contains(Node))
	{
		Node->NodePosX = RootX - (Depth * ColumnWidth);
		Node->NodePosY = Y;
	}

	return Y;
}

void FNodeScribeBuildContext::LayoutAnimNodes()
{
	if (!bAnimGraph)
	{
		return;
	}

	UEdGraphNode* Root = NodeScribeAnimGraph::FindOutputPose(Graph);
	if (!Root)
	{
		return;
	}

	int32 NextRow = 0;
	TSet<UEdGraphNode*> Visited;
	PlaceAnimNode(Root, 0, Root->NodePosX, Root->NodePosY, NextRow, Visited);

	// O que nao chegou ao Output Pose nao esta' na arvore -- galho que o texto
	// criou e nao ligou em nada. Fica numa faixa propria abaixo, visivel, em
	// vez de empilhado por cima da arvore.
	int32 LooseRow = NextRow + 1;
	for (const TWeakObjectPtr<UEdGraphNode>& Weak : AnimNodes)
	{
		UEdGraphNode* Node = Weak.Get();
		if (!Node || Visited.Contains(Node))
		{
			continue;
		}

		Node->NodePosX = Root->NodePosX - ColumnWidth;
		Node->NodePosY = Root->NodePosY + (LooseRow * BranchRowHeight);
		++LooseRow;
	}
}

void FNodeScribeBuildContext::ConnectOutputPose()
{
	if (!bAnimGraph || Frames.Num() == 0)
	{
		return;
	}

	UEdGraphNode* Root = NodeScribeAnimGraph::FindOutputPose(Graph);
	UEdGraphPin* RootPose = Root ? NodeScribeAnimGraph::FindPoseInput(Root) : nullptr;
	if (!RootPose)
	{
		return;
	}

	if (RootPose->LinkedTo.Num() == 0)
	{
		// Frames[0] e' o nivel raiz: o que sobrou ali e' a saida do ultimo node
		// que o texto pos no fio principal.
		if (UEdGraphPin* End = Frames[0].PendingExec.Resolve())
		{
			if (NodeScribeAnimGraph::IsPosePin(End))
			{
				Connect(End, RootPose, 0);
			}
		}
	}

	if (RootPose->LinkedTo.Num() == 0 && AnimNodes.Num() > 0)
	{
		// O grafo tem nodes de anim e mesmo assim nada chega na saida. Sem isto
		// o aviso nao sairia: o Output Pose nao e' node criado por esta colagem,
		// e ReportEmptyPoseInputs so' olha os que sao.
		AddWarning(0, TEXT("Nada chegou no Output Pose. O personagem fica na pose de referencia."));
	}
}

void FNodeScribeBuildContext::ReportEmptyPoseInputs()
{
	if (!bAnimGraph)
	{
		return;
	}

	TArray<FString> Empty;

	for (const TWeakObjectPtr<UEdGraphNode>& Weak : AnimNodes)
	{
		UEdGraphNode* Node = Weak.Get();
		if (!Node)
		{
			continue;
		}

		for (UEdGraphPin* Pin : NodeScribeAnimGraph::GetPoseInputs(Node))
		{
			if (Pin->LinkedTo.Num() == 0)
			{
				Empty.Add(FString::Printf(TEXT("%s.%s"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*Pin->PinName.ToString()));
			}
		}
	}

	if (Empty.Num() == 0)
	{
		return;
	}

	// Pose vazia e' o buraco silencioso deste tipo de grafo: compila, roda, e o
	// personagem simplesmente fica na pose de referencia -- de bracos abertos.
	AddWarning(0, FString::Printf(
		TEXT("Entrada de pose sem nada ligado: %s. O que sair dali e' a pose de referencia."),
		*FString::Join(Empty, TEXT(", "))));
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

bool FNodeScribeBuildContext::IsFlowPin(const UEdGraphPin* Pin) const
{
	return IsExecPin(Pin) || (bAnimGraph && NodeScribeAnimGraph::IsPosePin(Pin));
}

UEdGraphPin* FNodeScribeBuildContext::FindFlowInput(UEdGraphNode* Node) const
{
	if (UEdGraphPin* Exec = FindExecInput(Node))
	{
		return Exec;
	}
	return bAnimGraph ? NodeScribeAnimGraph::FindPoseInput(Node) : nullptr;
}

TArray<UEdGraphPin*> FNodeScribeBuildContext::GetFlowOutputs(UEdGraphNode* Node) const
{
	TArray<UEdGraphPin*> Outputs = GetExecOutputs(Node);
	if (Outputs.Num() == 0 && bAnimGraph)
	{
		Outputs = NodeScribeAnimGraph::GetPoseOutputs(Node);
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

	// Convencao de codigo da Unreal: bool se chama `bXYOverride`, e a interface
	// mostra "XY Override". Ninguem digita o `b`, e o pino nem sempre tem nome
	// amigavel para cobrir isso.
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction != Direction || Pin->bHidden)
		{
			continue;
		}

		const FString PinName = Pin->PinName.ToString();
		if (PinName.Len() > 1 && PinName[0] == TEXT('b') && FChar::IsUpper(PinName[1])
			&& FNodeScribeCatalog::Normalize(PinName.RightChop(1)) == Wanted)
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

	auto IsSplittableStruct = [this](UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Output && !IsExecPin(Pin) && Pin->SubPins.Num() == 0
			&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
			&& Schema->CanSplitStructPin(*Pin);
	};

	// --- 1a tentativa: o nome pedido comeca com o nome do pino ------------
	//
	// `Selected Key Key` comeca com `Selected Key`: e' parte dessa struct.
	for (UEdGraphPin* Pin : Candidates)
	{
		if (!IsSplittableStruct(Pin))
		{
			continue;
		}

		const FString PinName = FNodeScribeCatalog::Normalize(Pin->PinName.ToString());
		const FString FriendlyName = Pin->PinFriendlyName.IsEmpty()
			? PinName
			: FNodeScribeCatalog::Normalize(Pin->PinFriendlyName.ToString());

		if (!Wanted.StartsWith(PinName) && !Wanted.StartsWith(FriendlyName))
		{
			continue;
		}

		Schema->SplitPin(Pin, false);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Output))
		{
			return Found;
		}
	}

	// --- 2a tentativa: o nome pedido e' um campo da struct ----------------
	//
	// `$velocidade.Z` com o pino chamado `ReturnValue`. O prefixo nao ajuda --
	// "z" nao comeca com "returnvalue" --, e o `Get Velocity` de um ator e' o
	// caso mais comum que existe: o pino de retorno de uma funcao quase nunca
	// tem nome proprio.
	//
	// Perguntamos a struct antes de dividir. Dividir para descobrir mudaria o
	// grafo procurando, e um pino dividido a toa fica visivelmente diferente do
	// que o texto pediu.
	TArray<UEdGraphPin*> WithField;
	for (UEdGraphPin* Pin : Candidates)
	{
		if (!IsSplittableStruct(Pin))
		{
			continue;
		}

		const UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
		if (!Struct)
		{
			continue;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Field = *It;
			if (FNodeScribeCatalog::Normalize(Field->GetName()) == Wanted
				|| FNodeScribeCatalog::Normalize(Field->GetAuthoredName()) == Wanted)
			{
				WithField.Add(Pin);
				break;
			}
		}
	}

	// Dois pinos de struct com um campo `Z` cada: dividir um deles seria
	// escolher, e o node escolhido compila e roda com o valor do outro.
	if (WithField.Num() > 1)
	{
		return nullptr;
	}

	if (WithField.Num() == 1)
	{
		UEdGraphPin* Parent = WithField[0];
		const FString ParentName = FNodeScribeCatalog::Normalize(Parent->PinName.ToString());

		Schema->SplitPin(Parent, false);

		if (UEdGraphPin* Found = FindPinByFuzzyName(Node, PinPath, EGPD_Output))
		{
			return Found;
		}

		// O sub-pino nasce com o nome do pai colado no do campo: dividir
		// `ReturnValue` da' `ReturnValue_X`, `ReturnValue_Y`, `ReturnValue_Z`.
		// Procurar por `Z` puro nao acha nenhum deles -- e a busca parava aqui,
		// depois de ja' ter dividido o pino, o que deixava o grafo mexido e a
		// ligacao por fazer.
		for (UEdGraphPin* Sub : Parent->SubPins)
		{
			if (!Sub)
			{
				continue;
			}

			const FString SubName = FNodeScribeCatalog::Normalize(Sub->PinName.ToString());
			if (SubName == ParentName + Wanted || SubName.EndsWith(Wanted))
			{
				return Sub;
			}
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
	if (Schema->TryCreateConnection(From, To))
	{
		// Quando isso acontece, os dois pinos nao ficam ligados um no outro: o
		// que passou a alimentar `To` e' a saida do node de conversao. E' assim
		// que ele se deixa reconhecer -- e precisa ser reconhecido aqui, porque
		// depois da ligacao nada mais distingue esse node de um que o texto
		// pediu. Sem isso ele fica na posicao em que os dois lados estavam
		// agora, que LayoutDataNodes() logo abandona, e o resultado e' um node
		// solto no meio do nada com dois fios atravessando o grafo inteiro.
		if (!From->LinkedTo.Contains(To))
		{
			const UEdGraphNode* SourceNode = From->GetOwningNodeUnchecked();
			for (UEdGraphPin* Feeding : To->LinkedTo)
			{
				UEdGraphNode* Inserted = Feeding ? Feeding->GetOwningNodeUnchecked() : nullptr;
				if (Inserted && Inserted != SourceNode && !Result.CreatedNodes.Contains(Inserted))
				{
					ConversionNodes.AddUnique(Inserted);
				}
			}
		}
	}
	else
	{
		// Dizer os dois tipos e' o que resolve o caso mais confuso: um nome que
		// existe, mas nao e' o que voce quis dizer. `$Slot` acha o `Slot` que
		// todo UWidget herda, e nao a variavel que voce ia criar.
		AddWarning(Line, FString::Printf(
			TEXT("`%s` e' %s, e o pino `%s` espera %s. A ligacao nao foi feita."),
			*From->PinName.ToString(),
			*UEdGraphSchema_K2::TypeToText(From->PinType).ToString(),
			*To->PinName.ToString(),
			*UEdGraphSchema_K2::TypeToText(To->PinType).ToString()));
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
			if (UClass* Found = FindClassByFriendlyNameInternal(Value))
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

	AnotaNomeNaoResolvido(TEXT("saida"), PinPath, TEXT("$") + BaseName);

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
		AutoCreatedGets.Add(GetNode);

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

	// Antes de tratar como variavel: o nome pode ser de uma linha anterior que
	// simplesmente nao foi nomeada. Criar uma variavel nesse caso seria obedecer
	// a letra e ignorar a intencao -- e o texto costuma vir de quem escreveu
	// `evento X` e logo abaixo `$X`.
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		// Nome curto casa por acaso com qualquer coisa; exigir tres letras
		// evita transformar coincidencia em diagnostico.
		if (Wanted.Len() >= 3)
		{
			for (const FUnnamedLine& Candidate : UnnamedLines)
			{
				if (FNodeScribeCatalog::Normalize(Candidate.Expression).Contains(Wanted))
				{
					AddError(Line, FString::Printf(
						TEXT("`$%s` nao existe, mas a linha %d parece ser o que voce quis. ")
						TEXT("Para poder referencia-la, de' nome a ela: escreva `%s = ` no comeco dela."),
						*Name, Candidate.Line, *Name));

					return nullptr;
				}
			}
		}
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
		AutoCreatedGets.Add(GetNode);

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

bool FNodeScribeBuildContext::FindNodeSetting(UEdGraphNode* Node, const FString& Name, FNodeSetting& Out, TArray<FString>& OutAvailable)
{
	const FString Wanted = FNodeScribeCatalog::Normalize(Name);

	// Duas camadas: as propriedades do node, e as de dentro da struct que ele
	// embrulha. Num `UAnimGraphNode_SequencePlayer` o que interessa mora na
	// segunda -- `bLoopAnimation` e `PlayRate` sao campos do `FAnimNode_*`, e o
	// node so' a carrega.
	TArray<FNodeSetting> Found;

	auto Consider = [&](FProperty* Property, void* Container)
	{
		if (!IsNodeSetting(Property))
		{
			return;
		}

		const FString Display = DisplayName(Property);
		OutAvailable.AddUnique(Display);

		if (FNodeScribeCatalog::Normalize(Display) == Wanted
			|| FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted)
		{
			Found.Add({ Property, Property->ContainerPtrToValuePtr<void>(Container) });
		}
	};

	for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
	{
		FProperty* Property = *It;

		if (FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			// So' a struct de anim se abre -- e' onde `Loop Animation` mora. Ver
			// IsAnimNodeStruct: abrir qualquer uma faria os campos de um `FGuid`
			// virarem opcoes chamadas `A`, `B`, `C` e `D`.
			if (IsNodeSetting(Property) && IsAnimNodeStruct(AsStruct))
			{
				void* StructPtr = AsStruct->ContainerPtrToValuePtr<void>(Node);
				for (TFieldIterator<FProperty> Inner(AsStruct->Struct); Inner; ++Inner)
				{
					Consider(*Inner, StructPtr);
				}
				continue;
			}
		}

		Consider(Property, Node);
	}

	// Duas propriedades com o mesmo nome de tela, em camadas diferentes: escolher
	// uma acerta metade das vezes, e a metade errada grava valor em asset.
	if (Found.Num() != 1)
	{
		return false;
	}

	Out = Found[0];
	return true;
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
				// Nem tudo que se ajusta num node e' pino. `Loop Animation` e
				// `Play Rate` de um asset player, `Blend Time` de uma transicao:
				// ficam no painel de detalhes, e antes disto a resposta era "o
				// node nao tem pino `Loop Animation`. Pinos de entrada:" -- com
				// a lista vazia, porque um Sequence Player nao tem nenhum. Quem
				// lesse aquilo concluiria que a Engine nao tem essa opcao.
				FNodeSetting Setting;
				TArray<FString> Settings;

				if (FindNodeSetting(Node, Arg.PinName, Setting, Settings))
				{
					if (Arg.bIsReference)
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s` e' uma opcao do painel de detalhes, nao um pino: aceita valor fixo, ")
							TEXT("nao `$%s`."), *Arg.PinName, *Arg.Value));
						continue;
					}

					Node->Modify();

					FString Error;
					if (!TextToValue(Setting.Property, Setting.ValuePtr, Arg.Value, Error))
					{
						AddError(Statement.LineNumber, FString::Printf(
							TEXT("`%s = %s` nao entrou: %s"), *Arg.PinName, *Arg.Value, *Error));
						continue;
					}

					Node->PostEditChange();

					AddInfo(Statement.LineNumber, FString::Printf(
						TEXT("`%s` nao e' pino: entrou como opcao do node."), *Arg.PinName));
					continue;
				}

				TArray<FString> Available;
				for (UEdGraphPin* Candidate : PositionalPins)
				{
					Available.Add(Candidate->PinName.ToString());
				}

				// Nome de pino errado e' a mesma familia: foi assim que apareceu
				// que o exemplo `Break Vector (In Vec = ...)` da documentacao
				// nunca funcionou -- o pino se chama como a struct.
				AnotaNomeNaoResolvido(TEXT("pino"), Arg.PinName, Statement.NodeExpression);

				// Sem pino nenhum, dizer "pinos de entrada: (nada)" nao ajuda.
				// O que responde a pergunta seguinte e' a lista das opcoes.
				const FString Onde = Available.Num() > 0
					? FString::Printf(TEXT("Pinos de entrada: %s"), *FString::Join(Available, TEXT(", ")))
					: (Settings.Num() > 0
						? FString::Printf(TEXT("Este node nao tem pino de entrada. Opcoes do painel: %s"),
							*FString::Join(Settings, TEXT(", ")))
						: TEXT("Este node nao tem pino de entrada nem opcao ajustavel."));

				AddError(Statement.LineNumber, FString::Printf(
					TEXT("O node nao tem pino `%s`. %s"), *Arg.PinName, *Onde));
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

void FNodeScribeBuildContext::PreCreateCustomEvents(const TArray<FNodeScribeStatement>& Statements)
{
	if (!Blueprint)
	{
		return;
	}

	bool bCreatedAny = false;

	for (const FNodeScribeStatement& Statement : Statements)
	{
		if (Statement.bIsLabel || Statement.bIsVariable)
		{
			continue;
		}

		const FString Expression = Statement.NodeExpression.TrimStartAndEnd();

		FString EventName;
		if (Expression.StartsWith(TEXT("Event "), ESearchCase::IgnoreCase))
		{
			EventName = Expression.RightChop(6);
		}
		else if (Expression.StartsWith(TEXT("Evento "), ESearchCase::IgnoreCase))
		{
			EventName = Expression.RightChop(7);
		}
		else
		{
			continue;
		}

		EventName.TrimStartAndEndInline();

		// `X de Y` e' evento de dispatcher, e `__DelegateSignature` e' recusado:
		// nenhum dos dois vira Custom Event, entao nao entram aqui.
		if (EventName.IsEmpty()
			|| EventName.EndsWith(TEXT("__DelegateSignature"), ESearchCase::CaseSensitive)
			|| EventName.Contains(TEXT(" de "), ESearchCase::IgnoreCase)
			|| EventName.Contains(TEXT(" of "), ESearchCase::IgnoreCase)
			|| PreCreatedEvents.Contains(EventName))
		{
			continue;
		}

		// Evento que a classe pai oferece vira override, nao Custom Event.
		bool bIsParentEvent = false;
		if (UClass* ParentClass = Blueprint->ParentClass.Get())
		{
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
						bIsParentEvent = true;
						break;
					}
				}
			}
		}

		if (bIsParentEvent)
		{
			continue;
		}

		const FName WantedName(*EventName);
		if (FindExistingEvent([&](UEdGraphNode* Existing)
			{
				const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Existing);
				return CustomEvent && CustomEvent->CustomFunctionName == WantedName;
			}))
		{
			continue;
		}

		UK2Node_CustomEvent* Node = AllocateNode<UK2Node_CustomEvent>();
		Node->CustomFunctionName = WantedName;
		FinalizeNode(Node);

		PreCreatedEvents.Add(EventName, Node);
		bCreatedAny = true;
	}

	// Uma regeracao para todos, em vez de uma por evento.
	if (bCreatedAny)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::GenerateBlueprintSkeleton(Blueprint, true);
	}
}

void FNodeScribeBuildContext::CreateDeclaredVariable(const FNodeScribeStatement& Statement)
{
	if (!Blueprint || Statement.VariableName.IsEmpty())
	{
		return;
	}

	// Declarar de novo nao e' erro: colar o mesmo texto duas vezes tem que ser
	// inofensivo, e o texto que o leitor produz sempre traz as declaracoes.
	if (FindPropertyByFriendlyName(GetSelfClass(), Statement.VariableName))
	{
		AddInfo(Statement.LineNumber, FString::Printf(
			TEXT("`%s` ja' existe; nao mexi nela."), *Statement.VariableName));
		return;
	}

	FEdGraphPinType PinType;
	if (!ResolvePinTypeFromNameInternal(Statement.VariableType, PinType))
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("Nao reconheci o tipo `%s`. Use o nome que aparece na interface, como Float, Name, Timer Handle."),
			*Statement.VariableType));
		return;
	}

	// Widget do Designer nao se cria por declaracao: a variavel nasce ao
	// colocar o widget na tela e marcar Is Variable. Uma variavel com o mesmo
	// nome compilaria e nunca apontaria para o widget -- e ainda atrapalharia
	// quando o widget de verdade fosse criado.
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		const UClass* WidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.Widget"));
		const UClass* UserWidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.UserWidget"));
		const UClass* VariableClass = Cast<UClass>(PinType.PinSubCategoryObject.Get());

		const bool bIsWidgetBlueprint = UserWidgetClass
			&& Blueprint->ParentClass
			&& Blueprint->ParentClass->IsChildOf(UserWidgetClass);

		if (bIsWidgetBlueprint && WidgetClass && VariableClass && VariableClass->IsChildOf(WidgetClass))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s` e' um widget: crie no Designer e marque `Is Variable`, nao aqui."),
				*Statement.VariableName));
			return;
		}
	}

	if (FBlueprintEditorUtils::AddMemberVariable(
		Blueprint, FName(*Statement.VariableName), PinType, Statement.VariableDefault))
	{
		AddInfo(Statement.LineNumber, FString::Printf(
			TEXT("Criei a variavel `%s`."), *Statement.VariableName));
	}
	else
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("Nao consegui criar `%s`. O nome pode estar em uso por algo herdado."),
			*Statement.VariableName));
	}
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

// ---------------------------------------------------------------------------
// AnimGraph
// ---------------------------------------------------------------------------

/**
 * Uma linha dentro de um grafo de animacao.
 *
 * Tres formas, nesta ordem: o Output Pose que ja' existe, um node de anim pelo
 * nome do menu, e um asset de animacao -- que aqui vira node em vez de ser
 * recusado. Fora do AnimGraph nome solto de asset continua sendo recusado, e
 * por bom motivo: la' nao ha' node obvio para embrulhar o asset. Aqui ha' um
 * so', e e' o mesmo que arrastar o asset para o grafo produz.
 *
 * A classe de node vem antes do asset porque o vocabulario e' fechado e o de
 * assets nao: uma animacao chamada `Blend` nao pode roubar o node `Blend`.
 */
UEdGraphNode* FNodeScribeBuildContext::TryCreateAnimNode(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = false;

	const FString Expression = Statement.NodeExpression.TrimStartAndEnd();
	const FString Normalized = FNodeScribeCatalog::Normalize(Expression);

	if (Expression.IsEmpty())
	{
		return nullptr;
	}

	// --- Output Pose ------------------------------------------------------
	if (Normalized == TEXT("outputpose")
		|| Normalized == TEXT("posedesaida")
		|| Normalized == TEXT("finalanimationpose")
		|| Normalized == TEXT("result")
		|| Normalized == TEXT("resultado"))
	{
		bOutHandled = true;

		if (UEdGraphNode* Existing = NodeScribeAnimGraph::FindOutputPose(Graph))
		{
			AdoptedNodes.Add(Existing);
			return Existing;
		}

		AddError(Statement.LineNumber,
			TEXT("Este grafo nao tem Output Pose. Ele nasce com o AnimGraph -- ")
			TEXT("se sumiu, e' o grafo que esta' errado, e criar outro nao conserta."));

		return CreateErrorComment(Statement,
			TEXT("Nao achei o Output Pose deste grafo.\n\n")
			TEXT("Ele nao se cria: nasce junto com o AnimGraph."));
	}

	// --- Node de anim pelo nome -------------------------------------------
	const NodeScribeAnimGraph::FLookup Lookup = NodeScribeAnimGraph::FindNodeClass(Expression);

	if (Lookup.IsAmbiguous())
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` e' o nome de mais de um node de anim: %s."),
			*Expression, *FString::Join(Lookup.Candidates, TEXT(", "))));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("`%s` e' ambiguo.\n\nCandidatos: %s"),
			*Expression, *FString::Join(Lookup.Candidates, TEXT("\n"))));
	}

	if (Lookup.IsConfident())
	{
		bOutHandled = true;

		UAnimGraphNode_Base* Node = NewObject<UAnimGraphNode_Base>(Graph, Lookup.NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		FinalizeNode(Node);

		// `Locomocao = Maquina de Estados` batiza a maquina. O nome de uma
		// maquina de estados e' o do sub-grafo dela, e sem isto toda maquina
		// nasceria "New State Machine" -- inclusive a segunda, que viraria
		// "New State Machine 1".
		if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
		{
			if (!Statement.OutputName.IsEmpty() && Machine->EditorStateMachineGraph)
			{
				FBlueprintEditorUtils::RenameGraph(Machine->EditorStateMachineGraph, Statement.OutputName);
			}
		}

		AnimNodes.Add(Node);
		return Node;
	}

	// --- Asset de animacao ------------------------------------------------
	const NodeScribeAnimGraph::FAssetLookup Asset = NodeScribeAnimGraph::FindAnimationAsset(Expression);

	if (Asset.IsAmbiguous())
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("Ha' mais de uma animacao chamada `%s`. Escreva o caminho completo: %s"),
			*Expression, *FString::Join(Asset.Candidates, TEXT(", "))));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("`%s` e' o nome de mais de uma animacao.\n\nEscreva o caminho:\n%s"),
			*Expression, *FString::Join(Asset.Candidates, TEXT("\n"))));
	}

	if (!Asset.IsConfident())
	{
		return nullptr;
	}

	UClass* NodeClass = NodeScribeAnimGraph::NodeClassForAsset(Asset.Asset);
	if (!NodeClass)
	{
		bOutHandled = true;

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` e' um asset de animacao, mas nao ha' node de AnimGraph que toque um %s."),
			*Expression, *Asset.Asset->GetClass()->GetName()));

		return CreateErrorComment(Statement, FString::Printf(
			TEXT("Nenhum node toca `%s`."), *Expression));
	}

	bOutHandled = true;

	UAnimGraphNode_AssetPlayerBase* Player =
		NewObject<UAnimGraphNode_AssetPlayerBase>(Graph, NodeClass, NAME_None, RF_Transactional);
	Graph->AddNode(Player, false, false);
	Player->CreateNewGuid();
	Player->SetAnimationAsset(Asset.Asset);

	// Depois do asset: os pinos de um BlendSpace dependem dos eixos dele, e
	// alocar antes daria um node com os pinos do espaco errado.
	FinalizeNode(Player);

	AnimNodes.Add(Player);

	AddInfo(Statement.LineNumber, FString::Printf(
		TEXT("`%s` virou um %s."), *Expression,
		*NodeClass->GetName().Replace(TEXT("AnimGraphNode_"), TEXT(""))));

	return Player;
}

UEdGraphNode* FNodeScribeBuildContext::TryCreateAnimGetter(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = false;

	// De quem esta regra e' a regra. O grafo mora dentro da transicao, e a
	// transicao sabe de que estado ela sai -- que e' a resposta que o getter
	// precisa e que o texto nao tem como dizer.
	UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Graph->GetOuter());
	if (!Transition)
	{
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	// A classe nativa mais proxima: os getters sao declarados em C++, e um
	// AnimBlueprint que herda de outro AnimBlueprint nao os redeclara.
	UClass* NativeClass = AnimBlueprint->ParentClass;
	while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native))
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	if (!NativeClass)
	{
		return nullptr;
	}

	const FString Wanted = FNodeScribeCatalog::Normalize(Statement.NodeExpression);

	UFunction* Getter = nullptr;
	TArray<FString> Available;

	for (TFieldIterator<UFunction> It(NativeClass); It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasMetaData(TEXT("AnimGetter")) || !Function->HasAnyFunctionFlags(FUNC_Native))
		{
			continue;
		}

		// `GetterContext` diz onde o getter vale: `Transition`, `CustomBlend`.
		// Sem contexto, vale em qualquer um. Um getter de contexto errado entra
		// e nao funciona, e a Engine so' reclama muito depois.
		const FString Context = Function->HasMetaData(TEXT("GetterContext"))
			? Function->GetMetaData(TEXT("GetterContext")) : FString();
		if (!Context.IsEmpty() && !Context.Contains(TEXT("Transition")))
		{
			continue;
		}

		const FString DisplayName = Function->HasMetaData(TEXT("DisplayName"))
			? Function->GetMetaData(TEXT("DisplayName"))
			: FName::NameToDisplayString(Function->GetName(), false);

		Available.Add(DisplayName);

		if (FNodeScribeCatalog::Normalize(Function->GetName()) == Wanted
			|| FNodeScribeCatalog::Normalize(DisplayName) == Wanted)
		{
			Getter = Function;
			break;
		}
	}

	if (!Getter)
	{
		return nullptr;
	}

	bOutHandled = true;

	UAnimStateNodeBase* PreviousState = Transition->GetPreviousState();
	if (!PreviousState)
	{
		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` precisa saber de que estado a transicao sai, e esta nao esta' ligada a um."),
			*Statement.NodeExpression));

		return CreateErrorComment(Statement,
			TEXT("A transicao nao tem estado de origem, e este getter le' justamente o estado de origem."));
	}

	// A maquina dona do estado. E' o par obrigatorio do estado: sem ela o node
	// compila com "contains invalid data. Please delete and recreate the node."
	UAnimGraphNode_StateMachineBase* MachineNode = nullptr;
	if (UAnimationStateMachineGraph* MachineGraph = Cast<UAnimationStateMachineGraph>(PreviousState->GetOuter()))
	{
		MachineNode = Cast<UAnimGraphNode_StateMachineBase>(MachineGraph->GetOuter());
	}

	UK2Node_AnimGetter* Node = AllocateNode<UK2Node_AnimGetter>();
	Node->SetFromFunction(Getter);
	Node->SourceStateNode = PreviousState;
	Node->SourceNode = MachineNode;
	Node->GetterClass = NativeClass;
	Node->SourceAnimBlueprint = AnimBlueprint;

	// O titulo e' guardado, nao calculado: `GetNodeTitle` devolve `CachedTitle` e
	// mais nada, entao um node sem isto aparece sem nome nenhum no grafo.
	const FString DisplayName = Getter->HasMetaData(TEXT("DisplayName"))
		? Getter->GetMetaData(TEXT("DisplayName"))
		: FName::NameToDisplayString(Getter->GetName(), false);

	Node->CachedTitle = FText::FromString(FString::Printf(
		TEXT("%s (%s)"), *DisplayName, *PreviousState->GetStateName()));

	Node->Contexts.Add(TEXT("Transition"));

	FinalizeNode(Node);

	AddInfo(Statement.LineNumber, FString::Printf(
		TEXT("`%s` le' o estado `%s`, que e' de onde esta transicao sai."),
		*Statement.NodeExpression, *PreviousState->GetStateName()));

	return Node;
}

UEdGraphNode* FNodeScribeBuildContext::TryCreateSpecialNode(const FNodeScribeStatement& Statement, bool& bOutHandled)
{
	bOutHandled = true;

	// --- `$Alguma Coisa` sozinho numa linha -------------------------------
	//
	// A linha inteira e' um valor. Aparece onde o bloco *e'* uma expressao: a
	// regra de uma transicao, cujo resultado e' o ultimo node do bloco.
	//
	// `Get Is In Air` ja' funcionava ali, e `$Is In Air` -- a forma que se
	// escreve em todo argumento -- respondia "nao achei nenhum node chamado
	// `$Is In Air`". Sao a mesma coisa escrita de dois jeitos, e aceitar so' uma
	// delas obriga quem escreve a descobrir qual, num lugar em que o erro nao
	// diz que a diferenca era essa.
	if (Statement.NodeExpression.StartsWith(TEXT("$")))
	{
		const FString Reference = Statement.NodeExpression.RightChop(1).TrimStartAndEnd();

		// Sem pino consumidor: aqui nao ha' quem receba o valor, entao uma
		// variavel que nao existe continua sendo erro -- e' o mesmo criterio de
		// um `Get X` solto, e pelo mesmo motivo (nao ha' de onde tirar o tipo).
		if (UEdGraphPin* Pin = ResolveReference(Reference, Statement.LineNumber, nullptr))
		{
			return Pin->GetOwningNodeUnchecked();
		}

		// ResolveReference ja' disse o que houve.
		return nullptr;
	}

	// Numa regra de transicao, os getters de maquina de estado vem antes do
	// catalogo: as funcoes de mesmo nome existem e sao para outro lugar.
	if (bTransitionGraph)
	{
		bool bGetterHandled = false;
		if (UEdGraphNode* Getter = TryCreateAnimGetter(Statement, bGetterHandled))
		{
			return Getter;
		}
		if (bGetterHandled)
		{
			return nullptr;
		}
	}

	// Num grafo de animacao o vocabulario de anim vem primeiro: `Blend` la' e'
	// um node de pose, nao a funcao de mesmo nome da biblioteca de matematica.
	if (bPoseGraph)
	{
		bool bAnimHandled = false;
		if (UEdGraphNode* AnimNode = TryCreateAnimNode(Statement, bAnimHandled))
		{
			return AnimNode;
		}
		if (bAnimHandled)
		{
			return nullptr;
		}
	}

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

	// --- To Text ----------------------------------------------------------
	//
	// Node proprio, e nao a funcao de mesmo nome: a funcao e'
	// `BlueprintInternalUseOnly` e nao esta' no catalogo. Sem este caso a linha
	// que o leitor escreve nao teria como voltar.
	if (Normalized == TEXT("totext") || Normalized == TEXT("paratexto"))
	{
		UK2Node_GenericToText* Node = AllocateNode<UK2Node_GenericToText>();
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
			UClass* TargetClass = FindClassByFriendlyNameInternal(ClassName);

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
						FindDelegateByFriendlyName(ComponentProperty->PropertyClass, DelegateName);

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

			// A pre-passagem ja' criou este evento para que linhas acima
			// pudessem chama-lo. Aqui so' entregamos o mesmo node.
			if (UEdGraphNode** PreCreated = PreCreatedEvents.Find(EventName))
			{
				return *PreCreated;
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

			// Sem isto, uma linha adiante que CHAME este evento nao o encontra:
			// a classe esqueleto so' ganha a funcao quando e' regerada, e
			// ninguem compila no meio de uma colagem. Custa pouco -- eventos
			// sao poucos por texto -- e e' o que torna `Pular` chamavel na
			// mesma colagem que o declarou.
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::GenerateBlueprintSkeleton(Blueprint, true);

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

			FMulticastDelegateProperty* DelegateProperty =
				FindDelegateByFriendlyName(OwnerClass, DelegateName);

			if (!DelegateProperty)
			{
				// Nao e' dispatcher: pode ser uma funcao que so' comeca com
				// "Clear" ou "Call". Deixa o catalogo tentar.
				break;
			}

			UK2Node_BaseMCDelegate* Node = static_cast<UK2Node_BaseMCDelegate*>(
				NewObject<UEdGraphNode>(Graph, Form.MakeClass(), NAME_None, RF_Transactional));

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

		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
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
		UK2Node_ConstructObjectFromClass* Node = NewObject<UK2Node_ConstructObjectFromClass>(Graph, NodeClass, NAME_None, RF_Transactional);
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
			: FindClassByFriendlyNameInternal(ClassValue);

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
			// Algumas structs trazem a propria funcao de quebrar ou montar, e a
			// Engine avisa na compilacao quando o node generico e' usado numa
			// delas: "The structure cannot be broken using generic 'break' node.
			// Try use specialized 'break' function if available."
			//
			// O aviso e' de graca para quem escreve o texto -- nem da' para
			// escolher o node especializado pelo formato --, entao a escolha e'
			// aqui. `Vector`, `Rotator` e `Transform`, que sao os mais escritos,
			// estao todos nesse caso.
			const TCHAR* const MetaKey = bIsBreak ? TEXT("HasNativeBreak") : TEXT("HasNativeMake");
			if (Struct->HasMetaData(MetaKey))
			{
				const FString FunctionPath = Struct->GetMetaData(MetaKey);
				if (UFunction* Native = FindObject<UFunction>(nullptr, *FunctionPath))
				{
					UK2Node_CallFunction* Node = AllocateNode<UK2Node_CallFunction>();
					Node->SetFromFunction(Native);
					FinalizeNode(Node);
					return Node;
				}
			}

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
			if (UClass* SubsystemClass = FindClassByFriendlyNameInternal(VariableName))
			{
				if (SubsystemClass->IsChildOf(USubsystem::StaticClass()))
				{
					UClass* NodeClass = ChooseSubsystemNodeClass(SubsystemClass);

					UK2Node_GetSubsystem* Node = NewObject<UK2Node_GetSubsystem>(Graph, NodeClass, NAME_None, RF_Transactional);
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
		// Acao assincrona nao e' chamada de funcao: e' um node proprio, com uma
		// saida de execucao para cada delegate do proxy (`On Connected`,
		// `On Disconnected`). Essas saidas ja' sao pinos de execucao comuns,
		// entao os rotulos indentados do formato servem sem nada novo.
		if (FNodeScribeCatalog::IsAsyncActionFactory(Lookup.Function))
		{
			UK2Node_AsyncAction* Node = AllocateNode<UK2Node_AsyncAction>();

			// Antes de alocar os pinos: sao os delegates do proxy que dizem quais
			// pinos existem, e o proxy so' e' conhecido depois desta linha.
			Node->InitializeProxyFromFunction(Lookup.Function);
			FinalizeNode(Node);
			return Node;
		}

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

		// Ambiguo tambem e' falta de vocabulario: o nome curto existe e nao chega
		// para decidir. Ou falta um apelido, ou falta o formato saber desempatar.
		AnotaNomeNaoResolvido(TEXT("ambiguo"), Statement.NodeExpression);

		AddError(Statement.LineNumber, FString::Printf(
			TEXT("`%s` e' ambiguo. Candidatos: %s"),
			*Statement.NodeExpression, *FString::Join(Lookup.Candidates, TEXT(" | "))));

		return CreateErrorComment(Statement, Reason);
	}

	// A linha comeca com verbo de dispatcher e mesmo assim nao virou node: em vez
	// de repetir "nao achei", diga quais dispatchers a classe realmente tem. E'
	// a diferenca entre "errei o nome" e "esse recurso nao existe".
	FString DelegateHint;
	{
		static const TCHAR* const DelegateVerbs[] = {
			TEXT("Chamar "), TEXT("Call "), TEXT("Vincular "), TEXT("Bind "),
			TEXT("Desvincular "), TEXT("Unbind "), TEXT("Limpar "), TEXT("Clear ")
		};

		const FString Expression = Statement.NodeExpression.TrimStartAndEnd();

		for (const TCHAR* Verb : DelegateVerbs)
		{
			if (!Expression.StartsWith(Verb, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TArray<FString> Available;
			if (UClass* SelfClass = GetSelfClass())
			{
				for (TFieldIterator<FMulticastDelegateProperty> It(SelfClass); It; ++It)
				{
					Available.Add(It->GetName());
				}
			}

			DelegateHint = Available.Num() > 0
				? FString::Printf(TEXT("\n\nSe era um dispatcher: os deste Blueprint sao %s"),
					*FString::Join(Available, TEXT(", ")))
				: TEXT("\n\nSe era um dispatcher: este Blueprint nao tem nenhum.");

			break;
		}
	}

	AnotaNomeNaoResolvido(TEXT("node"), Statement.NodeExpression);

	AddError(Statement.LineNumber, FString::Printf(
		TEXT("Nao achei nenhum node chamado `%s`.%s"),
		*Statement.NodeExpression, *DelegateHint.Replace(TEXT("\n\n"), TEXT(" "))));

	return CreateErrorComment(Statement,
		TEXT("Nenhum node com esse nome foi encontrado.") + DelegateHint);
}

namespace
{
	/**
	 * `estado Parado` -> "Parado". Sem o prefixo, nao e' declaracao de estado.
	 *
	 * O prefixo e' obrigatorio de proposito: dentro de uma maquina de estados
	 * um rotulo solto seria indistinguivel de uma entrada de pose, e escolher
	 * pelo formato do nome seria adivinhar.
	 */
	bool ParseStateLabel(const FString& Label, FString& OutName)
	{
		for (const TCHAR* Prefix : { TEXT("estado "), TEXT("state ") })
		{
			if (Label.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				OutName = Label.Mid(FCString::Strlen(Prefix)).TrimStartAndEnd();
				return !OutName.IsEmpty();
			}
		}
		return false;
	}

	/** `Parado -> Correndo`, com ou sem `transicao` na frente. */
	bool ParseTransitionLabel(const FString& Label, FString& OutFrom, FString& OutTo)
	{
		FString Rest = Label;
		for (const TCHAR* Prefix : { TEXT("transicao "), TEXT("transi\u00e7\u00e3o "), TEXT("transition ") })
		{
			if (Rest.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				Rest = Rest.Mid(FCString::Strlen(Prefix));
				break;
			}
		}

		if (!Rest.Split(TEXT("->"), &OutFrom, &OutTo))
		{
			return false;
		}

		OutFrom.TrimStartAndEndInline();
		OutTo.TrimStartAndEndInline();
		return !OutFrom.IsEmpty() && !OutTo.IsEmpty();
	}

	/** Onde os estados ficam no sub-grafo: uma fila, com folga para os fios. */
	constexpr int32 StateColumnWidth = 320;
}

UEdGraphNode* FNodeScribeBuildContext::BuildSubGraph(UEdGraph* SubGraph, const TArray<FNodeScribeStatement>& Statements, int32 First, int32 End)
{
	if (!SubGraph || First >= End)
	{
		return nullptr;
	}

	TArray<FNodeScribeStatement> Body;
	Body.Reserve(End - First);
	for (int32 Index = First; Index < End; ++Index)
	{
		Body.Add(Statements[Index]);
	}

	// Contexto proprio: o sub-grafo tem outro schema, outro Output Pose e outra
	// arvore de layout. Compartilhar o nosso faria os dois grafos disputarem o
	// mesmo estado -- e as `$referencias` de um vazariam para o outro, onde os
	// pinos nem existem.
	FNodeScribeBuildContext Nested(SubGraph, Blueprint, FVector2D::ZeroVector);
	Nested.Run(Body);

	Result.Diagnostics.Append(Nested.Result.Diagnostics);
	Result.ErrorCount += Nested.Result.ErrorCount;
	Result.WarningCount += Nested.Result.WarningCount;
	NestedNodes.Append(Nested.Result.CreatedNodes);

	// Frames[0] e' o nivel raiz do bloco, e o LastNode dele e' o node da ultima
	// linha -- inclusive quando essa linha e' um node puro, que nao entra na
	// cadeia de fluxo e por isso nao aparece no PendingExec.
	return Nested.Frames.Num() > 0 ? Nested.Frames[0].LastNode : nullptr;
}

int32 FNodeScribeBuildContext::BuildStateMachine(UAnimGraphNode_StateMachineBase* Machine, const TArray<FNodeScribeStatement>& Statements, int32 First)
{
	const int32 BlockIndent = Statements[First].Indent;

	int32 End = First;
	while (End < Statements.Num() && Statements[End].Indent >= BlockIndent)
	{
		++End;
	}

	UAnimationStateMachineGraph* MachineGraph = Machine->EditorStateMachineGraph;
	if (!MachineGraph)
	{
		AddError(Statements[First].LineNumber,
			TEXT("A maquina de estados entrou sem sub-grafo. Nao da' para criar estado dentro dela."));
		return End;
	}

	// --- 1a passagem: os estados ------------------------------------------
	//
	// Todos antes de qualquer transicao. Assim `Parado -> Correndo` pode vir
	// escrito antes de `estado Correndo`, e um nome errado numa transicao vira
	// erro em vez de um estado vazio criado por engano.
	TMap<FString, UAnimStateNode*> States;
	TArray<FString> DeclaredOrder;

	for (int32 Index = First; Index < End; ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];
		if (!Statement.bIsLabel || Statement.Indent != BlockIndent)
		{
			continue;
		}

		FString Name;
		if (!ParseStateLabel(Statement.Label, Name))
		{
			continue;
		}

		const FString Key = FNodeScribeCatalog::Normalize(Name);
		if (States.Contains(Key))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("O estado `%s` ja' foi declarado nesta maquina."), *Name));
			continue;
		}

		UAnimStateNode* State = AllocateNode<UAnimStateNode>(MachineGraph);
		FinalizeNode(State);

		if (State->BoundGraph)
		{
			FBlueprintEditorUtils::RenameGraph(State->BoundGraph, Name);
		}

		State->NodePosX = States.Num() * StateColumnWidth;
		State->NodePosY = 0;

		States.Add(Key, State);
		DeclaredOrder.Add(Key);
		NestedNodes.Add(State);
	}

	if (States.Num() == 0)
	{
		AddWarning(Statements[First].LineNumber,
			TEXT("Esta maquina de estados nao declarou nenhum estado. Use `estado Nome:`."));
		return End;
	}

	// O primeiro declarado e' onde a maquina comeca. E' a unica leitura possivel
	// sem inventar sintaxe: no grafo o Entry aponta para um estado so'.
	if (MachineGraph->EntryNode)
	{
		if (UEdGraphPin* EntryPin = MachineGraph->EntryNode->GetOutputPin())
		{
			UAnimStateNode* FirstState = States[DeclaredOrder[0]];
			if (UEdGraphPin* StatePin = FirstState->GetInputPin())
			{
				EntryPin->BreakAllPinLinks();
				EntryPin->MakeLinkTo(StatePin);
			}
		}
	}

	// --- 2a passagem: o conteudo de cada estado e cada transicao ----------
	for (int32 Index = First; Index < End; ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];
		if (!Statement.bIsLabel || Statement.Indent != BlockIndent)
		{
			continue;
		}

		int32 BodyEnd = Index + 1;
		while (BodyEnd < End && Statements[BodyEnd].Indent > BlockIndent)
		{
			++BodyEnd;
		}

		FString Name;
		if (ParseStateLabel(Statement.Label, Name))
		{
			UAnimStateNode* State = States.FindRef(FNodeScribeCatalog::Normalize(Name));
			if (State && State->BoundGraph)
			{
				BuildSubGraph(State->BoundGraph, Statements, Index + 1, BodyEnd);
			}
			continue;
		}

		FString From, To;
		if (!ParseTransitionLabel(Statement.Label, From, To))
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("`%s:` nao e' estado nem transicao. Dentro de uma maquina de estados so' ha' ")
				TEXT("`estado Nome:` e `Origem -> Destino:`."), *Statement.Label));
			continue;
		}

		UAnimStateNode* FromState = States.FindRef(FNodeScribeCatalog::Normalize(From));
		UAnimStateNode* ToState = States.FindRef(FNodeScribeCatalog::Normalize(To));

		if (!FromState || !ToState)
		{
			TArray<FString> Known;
			for (const FString& Key : DeclaredOrder)
			{
				Known.Add(States[Key]->GetStateName());
			}

			AddError(Statement.LineNumber, FString::Printf(
				TEXT("A transicao `%s` fala de um estado que nao existe (`%s`). Estados desta maquina: %s"),
				*Statement.Label, *(FromState ? To : From), *FString::Join(Known, TEXT(", "))));
			continue;
		}

		if (FromState == ToState)
		{
			AddError(Statement.LineNumber, FString::Printf(
				TEXT("A transicao `%s` sai e chega no mesmo estado."), *Statement.Label));
			continue;
		}

		UAnimStateTransitionNode* Transition = AllocateNode<UAnimStateTransitionNode>(MachineGraph);
		FinalizeNode(Transition);
		Transition->CreateConnections(FromState, ToState);

		// Em cima do fio, que e' onde o editor a desenha.
		Transition->NodePosX = (FromState->NodePosX + ToState->NodePosX) / 2;
		Transition->NodePosY = FromState->NodePosY - 120;

		NestedNodes.Add(Transition);

		if (!Transition->BoundGraph)
		{
			continue;
		}

		UEdGraphNode* RuleResult = BuildSubGraph(Transition->BoundGraph, Statements, Index + 1, BodyEnd);

		// A regra e' um grafo de dado que termina num bool. Ligar o resultado e'
		// trabalho do plugin, como qualquer outra ligacao que o texto nao
		// escreve -- e sem isso a transicao nunca dispara.
		UAnimGraphNode_TransitionResult* ResultNode = nullptr;
		for (UEdGraphNode* Node : Transition->BoundGraph->Nodes)
		{
			if (UAnimGraphNode_TransitionResult* Found = Cast<UAnimGraphNode_TransitionResult>(Node))
			{
				ResultNode = Found;
				break;
			}
		}

		// Estes dois calavam. Uma transicao sem regra compila, roda, e nunca
		// dispara -- o pino vazio parece um `false` deliberado, e nada na tela
		// distingue "o texto nao pediu regra" de "o plugin nao conseguiu ligar".
		if (!ResultNode)
		{
			AddWarning(Statement.LineNumber, FString::Printf(
				TEXT("A transicao `%s` nasceu sem node de resultado. A regra nao foi ligada."),
				*Statement.Label));
			continue;
		}

		UEdGraphPin* CanEnter = ResultNode->FindPin(TEXT("bCanEnterTransition"));
		if (!CanEnter)
		{
			TArray<FString> Pinos;
			for (const UEdGraphPin* Pin : ResultNode->Pins)
			{
				Pinos.Add(Pin->PinName.ToString());
			}

			AddWarning(Statement.LineNumber, FString::Printf(
				TEXT("O resultado de `%s` nao tem pino `bCanEnterTransition`. Tem: %s"),
				*Statement.Label,
				Pinos.Num() > 0 ? *FString::Join(Pinos, TEXT(", ")) : TEXT("nenhum")));
			continue;
		}

		if (CanEnter->LinkedTo.Num() > 0 || BodyEnd == Index + 1)
		{
			continue;
		}

		// O ultimo node do bloco e' o resultado da regra, do mesmo jeito que o
		// ultimo node de um galho de pose e' o resultado do galho.
		//
		// Quem diz qual e' esse node e' o percurso do bloco, nao a ordem em que
		// os nodes cairam no grafo: o argumento de um node nasce *depois* dele,
		// entao varrer `BoundGraph->Nodes` de tras para frente pega o
		// `$Ground Speed` da regra em vez da comparacao que o consome -- e ai a
		// transicao fica sem regra, com um aviso de tipo trocado no lugar.
		UEdGraphPin* Output = RuleResult ? FindPrimaryOutput(RuleResult) : nullptr;
		if (!Output)
		{
			// Transicao sem regra nunca dispara, e nada na tela diz isso: o pino
			// vazio parece um `false` deliberado.
			AddWarning(Statement.LineNumber, FString::Printf(
				TEXT("A regra de `%s` nao terminou num valor. A transicao nunca dispara."),
				*Statement.Label));
			continue;
		}

		// Os dois pinos vivem no grafo da regra, nao neste. Ligar pelo schema
		// daqui produz um fio que o grafo de la' nao reconhece: ele aparece, e
		// some no primeiro refresh do Blueprint -- que e' logo ali, no
		// MarkBlueprintAsStructurallyModified. Quem valida a ligacao tem que ser
		// o schema do grafo onde os pinos moram.
		const UEdGraphSchema* RuleSchema = Transition->BoundGraph->GetSchema();
		if (RuleSchema)
		{
			RuleSchema->TryCreateConnection(Output, CanEnter);
		}

		// Conferir em vez de confiar: sem regra a transicao nunca dispara, e o
		// pino vazio nao se distingue de um `false` deliberado.
		if (CanEnter->LinkedTo.Num() == 0)
		{
			AddWarning(Statement.LineNumber, FString::Printf(
				TEXT("A regra de `%s` (`%s`) nao entrou no Can Enter Transition. A transicao nunca dispara."),
				*Statement.Label,
				*RuleResult->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		}
	}

	return End;
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

	PreCreateCustomEvents(Statements);

	// Por indice, e nao por range-for, porque a maquina de estados nao le' o
	// bloco dela linha a linha: pega o bloco inteiro e devolve onde parou.
	for (int32 Index = 0; Index < Statements.Num(); ++Index)
	{
		const FNodeScribeStatement& Statement = Statements[Index];

		// Declaracao nao cria node nem participa da cadeia: e' so' uma variavel
		// passando a existir antes das linhas que a usam.
		if (Statement.bIsVariable)
		{
			CreateDeclaredVariable(Statement);
			continue;
		}

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

			// Debaixo de uma maquina de estados o rotulo nao nomeia pino nenhum:
			// nomeia um estado ou uma transicao, e o corpo dele vive noutro
			// grafo. O bloco sai inteiro do percurso aqui.
			if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Owner))
			{
				Index = BuildStateMachine(Machine, Statements, Index) - 1;
				continue;
			}

			// No AnimGraph o rotulo nomeia uma *entrada* de pose: `True Pose:` de
			// um blend abre o galho que alimenta aquele pino. A arvore de um
			// AnimGraph e' de quem entra, nao de quem continua.
			const bool bLabelsAreInputs = bAnimGraph && GetExecOutputs(Owner).Num() == 0;

			const TArray<UEdGraphPin*> ExecOutputs = bLabelsAreInputs
				? NodeScribeAnimGraph::GetPoseInputs(Owner)
				: GetExecOutputs(Owner);

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
					TEXT("`%s:` nao e' %s deste node. %s: %s"),
					*Statement.Label,
					bLabelsAreInputs ? TEXT("uma entrada de pose") : TEXT("uma saida"),
					bLabelsAreInputs ? TEXT("Entradas") : TEXT("Saidas"),
					*FString::Join(Available, TEXT(", "))));
				continue;
			}

			// Este node ganhou rotulo: sai da lista de "parou sem escolher ramo".
			UnbranchedNodes.RemoveAll([Owner](const FUnbranchedNode& Entry)
			{
				return Entry.Node == Owner;
			});

			FFrame Branch;
			Branch.Indent = Statement.Indent;

			if (bLabelsAreInputs)
			{
				// O bloco comeca sem nada antes dele: o primeiro node do galho e'
				// uma ponta da arvore, nao a continuacao do blend.
				Branch.FlowSink = FPinRef(Chosen);
			}
			else
			{
				Branch.PendingExec = FPinRef(Chosen);
			}

			// O galho de pose desenha a' esquerda: ali o fluxo anda para o
			// Output Pose, e o que alimenta um node fica antes dele.
			Branch.BaseX = bLabelsAreInputs
				? Owner->NodePosX - ColumnWidth
				: Owner->NodePosX + ColumnWidth;
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

		// Node adotado ja' estava no grafo -- o Output Pose e' o caso. Mover e'
		// mexer no que o usuario arrumou, e contar seria dizer que criamos algo
		// que sempre esteve la'.
		const bool bAdopted = AdoptedNodes.Contains(Node);

		// Nodes de dado nao ocupam coluna: eles descem em pilha embaixo de quem
		// os consome, e quem cuida disso e' LayoutDataNodes(), no fim.
		if (!bAdopted && !IsPureDataNode(Node))
		{
			Node->NodePosX = Frame.BaseX + (Frame.Column * ColumnWidth);
			Node->NodePosY = Frame.BaseY;
			++Frame.Column;
		}

		if (!bAdopted)
		{
			Result.CreatedNodes.Add(Node);
		}

		// Um comentario ocupa o lugar de uma linha que nao resolveu; aplicar os
		// argumentos dela geraria uma segunda leva de erros sobre o mesmo problema.
		if (!Node->IsA<UEdGraphNode_Comment>())
		{
			ApplyArguments(Node, Statement);
		}

		if (Statement.OutputName.IsEmpty() && !Node->IsA<UEdGraphNode_Comment>())
		{
			UnnamedLines.Add({ Statement.LineNumber, Statement.NodeExpression });
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

		UEdGraphPin* ExecIn = FindFlowInput(Node);
		const TArray<UEdGraphPin*> ExecOutputs = GetFlowOutputs(Node);

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

		// O bloco alimenta uma entrada de pose la' em cima. Cada node liga por
		// cima do anterior; o pino so' aceita um fio, entao sobra o ultimo --
		// que e' o resultado do bloco.
		if (ExecOutputs.Num() == 1)
		{
			if (UEdGraphPin* Sink = Frame.FlowSink.Resolve())
			{
				// Quebrar antes de ligar, em vez de contar que o pino recuse o
				// segundo fio: se a saida de pose aceitar varios destinos, o node
				// anterior ficaria alimentando o blend *e* o node seguinte.
				Sink->BreakAllPinLinks();
				Connect(ExecOutputs[0], Sink, Statement.LineNumber);
			}
		}

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

	// Get que o plugin criou por conta propria e que nao ligou em nada nao
	// representa nenhuma linha do texto: e' resto de uma ligacao que falhou.
	// O aviso da falha ja' saiu; deixar o node so' encheria o grafo.
	for (UEdGraphNode* GetNode : AutoCreatedGets)
	{
		bool bConnected = false;
		for (const UEdGraphPin* Pin : GetNode->Pins)
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				bConnected = true;
				break;
			}
		}

		if (!bConnected)
		{
			Result.CreatedNodes.Remove(GetNode);
			Graph->RemoveNode(GetNode);
		}
	}

	ConnectOutputPose();

	LayoutAnimNodes();
	LayoutDataNodes();
	ReportEmptyPoseInputs();

	// Depois do layout: eles vivem noutro grafo e nada aqui tem o que posicionar
	// neles. Entram so' para a contagem e para o Ctrl+Z pegar tudo.
	Result.CreatedNodes.Append(NestedNodes);
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

bool NodeScribeTypeNames::ResolvePinTypeFromName(const FString& InTypeName, FEdGraphPinType& OutType)
{
	return ResolvePinTypeFromNameInternal(InTypeName, OutType);
}

UClass* NodeScribeTypeNames::FindClassByFriendlyName(const FString& Name)
{
	return FindClassByFriendlyNameInternal(Name);
}
