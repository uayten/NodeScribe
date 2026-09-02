#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/**
 * As duas configuracoes que separam "plugin instalado" de "plugin funcionando".
 *
 * O NodeScribe nao fala MCP: ele registra um toolset no ToolsetRegistry, e quem
 * poe aquilo no ar e' o plugin ModelContextProtocol, da propria Engine. Sao
 * dependencias opcionais no `.uplugin` -- sem elas o plugin continua inteiro do
 * lado do editor, so' que o assistente nao alcanca nada.
 *
 * O problema e' que instalar os tres nao basta, e as duas coisas que faltam nao
 * dao erro nenhum -- e' tudo silencio:
 *
 * 1. O servidor da Engine nasce desligado (`bAutoStartServer` e' false por
 *    padrao). Sem ligar, cada abertura do editor exige rodar
 *    `ModelContextProtocol.StartServer` na mao.
 *
 * 2. O cliente precisa saber o endereco. No Claude Code isso e' uma entrada em
 *    `.mcp.json` na raiz do projeto, que ninguem escreve sozinho.
 *
 * Nos dois casos a falha e' calada: o assistente conecta, recebe lista vazia (ou
 * nem conecta) e segue sem dizer que perdeu alguma coisa. Uma sessao inteira se
 * passou aqui com o servidor desligado antes de alguem notar.
 *
 * Nao da' para resolver por ini de plugin: a Unreal injeta `<Plugin>/Config` na
 * hierarquia do projeto, mas so' para branches que ela ja' conhece pelo nome do
 * arquivo, e o resultado nao chega no `EditorPerProjectUserSettings`. Foi
 * medido, nao deduzido.
 *
 * Entao e' codigo. Nada aqui linka com o plugin da Epic: a settings e' lida por
 * reflexao pelo nome da classe, e o servidor sobe por comando de console. Sem o
 * ModelContextProtocol instalado, os dois falham quietos, que e' o certo -- a
 * dependencia e' opcional de verdade.
 */
class FNodeScribeMcpSetup
{
public:
	/** Agenda a conferencia para depois que todo mundo carregou. */
	static void Start();

	static void Stop();

	/**
	 * Liga o `bAutoStartServer` se estiver desligado, e sobe o servidor agora.
	 *
	 * @return o que houve, em uma linha, para o log e para o menu.
	 */
	static FString GarantirServidor();

	/**
	 * Poe a entrada deste projeto no `.mcp.json`, se ainda nao estiver la'.
	 *
	 * Estritamente aditivo: entrada que ja' existe com outro nome, ou com outra
	 * URL, fica como esta'. Mexer no que a pessoa escreveu seria pior que nao
	 * fazer nada -- ela pode estar apontando para um tunel, outra porta, outro
	 * editor.
	 *
	 * @param bForcar corrige a URL de uma entrada nossa que esteja desatualizada.
	 *                E' o que o item de menu manda, e o que a partida nao manda.
	 */
	static FString GarantirEntradaNoMcpJson(bool bForcar);

	/** `<Projeto>/.mcp.json`. Exposto para a documentacao e para teste. */
	static FString GetMcpJsonPath();

	/** Registra o item em Tools, para refazer as duas coisas sob demanda. */
	static void RegisterStartupHook();

	static void Unregister();

private:
	static void RegisterMenu();
	static bool Conferir(float DeltaTime);

	/**
	 * Porta e caminho configurados no plugin da Epic, por reflexao.
	 *
	 * @return false se o ModelContextProtocol nao estiver no projeto.
	 */
	static bool LerEnderecoDoServidor(int32& OutPorta, FString& OutCaminho);

	static FTSTicker::FDelegateHandle TickHandle;
	static FDelegateHandle StartupCallbackHandle;
};
