#pragma once

#include "CoreMinimal.h"

class UBlackboardData;

/**
 * Os assets de IA como texto: blackboard hoje, Behavior Tree em seguida.
 *
 * Sao um caso a parte da ficha porque a informacao deles nao esta nas
 * propriedades do objeto de cima, e sim numa estrutura pendurada nele. A ficha
 * generica de um BlackboardData ve' uma propriedade `Keys` e nada mais; a de
 * um BehaviorTree ve' um ponteiro para a raiz. Quem quiser saber o que a IA
 * faz teria que seguir ponteiro objeto a objeto, uma chamada por node.
 */
class FNodeScribeAIReader
{
public:
	/** true quando ReadAsset sabe ler este objeto de um jeito melhor que a ficha. */
	static bool Handles(const UObject* Object);

	/** O asset de IA como texto. Vazio quando nao e' um deles. */
	static FString ReadAsset(UObject* Object);

private:
	static FString ReadBlackboard(UBlackboardData* Blackboard);
};
