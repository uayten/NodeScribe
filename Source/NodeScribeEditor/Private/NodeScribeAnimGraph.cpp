#include "NodeScribeAnimGraph.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeTypes.h"

#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateResult.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UObjectIterator.h"

namespace NodeScribeAnimGraph
{

// ---------------------------------------------------------------------------
// Grafo
// ---------------------------------------------------------------------------

bool IsAnimationGraph(const UEdGraph* Graph)
{
	if (!Graph || !Graph->GetSchema())
	{
		return false;
	}

	// O schema cobre AnimGraph e interior de estado de uma vez -- os dois
	// aceitam node de anim. Testar a classe do grafo deixaria de fora o interior
	// de estado, que e' exatamente onde a maquina de estados precisa escrever.
	//
	// **Nao cobre a regra de transicao.** UAnimationTransitionSchema desce de
	// UEdGraphSchema_K2, nao daqui, porque a regra nao tem pose: e' uma cadeia
	// de dado terminando num bool. Quem precisa dela testa
	// `IsA<UAnimationTransitionGraph>()`.
	return Graph->GetSchema()->IsA<UAnimationGraphSchema>();
}

bool IsStateMachineGraph(const UEdGraph* Graph)
{
	return Graph && Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationStateMachineSchema>();
}

// ---------------------------------------------------------------------------
// Pinos de pose
// ---------------------------------------------------------------------------

bool IsPosePin(const UEdGraphPin* Pin)
{
	return Pin && UAnimationGraphSchema::IsPosePin(Pin->PinType);
}

UEdGraphPin* FindPoseInput(UEdGraphNode* Node)
{
	if (!Node)
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && IsPosePin(Pin))
		{
			return Pin;
		}
	}
	return nullptr;
}

TArray<UEdGraphPin*> GetPoseInputs(UEdGraphNode* Node)
{
	TArray<UEdGraphPin*> Inputs;
	if (!Node)
	{
		return Inputs;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && IsPosePin(Pin))
		{
			Inputs.Add(Pin);
		}
	}
	return Inputs;
}

TArray<UEdGraphPin*> GetPoseOutputs(UEdGraphNode* Node)
{
	TArray<UEdGraphPin*> Outputs;
	if (!Node)
	{
		return Outputs;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && IsPosePin(Pin))
		{
			Outputs.Add(Pin);
		}
	}
	return Outputs;
}

UEdGraphNode* FindOutputPose(UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}

	// AnimGraph termina em Root; o interior de um estado termina em StateResult.
	// Sao dois nodes diferentes com o mesmo papel, e nenhum dos dois se cria:
	// ambos nascem junto com o grafo.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && (Node->IsA<UAnimGraphNode_Root>() || Node->IsA<UAnimGraphNode_StateResult>()))
		{
			return Node;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Indice de classes de node
// ---------------------------------------------------------------------------

namespace
{

struct FEntry
{
	TWeakObjectPtr<UClass> NodeClass;

	/** Nome da classe sem o prefixo: `UAnimGraphNode_BlendSpacePlayer` -> "blendspaceplayer". */
	FString NormalizedClassName;

	/** Titulo do menu normalizado: "blendposesbybool". */
	FString NormalizedTitle;

	/** Nome legivel para listar num erro de ambiguidade. */
	FString Display;
};

TArray<FEntry> GEntries;
bool GBuilt = false;

/** O titulo que o node mostra no menu do grafo, lido do CDO. */
FString TitleForClass(UClass* NodeClass)
{
	const UAnimGraphNode_Base* CDO = Cast<UAnimGraphNode_Base>(NodeClass->GetDefaultObject(false));
	if (!CDO)
	{
		return FString();
	}

	// Alguns nodes montam o titulo a partir do asset que embrulham e devolvem
	// vazio no CDO. Nesses o nome da classe e' o unico nome estavel, e ja' esta'
	// indexado em separado.
	return CDO->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
}

void BuildIndex()
{
	GEntries.Reset();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UAnimGraphNode_Base::StaticClass()))
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (Class == UAnimGraphNode_Base::StaticClass())
		{
			continue;
		}

		FEntry Entry;
		Entry.NodeClass = Class;

		FString ClassName = Class->GetName();
		ClassName.RemoveFromStart(TEXT("AnimGraphNode_"));
		Entry.NormalizedClassName = FNodeScribeCatalog::Normalize(ClassName);

		const FString Title = TitleForClass(Class);
		Entry.NormalizedTitle = FNodeScribeCatalog::Normalize(Title);
		Entry.Display = Title.IsEmpty() ? ClassName : Title;

		GEntries.Add(MoveTemp(Entry));
	}

	GBuilt = true;
}

