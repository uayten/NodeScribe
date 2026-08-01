#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * O caminho de volta: nodes de um grafo -> texto no formato do NodeScribe.
 *
 * Existe para fechar o ciclo. Sem ele, mostrar um grafo pronto a alguem (ou a
 * um assistente) custa um print, que nao diz os valores dos pinos, ou o
 * Ctrl+C da Unreal, que custa ~1.000 tokens por node.
 *
 * Herda o mesmo principio do builder ao contrario: quando o grafo tem algo que
 * o formato de texto nao sabe dizer -- uma cadeia que reconverge, um node sem
 * nome estavel -- o leitor avisa em vez de emitir um texto que parece completo
 * e nao e'. Um texto que volta como um grafo diferente do original e' o pior
 * resultado possivel, porque a diferenca so' aparece depois.
 */
class FNodeScribeReader
{
public:
	struct FResult
	{
		FString Text;
		TArray<FNodeScribeDiagnostic> Diagnostics;

		int32 NodeCount = 0;
		int32 ErrorCount = 0;
		int32 WarningCount = 0;

		/**
		 * Nodes que existem no grafo e nao existem no texto: os de dado que nao
		 * alimentam ninguem.
		 *
		 * Sao inofensivos numa leitura -- viram uma nota no fim. Sao fatais para
		 * quem pensa em apagar o grafo e recolar a partir deste texto, porque
		 * eles nao voltam. Por isso saem contados a' parte, e nao so' como nota.
		 */
		int32 LostNodeCount = 0;
	};

	/**
	 * @param Nodes      os nodes a transcrever. Nodes ligados a alguem de fora
	 *                   dessa lista viram aviso, nao ligacao silenciosa.
	 * @param Blueprint  dono do grafo, usado para reconhecer variaveis proprias.
	 */
	static FResult Read(const TArray<UEdGraphNode*>& Nodes, UBlueprint* Blueprint);

	/** Atalho para o grafo inteiro. */
	static FResult ReadGraph(UEdGraph* Graph, UBlueprint* Blueprint);
};
