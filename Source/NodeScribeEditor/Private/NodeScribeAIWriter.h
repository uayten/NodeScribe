#pragma once

#include "CoreMinimal.h"

class UBlackboardData;
class UObject;

/**
 * O espelho do FNodeScribeAIReader. Hoje so' blackboard.
 *
 * Behavior Tree ainda nao: o asset guarda a hierarquia de execucao *e* um grafo
 * de editor que precisa ficar em sincronia, e escrever so' o lado de runtime
 * da' um asset que roda e aparece vazio na tela.
 */
class FNodeScribeAIWriter
{
public:
	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	/** true quando WriteAsset sabe escrever neste objeto. */
	static bool Handles(const UObject* Object);

	static FResult WriteAsset(UObject* Object, const FString& Text);

private:
	static FResult WriteBlackboard(UBlackboardData* Blackboard, const FString& Text);
};
