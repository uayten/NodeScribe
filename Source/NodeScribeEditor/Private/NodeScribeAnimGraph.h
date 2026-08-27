#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Suporte a AnimGraph.
 *
 * Um AnimGraph nao e' um grafo de execucao: os nodes nao "continuam" um no
 * outro, eles alimentam uma pose. O fluxo e' invertido em relacao ao
 * EventGraph -- a cadeia termina no Output Pose, que ja' existe no grafo e
 * nunca e' criado.
 *
 * O catalogo de funcoes nao serve aqui: node de anim e' subclasse de
 * `UAnimGraphNode_Base`, nao `UFunction`. Por isso este arquivo mantem o
 * proprio indice, montado do mesmo jeito -- uma vez, por nome normalizado.
 */
namespace NodeScribeAnimGraph
{

/** true quando o grafo e' um AnimGraph, um sub-grafo de estado ou de transicao. */
bool IsAnimationGraph(const UEdGraph* Graph);

/** true quando o grafo e' o interior de uma maquina de estado. */
bool IsStateMachineGraph(const UEdGraph* Graph);

/** true para pino de pose, local ou component space. */
bool IsPosePin(const UEdGraphPin* Pin);

/** O pino de pose de entrada do node, se houver. */
UEdGraphPin* FindPoseInput(UEdGraphNode* Node);

/** Os pinos de pose de saida do node. */
TArray<UEdGraphPin*> GetPoseOutputs(UEdGraphNode* Node);

/**
 * O Output Pose do grafo. Ele nasce com o AnimGraph e e' unico; escrever
 * `Output Pose` numa linha encontra este node em vez de criar outro.
 */
UEdGraphNode* FindOutputPose(UEdGraph* Graph);

/** Resultado de uma busca por nome de node de anim. */
struct FLookup
{
	UClass* NodeClass = nullptr;
	TArray<FString> Candidates;

	bool IsConfident() const { return NodeClass != nullptr; }
	bool IsAmbiguous() const { return NodeClass == nullptr && Candidates.Num() > 0; }
};

/**
 * Procura a classe de node pelo nome escrito pelo usuario.
 * Compara contra o titulo que aparece no menu do grafo -- `Blend Poses by
 * Bool`, `Layered blend per bone` --, que e' o unico nome que o usuario tem
 * como saber.
 */
FLookup FindNodeClass(const FString& Query);

/**
 * Um asset de animacao pelo nome curto ou caminho completo.
 * Devolve nulo em vez de chutar: o formato ja' recusa nome solto de asset.
 */
UAnimationAsset* FindAnimationAsset(const FString& Query);

/**
 * A classe de node que toca um dado asset -- Sequence Player para AnimSequence,
 * BlendSpace Player para BlendSpace, e assim por diante. E' o mesmo mapeamento
 * que o arrastar-e-soltar usa.
 */
UClass* NodeClassForAsset(const UAnimationAsset* Asset);

/** Forca a reconstrucao do indice. */
void Invalidate();

} // namespace NodeScribeAnimGraph
