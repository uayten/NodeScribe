#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;
class UEdGraph;

/**
 * Um destino possivel para a transcricao: um grafo aberto no editor.
 *
 * O painel lista todos e deixa voce escolher em vez de adivinhar qual
 * Blueprint esta' "em foco" -- escolher errado escreveria no asset errado.
 */
struct FNodeScribeTarget
{
	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakObjectPtr<UEdGraph> Graph;

	bool IsValid() const { return Blueprint.IsValid() && Graph.IsValid(); }

	/** Ex.: "WBP_Rebind -> EventGraph". */
	FText GetDisplayText() const;

	/** Todos os grafos de Blueprint abertos no editor, na ordem em que aparecem. */
	static TArray<TSharedPtr<FNodeScribeTarget>> FindAllOpen();

	/**
	 * Um ponto livre abaixo do que ja' existe no grafo, para os nodes novos
	 * nao caírem em cima do que voce ja' montou.
	 */
	static FVector2D FindFreeOrigin(const UEdGraph* Graph);
};
