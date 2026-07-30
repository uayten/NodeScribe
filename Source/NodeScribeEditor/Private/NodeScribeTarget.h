#pragma once

#include "CoreMinimal.h"

class UEdGraph;

/** Onde os nodes novos entram no grafo. */
struct FNodeScribeTarget
{
	/**
	 * Um ponto livre abaixo do que ja' existe no grafo, para os nodes novos
	 * nao caírem em cima do que voce ja' montou.
	 */
	static FVector2D FindFreeOrigin(const UEdGraph* Graph);
};
