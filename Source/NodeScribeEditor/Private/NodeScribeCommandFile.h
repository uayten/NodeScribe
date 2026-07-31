#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/**
 * Um comando por arquivo, para quem nao tem MCP.
 *
 * O ciclo de desenvolvimento precisa fechar o editor para recompilar, e hoje
 * isso chega pelo MCP. Abrir nunca dependeu dele -- e' processo novo pelo
 * terminal --, mas fechar sim, e numa sessao o editor estava aberto com o MCP
 * fora do ar: nao houve como fechar sem pedir para uma pessoa clicar no X.
 *
 * Matar o processo resolveria e e' a pior saida: perde trabalho nao salvo e
 * pula a recusa de fechar com Play In Editor rodando.
 *
 * Aqui nao ha' rede, porta nem protocolo -- e' um arquivo que aparece e some.
 * Nao tem o que cair. Um servidor proprio dentro do plugin seria mais codigo
 * para resolver menos, porque continuaria sendo algo que precisa estar de pe'.
 *
 * `Saved/NodeScribe/comando.txt` some ao ser lido; a saida vai para
 * `resposta.txt`, com a mesma mensagem que o MCP devolveria.
 */
class FNodeScribeCommandFile
{
public:
	static void Start();
	static void Stop();

	/** Caminhos, expostos para a documentacao e para teste. */
	static FString GetCommandPath();
	static FString GetResponsePath();

private:
	static bool Tick(float DeltaTime);

	static FTSTicker::FDelegateHandle TickHandle;
};
