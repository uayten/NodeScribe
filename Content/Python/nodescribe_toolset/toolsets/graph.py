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
    """Grafos de Blueprint e propriedades de objeto como texto, em uma chamada."""

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
    def read_object(target: unreal.Object, filter: str = '') -> str:
        """Le' um objeto, classe, CDO, ator ou asset como ficha de propriedades.

        Uma linha por propriedade, e so' o que difere do valor de fabrica --
        num CDO tipico isso e' ~5% delas. A contagem no fim confirma que o
        resto esta' no padrao.

        Nao existe passo separado de listar o esquema. Procurando uma
        propriedade especifica, passe o filtro nesta mesma chamada e a
        resposta ja' vem com tipo e valor. Nunca leia tudo para depois
        procurar.

        Args:
            target: O objeto a ler. Blueprint e classe viram o CDO delas.
            filter: Vazio devolve o que mudou. Com texto, devolve as
                    propriedades cujo nome contem esse texto.
        Returns:
            A ficha.
        """
        return unreal.NodeScribeLibrary.read_object(target, filter)

    @toolset_registry.tool_call
    @staticmethod
    def write_object(target: unreal.Object, text: str) -> str:
        """Aplica uma ficha de propriedades num objeto, classe, CDO ou asset.

        Mesmo formato de read_object. O texto e' uma **lista de mudancas**, nao
        o estado final: nada e' apagado, e colar de volta uma ficha inteira nao
        mexe em nada alem do que as linhas dizem. Mande so' as linhas que mudam.

        `Nome = padrao` devolve a propriedade ao valor de fabrica.
        Bloco indentado sob `Componente:` ou sob `Struct:` alcanca dentro deles.

        Nao cria variavel nem componente. Linha que nao resolve vira
        diagnostico com os nomes parecidos, e as outras sao aplicadas.

        Args:
            target: O objeto a alterar. Blueprint e classe viram o CDO delas.
            text: As linhas no formato da ficha.
        Returns:
            Quantas propriedades mudaram, e uma linha por diagnostico.
        """
        return unreal.NodeScribeLibrary.write_object(target, text)

    @toolset_registry.tool_call
    @staticmethod
    def create_asset(path: str, parent: str) -> str:
        """Cria um asset vazio.

        Existe porque o toolset nativo tem duplicate, move e delete, e nao tem
        criacao -- sem isto, todo asset novo depende de alguem clicar.

        Nunca sobrescreve, e so' cria dentro de /Game/. O asset fica sujo, sem
        salvar, como qualquer um recem-criado no editor.

        Args:
            path: Onde criar, com nome: '/Game/BossRush/Testes/BTTask_Foo'.
            parent: O tipo, pelo nome de tela: 'BTTask_BlueprintBase',
                    'GameplayEffect', 'BlackboardData', 'BehaviorTree'.
        Returns:
            O caminho do que foi criado, ou o motivo de nao ter dado.
        """
        return unreal.NodeScribeLibrary.create_asset(path, parent)

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
