#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
struct FEdGraphPinType;

/**
 * Nome de tipo escrito a mao -> tipo de pino da Unreal.
 *
 * Aceita o que aparece na interface (`Float`, `Timer Handle`,
 * `EPlayerMappableKeySlot`, `Array de Name`), porque e' o que o usuario ve'.
 *
 * E' o espelho de `NodeScribePropertyText::DescribePinType`: o que sai de la'
 * entra aqui. Mora no builder porque depende dos buscadores de struct, enum e
 * classe por nome de tela, que sao dele -- mas a ficha tambem precisa, para
 * criar variavel a partir de `variavel Nome : Tipo`.
 */
namespace NodeScribeTypeNames
{
	bool ResolvePinTypeFromName(const FString& InTypeName, FEdGraphPinType& OutType);
}

/**
 * Transforma statements em nodes reais dentro de um UEdGraph.
 *
 * Regra que orienta o arquivo inteiro: quando nao da' para decidir com
 * seguranca, nao decide. Node nao resolvido vira comentario vermelho no grafo;
 * pino que depende de uma escolha sua fica vazio e impede compilar. O modo de
 * falha aceitavel e' o barulhento -- nunca um node plausivel chutado.
 */
class FNodeScribeBuilder
{
public:
	struct FResult
	{
		TArray<FNodeScribeDiagnostic> Diagnostics;
		TArray<UEdGraphNode*> CreatedNodes;

		int32 ErrorCount = 0;
		int32 WarningCount = 0;

		bool HasErrors() const { return ErrorCount > 0; }
	};

	/**
	 * @param Statements  saida do parser.
	 * @param Graph       grafo de destino (pode ser um grafo temporario para exportar).
	 * @param Blueprint   Blueprint dono do grafo, usado para achar variaveis e funcoes proprias.
	 * @param Origin      canto superior esquerdo onde comecar a desenhar.
	 */
	static FResult Build(
		const TArray<FNodeScribeStatement>& Statements,
		UEdGraph* Graph,
		UBlueprint* Blueprint,
		const FVector2D& Origin);
};
