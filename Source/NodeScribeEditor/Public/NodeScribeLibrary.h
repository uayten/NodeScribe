#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NodeScribeLibrary.generated.h"

class UEdGraph;

/**
 * A superficie do NodeScribe para quem chama de fora: Python, MCP, automacao.
 *
 * Existe por causa de custo, nao de capacidade. Um agente que monta grafo pelo
 * MCP convencional gasta a maior parte dos tokens *descobrindo* nomes de node
 * -- uma chamada por tipo, cada uma devolvendo dezenas de identificadores.
 * O catalogo do NodeScribe resolve `Print String` localmente, entao um grafo
 * inteiro cabe em duas chamadas e algumas centenas de tokens.
 */
UCLASS()
class NODESCRIBEEDITOR_API UNodeScribeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Transcreve texto do formato NodeScribe para nodes reais no grafo.
	 *
	 * @return Diagnosticos em texto, uma linha por mensagem. Vazio = tudo certo.
	 *         Nunca lanca: linha que nao resolve vira comentario vermelho no
	 *         grafo e uma linha aqui, e o resto do texto continua sendo criado.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteGraph(UEdGraph* Graph, const FString& Text);

	/** Le' o grafo inteiro de volta como texto no mesmo formato. */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadGraph(UEdGraph* Graph);

	/** A especificacao do formato, para quem nunca a viu. */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString GetFormatDocs();

	/**
	 * Salva tudo e fecha o editor.
	 *
	 * Existe porque recompilar o plugin exige o editor fechado, e sem isto cada
	 * ciclo de correcao para' esperando alguem clicar no X.
	 *
	 * Recusa enquanto houver Play In Editor rodando: fechar no meio de um teste
	 * surpreende, e o ganho de tempo nao paga isso. O fechamento e' adiado um
	 * instante para esta resposta conseguir sair antes de a conexao cair.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString SaveAllAndQuit();
};
