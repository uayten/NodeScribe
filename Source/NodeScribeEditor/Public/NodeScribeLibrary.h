#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NodeScribeLibrary.generated.h"

class UBlendSpace;
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
	 * @param bReplace  true apaga o que ja' esta' no grafo antes de escrever, em
	 *                  vez de acrescentar. **So' se o grafo atual voltar limpo
	 *                  na leitura** -- se houver qualquer aviso de "isso nao
	 *                  volta igual", ou node de dado que ninguem consome, a
	 *                  substituicao e' recusada e nada e' alterado. Apagar a
	 *                  partir de um texto que perdeu alguma coisa destruiria
	 *                  justamente o que o texto nao soube dizer.
	 *
	 * @return Diagnosticos em texto, uma linha por mensagem. Vazio = tudo certo.
	 *         Nunca lanca: linha que nao resolve vira comentario vermelho no
	 *         grafo e uma linha aqui, e o resto do texto continua sendo criado.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteGraph(UEdGraph* Graph, const FString& Text, bool bReplace = false);

	/** Le' o grafo inteiro de volta como texto no mesmo formato. */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadGraph(UEdGraph* Graph);

	/**
	 * Esvazia o grafo, e **devolve como texto o que estava nele**.
	 *
	 * E' o gesto que faltava. A substituicao do `WriteGraph` se recusa a apagar
	 * um grafo que o texto nao sabe descrever, e essa recusa esta' certa: o que
	 * some nao aparece no que sobrou. Mas ha' caso em que a intencao e'
	 * justamente jogar fora -- os stubs que um Blueprint novo traz de fabrica,
	 * uma tentativa que falhou --, e ali a recusa so' obriga alguem a fazer na
	 * mao o que a chamada faria.
	 *
	 * O que muda em relacao a um modo forcado, que este plugin nao tem: nada
	 * some calado. O grafo volta transcrito na resposta, com os avisos da
	 * leitura junto -- inclusive o aviso de que uma parte nao coube em texto.
	 * Quem apagou fica com o que apagou na mao.
	 *
	 * Node que a Engine marca como indelevel fica: o Output Pose de um
	 * AnimGraph, o Result de uma transicao, a entrada de uma funcao. Sao os
	 * mesmos que o Ctrl+A + Delete do editor preserva.
	 *
	 * Uma transacao so': o Ctrl+Z devolve o grafo inteiro.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ClearGraph(UEdGraph* Graph);

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
	 * @param Path     onde criar, com nome: `/Game/BossRush/Testes/BTTask_Foo`.
	 * @param Parent   o tipo, pelo nome de tela: `BTTask_BlueprintBase`,
	 *                 `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 * @param Options  propriedades da factory, no formato da ficha, aplicadas
	 *                 antes de criar. Ha' asset que nao se cria so' com o tipo:
	 *                 um AnimBlueprint precisa saber o esqueleto, e um
	 *                 BlendSpace tambem. Consertar depois nao serve -- no asset
	 *                 pronto o esqueleto e' somente-leitura, de proposito.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString CreateAsset(const FString& Path, const FString& Parent, const FString& Options);

	/**
	 * Preenche um BlendSpace: os eixos e os samples, por texto.
	 *
	 * Existe porque a ficha nao alcanca. `SampleData` e `BlendParameters` sao
	 * arrays de struct, e escrever neles a mao pularia a validacao da Engine --
	 * que e' quem recalcula a malha de interpolacao. Sem a malha o BlendSpace
	 * existe, abre, mostra os pontos e nao interpola nada.
	 *
	 *     eixo X : Speed = 0 .. 600
	 *     MM_Idle = 0
	 *     MF_Unarmed_Walk_Fwd = 300
	 *
	 * Nao apaga o que ja' esta' la': o texto e' uma lista de mudancas.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteBlendSpace(UBlendSpace* BlendSpace, const FString& Text);

	/**
	 * As Gameplay Tags declaradas, uma por linha.
	 *
	 * @param Filter  vazio traz todas; com texto, so' as que contem esse trecho.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadTags(const FString& Filter);

	/**
	 * O espelho: cria as tags do texto, uma por linha.
	 *
	 * Existe por capacidade, nao por economia -- tag nao e' asset nem
	 * propriedade, vive num ini, e sem isto ela so' nasce por alguem abrir a
	 * janela de configuracao. Um cooldown novo precisa da tag existir antes de o
	 * Gameplay Effect poder concede-la.
	 *
	 * Tag ja' declarada e' pulada sem erro. Nao apaga nem renomeia.
	 *
	 * @param Source  o ini de destino, pelo nome de tela (`BossRush.ini`).
	 *                Vazio deixa a Engine escolher, que da' o
	 *                `DefaultGameplayTags.ini`. Fonte que nao existe e' recusada
	 *                com a lista das que existem.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteTags(const FString& Text, const FString& Source);

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