void EnsureBuilt()
{
	if (!GBuilt)
	{
		BuildIndex();
	}
}

} // namespace

FLookup FindNodeClass(const FString& Query)
{
	EnsureBuilt();

	FLookup Result;
	const FString Normalized = FNodeScribeCatalog::Normalize(Query);
	if (Normalized.IsEmpty())
	{
		return Result;
	}

	TArray<const FEntry*> Exact;
	for (const FEntry& Entry : GEntries)
	{
		if (!Entry.NodeClass.IsValid())
		{
			continue;
		}
		if (Entry.NormalizedClassName == Normalized || Entry.NormalizedTitle == Normalized)
		{
			Exact.Add(&Entry);
		}
	}

	if (Exact.Num() == 1)
	{
		Result.NodeClass = Exact[0]->NodeClass.Get();
		return Result;
	}

	if (Exact.Num() > 1)
	{
		// Mesmo nome em duas classes: listar em vez de escolher, pela mesma
		// razao do catalogo de funcoes -- chutar compila e roda errado.
		for (const FEntry* Entry : Exact)
		{
			Result.Candidates.Add(Entry->Display);
		}
		return Result;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------

namespace
{

/** Nome curto normalizado -> todos os assets que atendem por ele. */
TMultiMap<FString, FSoftObjectPath> GAssetsByName;
bool GAssetsBuilt = false;

void BuildAssetIndex()
{
	GAssetsByName.Reset();

	const FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UAnimationAsset::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(Filter, Assets);

	// Guardamos o caminho, nao o asset: varrer o registry ja' e' caro uma vez,
	// e carregar toda animacao do projeto para montar um indice seria pior que
	// o problema. Carrega-se so' a que a linha pedir.
	for (const FAssetData& Data : Assets)
	{
		GAssetsByName.Add(FNodeScribeCatalog::Normalize(Data.AssetName.ToString()), Data.GetSoftObjectPath());
	}

	GAssetsBuilt = true;
}

} // namespace

FAssetLookup FindAnimationAsset(const FString& Query)
{
	FAssetLookup Result;

	const FString Trimmed = Query.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return Result;
	}

	// Caminho completo: carregar direto, sem consultar o registry.
	if (Trimmed.StartsWith(TEXT("/")))
	{
		Result.Asset = LoadObject<UAnimationAsset>(nullptr, *Trimmed);
		return Result;
	}

	if (!GAssetsBuilt)
	{
		BuildAssetIndex();
	}

	TArray<FSoftObjectPath> Matches;
	GAssetsByName.MultiFind(FNodeScribeCatalog::Normalize(Trimmed), Matches);

	// Zero resultados quer dizer duas coisas diferentes: o asset nao existe, ou
	// existe e e' mais novo que o indice. O indice e' montado uma vez por
	// sessao, entao um BlendSpace recem-criado -- por `create_asset`, ou por
	// alguem clicando no editor -- ficava de fora ate' o editor reabrir, e a
	// resposta era "nao achei nenhum node chamado X" para um asset que estava
	// ali na tela. Refazer o indice custa uma varredura do registry, e so' no
	// caminho que ja' ia dar erro.
	if (Matches.Num() == 0)
	{
		BuildAssetIndex();
		GAssetsByName.MultiFind(FNodeScribeCatalog::Normalize(Trimmed), Matches);
	}

	if (Matches.Num() == 1)
	{
		Result.Asset = Cast<UAnimationAsset>(Matches[0].TryLoad());
		return Result;
	}

	for (const FSoftObjectPath& Path : Matches)
	{
		Result.Candidates.Add(Path.ToString());
	}
	return Result;
}

UClass* NodeClassForAsset(const UAnimationAsset* Asset)
{
	if (!Asset)
	{
		return nullptr;
	}

	// Mesmo mapeamento que arrastar o asset para o grafo usa.
	return GetNodeClassForAsset(Asset->GetClass());
}

void Invalidate()
{
	GEntries.Reset();
	GBuilt = false;
	GAssetsBuilt = false;
}

} // namespace NodeScribeAnimGraph
