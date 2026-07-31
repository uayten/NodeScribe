"""Grafo de Blueprint como texto compacto.

O motivo deste toolset existir e' custo. Montar um grafo pelas ferramentas
convencionais gasta a maior parte dos tokens *descobrindo* identificadores de
node -- uma chamada por tipo, cada uma devolvendo dezenas de resultados. O
NodeScribe resolve os nomes localmente, com busca tolerante, entao um grafo
inteiro cabe em uma chamada.
"""

import unreal

import toolset_registry


@unreal.uclass()
class NodeScribeTools(unreal.ToolsetDefinition):
    """Le' e escreve grafos de Blueprint inteiros como texto, em uma chamada."""

    @toolset_registry.tool_call
    @staticmethod
    def write_graph(graph: unreal.EdGraph, text: str) -> str:
        """Cria nodes num grafo a partir de texto no formato NodeScribe.

        Uma linha por node. Chame get_format_docs() antes da primeira vez.

        Nao levanta excecao: linha que nao resolve vira comentario vermelho no
        grafo, e o resto continua sendo criado. O retorno diz o que houve.

        Args:
            graph: O grafo a popular.
            text: O script no formato NodeScribe.
        Returns:
            Quantos nodes entraram, e uma linha por diagnostico.
        """
        return unreal.NodeScribeLibrary.write_graph(graph, text)

    @toolset_registry.tool_call
    @staticmethod
    def read_graph(graph: unreal.EdGraph) -> str:
        """Le' um grafo inteiro como texto no formato NodeScribe.

        O texto volta colavel em write_graph. O cabecalho traz o asset, o grafo
        e as variaveis declaradas. Avisos sobre o que nao volta igual saem
        comentados no fim.

        Args:
            graph: O grafo a ler.
        Returns:
            O script equivalente ao grafo.
        """
        return unreal.NodeScribeLibrary.read_graph(graph)

    @toolset_registry.tool_call
    @staticmethod
    def save_all_and_quit() -> str:
        """Salva tudo e fecha o editor.

        Serve para recompilar o plugin sem depender de alguem clicar no X --
        a Unreal segura os binarios enquanto esta' aberta.

        Recusa se houver Play In Editor rodando. Depois disto a conexao MCP
        cai; reabrir o editor e' por fora.

        Returns:
            O que foi salvo, ou o motivo de nao ter fechado.
        """
        return unreal.NodeScribeLibrary.save_all_and_quit()

    @toolset_registry.tool_call
    @staticmethod
    def get_format_docs() -> str:
        """Devolve a especificacao do formato NodeScribe.

        Chame uma vez por sessao, antes de escrever um grafo pela primeira vez.
        """
        return unreal.NodeScribeLibrary.get_format_docs()
