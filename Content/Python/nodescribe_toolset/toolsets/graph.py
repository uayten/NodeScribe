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
    def write_graph(graph: unreal.EdGraph, text: str,
                    substituir: bool | None = None) -> str:
        """Cria nodes num grafo a partir de texto no formato NodeScribe.

        Uma linha por node. Chame get_format_docs() antes da primeira vez.

        Nao levanta excecao: linha que nao resolve vira comentario vermelho no
        grafo, e o resto continua sendo criado. O retorno diz o que houve.

        Args:
            graph: O grafo a popular.
            text: O script no formato NodeScribe.
            substituir: Omitido acrescenta ao que ja' existe. True apaga o grafo
                        antes de escrever -- e **so' se o grafo atual voltar
                        limpo na leitura**. Havendo aviso de "isso nao volta
                        igual", ou node de dado que ninguem consome, a
                        substituicao e' recusada e nada muda: apagar a partir de
                        um texto que perdeu algo destruiria justamente o que o
                        texto nao soube dizer. Nao ha' modo forcado; para isso,
                        apague na mao no editor.
        Returns:
            Quantos nodes entraram, e uma linha por diagnostico.
        """
        return unreal.NodeScribeLibrary.write_graph(graph, text, bool(substituir))

    @toolset_registry.tool_call
    @staticmethod
    def read_graph(graph: unreal.EdGraph) -> str:
        """Le' um grafo inteiro como texto no formato NodeScribe.

        O texto volta colavel em write_graph. O cabecalho traz o asset, o grafo,
        o caminho exato do grafo (`refPath:`) e as variaveis declaradas. Avisos
        sobre o que nao volta igual saem comentados no fim.

        O caminho e' `/Raiz/Pasta/Asset.Asset:NomeDoGrafo`. A raiz de um asset de
        plugin e' o nome do plugin (`/JoyShockLibrary4Unreal/...`), nao `/Game/`,
        e o nome do grafo nao e' adivinhavel -- `read_object` no Blueprint lista
        os grafos que ele tem.

        Args:
            graph: O grafo a ler.
        Returns:
            O script equivalente ao grafo.
        """
        return unreal.NodeScribeLibrary.read_graph(graph)

    @toolset_registry.tool_call
    @staticmethod
    def read_object(target: unreal.Object, filter: str | None = None) -> str:
        """Le' um objeto, classe, CDO, ator ou asset como ficha de propriedades.

        Uma linha por propriedade, e so' o que difere do valor de fabrica --
        num CDO tipico isso e' ~5% delas. A contagem no fim confirma que o
        resto esta' no padrao.

        Nao existe passo separado de listar o esquema. Procurando uma
        propriedade especifica, passe o filtro nesta mesma chamada e a
        resposta ja' vem com tipo e valor. Nunca leia tudo para depois
        procurar.

        O alvo e' um caminho de objeto: `/Raiz/Pasta/Asset.Asset`. A raiz de um
        asset de plugin e' o nome do plugin (`/JoyShockLibrary4Unreal/...`), nao
        `/Game/`.

        Args:
            target: O objeto a ler. Blueprint e classe viram o CDO delas.
            filter: Omitido devolve o que mudou. Com texto, devolve as
                    propriedades cujo nome contem esse texto.
        Returns:
            A ficha.
        """
        return unreal.NodeScribeLibrary.read_object(target, filter or '')

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
    def create_asset(path: str, parent: str, options: str | None = None) -> str:
        """Cria um asset vazio.

        Existe porque o toolset nativo tem duplicate, move e delete, e nao tem
        criacao -- sem isto, todo asset novo depende de alguem clicar.

        Nunca sobrescreve, e so' cria dentro de /Game/. O asset fica sujo, sem
        salvar, como qualquer um recem-criado no editor.

        Ha' asset que nao se cria so' com o tipo: um AnimBlueprint precisa saber
        o esqueleto, e um BlendSpace tambem. E' para isso que serve `options` --
        e nao da' para deixar para depois, porque no asset pronto o esqueleto e'
        somente-leitura. Quando o tipo pede configuracao e nada vem em
        `options`, a resposta traz uma nota dizendo o que ficou em branco.

            create_asset('/Game/Anims/ABP_Sophia', 'AnimBlueprint',
                         'TargetSkeleton = /Game/MetaHumans/.../metahuman_base_skel')

        Args:
            path: Onde criar, com nome: '/Game/BossRush/Testes/BTTask_Foo'.
            parent: O tipo, pelo nome de tela: 'BTTask_BlueprintBase',
                    'GameplayEffect', 'BlackboardData', 'AnimBlueprint',
                    'BlendSpace', 'BlendSpace1D'.
            options: Propriedades da factory, no formato da ficha, uma por
                     linha. Se alguma nao for aceita, nada e' criado.
        Returns:
            O caminho do que foi criado, ou o motivo de nao ter dado.
        """
        return unreal.NodeScribeLibrary.create_asset(path, parent, options or '')

    @toolset_registry.tool_call
    @staticmethod
    def write_blendspace(blend_space: unreal.BlendSpace, text: str) -> str:
        """Preenche um BlendSpace: os eixos e os samples.

        Existe porque a ficha nao alcanca. `SampleData` e `BlendParameters` sao
        arrays de struct, e escrever neles a mao pularia a validacao da Engine,
        que e' quem recalcula a malha de interpolacao. Sem a malha o BlendSpace
        existe, abre, mostra os pontos e nao interpola nada.

        Uma linha por sample. Os eixos sao aplicados antes dos samples, apareca
        o que aparecer primeiro no texto -- um sample fora do intervalo e'
        recusado, e definir o intervalo depois nao o traz de volta.

            eixo X : Speed = 0 .. 600
            MM_Idle = 0
            MF_Unarmed_Walk_Fwd = 300
            MF_Unarmed_Jog_Fwd = 600

        Em duas dimensoes o eixo Y entra igual e o sample ganha a segunda
        posicao: `MF_Unarmed_Walk_Fwd = 0, 300`.

        Nao apaga o que ja' esta' la': o texto e' uma lista de mudancas.

        Args:
            blend_space: O asset a preencher.
            text: As linhas de eixo e de sample.
        Returns:
            Quantos samples e eixos entraram, e uma linha por problema.
        """
        return unreal.NodeScribeLibrary.write_blend_space(blend_space, text)

    @toolset_registry.tool_call
    @staticmethod
    def read_tags(filter: str | None = None) -> str:
        """Le' as Gameplay Tags declaradas, uma por linha.

        Args:
            filter: Omitido traz todas. Com texto, so' as que contem esse trecho.
        Returns:
            Uma linha `tag Nome` por tag, com a contagem no cabecalho.
        """
        return unreal.NodeScribeLibrary.read_tags(filter or '')

    @toolset_registry.tool_call
    @staticmethod
    def write_tags(text: str, source: str | None = None) -> str:
        """Cria Gameplay Tags, uma por linha.

        Existe porque tag nao e' asset nem propriedade -- vive num ini --, entao
        nem create_asset nem write_object alcancam. Sem isto, toda tag nova
        depende de alguem abrir a janela de configuracao.

        Mesmo formato de read_tags: aceita `tag X` ou so' `X`, e o cabecalho da
        leitura e' ignorado. Tag ja' declarada e' pulada sem erro. **Nao apaga
        nem renomeia** -- as duas coisas quebram todo asset que usa a tag.

        Args:
            text: Uma tag por linha.
            source: O ini de destino, pelo nome de tela: 'BossRush.ini'.
                    Omitido deixa a Engine escolher, que da'
                    'DefaultGameplayTags.ini' -- que pode nao ser onde as outras
                    tags do projeto moram. Fonte inexistente e' recusada com a
                    lista das que existem.
        Returns:
            Quantas foram criadas e em que arquivo, e uma linha por diagnostico.
        """
        return unreal.NodeScribeLibrary.write_tags(text, source or '')

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
