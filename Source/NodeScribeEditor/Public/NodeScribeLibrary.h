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

	/**
	 * Um objeto como ficha: uma linha por propriedade, so' o que difere do
	 * padrao.
	 *
	 * Existe pelo mesmo motivo do resto. Listar as propriedades de um Character
	 * pelo caminho convencional devolve o esquema JSON inteiro da classe --
	 * ordem de 10.000 tokens, e so' o formato, sem nenhum valor; os valores
	 * pedem uma segunda chamada. A ficha responde as duas coisas de uma vez,
	 * porque 95% das propriedades estao no valor de fabrica e o valor de
	 * fabrica se resolve deste lado.
	 *
	 * @param Filter  vazio devolve o que mudou. Com texto, devolve as
	 *                propriedades cujo nome casa, com tipo e valor -- e' o que
	 *                dispensa o passo separado de listar o esquema.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadObject(UObject* Object, const FString& Filter);

	/**
	 * O espelho: aplica uma ficha.
	 *
	 * O texto e' uma lista de mudancas, nao o estado final -- colar de volta uma
	 * ficha inteira nao mexe em nada alem do que as linhas dizem, e nada e'
	 * apagado. `= padrao` devolve a propriedade ao valor de fabrica.
	 *
	 * Nao levanta excecao: linha que nao resolve vira diagnostico com os nomes
	 * parecidos, e as outras continuam sendo aplicadas.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteObject(UObject* Object, const FString& Text);

	/**
	 * Cria um asset vazio.
	 *
	 * Existe por capacidade, nao por economia: o toolset nativo da Engine tem
	 * duplicate, move e delete, e nao tem criacao. Sem isto, todo asset novo e'
	 * um pedido de clique para uma pessoa, e o resto do trabalho para'.
	 *
	 * @param Path    onde criar, com nome: `/Game/BossRush/Testes/BTTask_Foo`.
	 * @param Parent  o tipo, pelo nome de tela: `BTTask_BlueprintBase`,
	 *                `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString CreateAsset(const FString& Path, const FString& Parent);

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
