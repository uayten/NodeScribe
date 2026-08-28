"""Testes de ida e volta do NodeScribe, sem abrir a interface.

    UnrealEditor-Cmd.exe <projeto>.uproject -run=pythonscript
        -script="<plugin>/Testes/rodar_testes.py" -unattended -nopause -NullRHI

O relatorio sai em `Saved/NodeScribe/testes.txt` e o resumo no log. Falhou,
o processo sai com codigo diferente de zero.

## O que ele testa, e por que assim

**Ponto fixo.** Cada caso e' escrito num Blueprint vazio, lido (T1), o T1 e'
escrito num *segundo* Blueprint vazio, e esse e' lido (T2). Se leitor e escritor
sao espelhos, T1 e T2 sao identicos. Quando nao sao, um dos dois esta' mentindo,
e o diff diz qual.

E' o teste que nao precisa de resposta guardada a mao: nada para atualizar
quando o formato mudar, e nenhum caso que passa por estar desatualizado junto
com o codigo.

Ele pega o que a revisao de agosto/2026 pegou: nome de node com parenteses,
Break com um campo visivel so', tipo de variavel que volta diferente. Nao pega
o que e' so' legibilidade -- `True:` num node que nao e' Branch volta como
`True:` e fecha o ponto fixo do mesmo jeito. Para isso ainda e' preciso olhar.

**So' o corpo entra na comparacao.** Linha comecada com `#` e' cabecalho,
ancora ou diagnostico -- nenhum e' grafo. E alguns diferem de proposito: um node
orfao vira nota no T1, e nao existe no segundo Blueprint para virar nota no T2.

**Aviso esperado.** Onde o formato *nao* da' conta, o combinado e' falhar em voz
alta. Esses casos declaram o trecho do aviso que tem que aparecer, e a ausencia
do aviso reprova.
"""

import difflib
import os
import traceback

import unreal

# Assets de teste nascem aqui e nunca sao salvos: o commandlet sai sem gravar.
# Rodando com o editor aberto, nao salve depois -- e' rascunho.
DESTINO = '/Game/BossRush/Testes'


CASOS = [
    {
        'nome': 'colecoes',
        'texto': '''
variavel Espelhos : Mapa de Int64 para Actor
variavel Marcados : Conjunto de Name
variavel Inimigos : Array de Actor
variavel Contador : Integer

evento Testar
Print String (In String = "colecoes")
''',
    },
    {
        'nome': 'tipos',
        'texto': '''
variavel A : Vector
variavel B : Timer Handle
variavel C : Transform
variavel D : Rotator
variavel E : Actor
variavel F : Pawn
variavel G : Linear Color
variavel H : Array de Vector
variavel I : Mapa de Name para Vector

evento Testar
Print String (In String = "tipos")
''',
    },
    {
        'nome': 'conversao_implicita',
        'texto': '''
variavel Contador : Integer

evento Testar
texto = Append (A = "n = ", B = $Contador (Integer -> String))
Print String (In String = $texto)
''',
    },
    {
        'nome': 'branch_e_break',
        'texto': '''
variavel Posicao : Vector
variavel Ativo : Boolean

evento Testar
Branch (Condition = $Ativo)
  verdadeiro:
    p = Break Vector ($Posicao)
    Print String (In String = "no ar", Duration = 3.5)
  falso:
    Print String (In String = "no chao")
''',
    },
    {
        'nome': 'macro_e_loop',
        'texto': '''
variavel Inimigos : Array de Actor

evento Testar
For Each Loop (Array = $Inimigos)
  corpo:
    Print String (In String = "um inimigo")
''',
    },
    {
        'nome': 'acao_assincrona',
        'texto': '''
evento Testar
Async Load Game from Slot (Slot Name = "teste")
  Completed:
    Print String (In String = "carregou")
''',
    },
    {
        'nome': 'evento_customizado_reaproveitado',
        'texto': '''
evento Atualizar Tela
Print String (In String = "atualizou")

evento Testar
Print String (In String = "vai atualizar")
Atualizar Tela
''',
    },
    {
        'nome': 'buraco_declarado',
        'texto': '''
evento Testar
Spawn Sound 2D (Sound = ?)
''',
        'espera_aviso': ['ficou vazio esperando'],
    },
]


# ---------------------------------------------------------------------------


def corpo(texto):
    """So' as linhas de grafo: sem cabecalho, ancora nem diagnostico."""
    linhas = []
    for linha in texto.splitlines():
        podada = linha.strip()
        if not podada or podada.startswith('#'):
            continue
        linhas.append(linha.rstrip())
    return linhas


def novo_blueprint(nome):
    caminho = '%s/%s' % (DESTINO, nome)
    # `options` e' obrigatorio na assinatura da UFUNCTION -- Python nao tem o
    # valor padrao que o C++ tem. Sem a string vazia aqui, a suite inteira morre
    # na primeira linha com `TypeError: create_asset() required argument
    # 'options' (pos 3) not found`, e nao roda desde que `options` foi criado.
    relato = unreal.NodeScribeLibrary.create_asset(caminho, 'Actor', '')
    if relato.startswith('[erro]'):
        return None, relato

    bp = unreal.load_asset(caminho)
    if not bp:
        return None, 'nao consegui carregar %s' % caminho

    grafo = unreal.load_object(bp, 'EventGraph')
    if not grafo:
        return None, 'nao achei o EventGraph de %s' % caminho

    return grafo, ''


def ida_e_volta(caso):
    """Devolve (passou, [linhas do relato])."""
    nome = caso['nome']
    relato = []

    grafo_a, erro = novo_blueprint('NS_%s_a' % nome)
    if not grafo_a:
        return False, ['nao consegui preparar o primeiro Blueprint: ' + erro]

    escrita = unreal.NodeScribeLibrary.write_graph(grafo_a, caso['texto'])
    t1 = unreal.NodeScribeLibrary.read_graph(grafo_a)

    if '[erro]' in escrita:
        relato.append('a escrita reclamou:')
        relato.extend('  ' + l for l in escrita.splitlines())
        return False, relato

    for esperado in caso.get('espera_aviso', []):
        if esperado not in t1 and esperado not in escrita:
            relato.append('faltou o aviso esperado: %r' % esperado)
            relato.append('o plugin tem que falhar em voz alta aqui.')
            return False, relato

    grafo_b, erro = novo_blueprint('NS_%s_b' % nome)
    if not grafo_b:
        return False, ['nao consegui preparar o segundo Blueprint: ' + erro]

    unreal.NodeScribeLibrary.write_graph(grafo_b, t1)
    t2 = unreal.NodeScribeLibrary.read_graph(grafo_b)

    a, b = corpo(t1), corpo(t2)
    if a == b:
        return True, []

    relato.append('a segunda leitura nao bateu com a primeira:')
    relato.extend(difflib.unified_diff(a, b, 'T1 (leitura do que escrevi)',
                                       'T2 (leitura do T1 colado)', lineterm='', n=2))
    relato.append('')
    relato.append('--- T1 inteiro ---')
    relato.extend(t1.splitlines())
    return False, relato


def substituir_troca_o_grafo():
    """Substituir num grafo que volta limpo troca o conteudo, nao acumula."""
    grafo, erro = novo_blueprint('NS_substituir_limpo')
    if not grafo:
        return False, [erro]

    unreal.NodeScribeLibrary.write_graph(grafo, 'evento Antes\nPrint String (In String = "antes")\n')
    relato = unreal.NodeScribeLibrary.write_graph(
        grafo, 'evento Depois\nPrint String (In String = "depois")\n', True)

    texto = unreal.NodeScribeLibrary.read_graph(grafo)

    problemas = []
    if 'apagados' not in relato:
        problemas.append('a substituicao nao apagou nada: %s' % relato)
    if 'event Antes' in texto:
        problemas.append('o evento antigo continua no grafo')
    if 'event Depois' not in texto:
        problemas.append('o evento novo nao entrou')

    if problemas:
        problemas.append('')
        problemas.extend(texto.splitlines())
        return False, problemas

    return True, []


def substituir_recusa_grafo_com_perda():
    """Grafo com node de dado solto nao pode ser substituido: ele nao volta."""
    grafo, erro = novo_blueprint('NS_substituir_recusa')
    if not grafo:
        return False, [erro]

    # `pc` nao alimenta ninguem: fica orfao, some do texto, e nao voltaria.
    unreal.NodeScribeLibrary.write_graph(
        grafo, 'evento Antes\nPrint String (In String = "antes")\npc = Get Player Controller\n')

    antes = unreal.NodeScribeLibrary.read_graph(grafo)
    relato = unreal.NodeScribeLibrary.write_graph(grafo, 'evento Depois\n', True)
    depois = unreal.NodeScribeLibrary.read_graph(grafo)

    problemas = []
    if '[erro]' not in relato:
        problemas.append('a substituicao devia ter sido recusada, e o retorno foi: %s' % relato)
    if 'event Depois' in depois:
        problemas.append('recusou e mesmo assim escreveu')
    if corpo(antes) != corpo(depois):
        problemas.append('recusou e mesmo assim mexeu no grafo')

    if problemas:
        problemas.append('')
        problemas.append('--- retorno ---')
        problemas.extend(relato.splitlines())
        return False, problemas

    return True, []


def qualquer_esqueleto():
    """Um Skeleton do projeto, ou None.

    Os casos de animacao precisam de um esqueleto de verdade -- um AnimBlueprint
    nao se cria sem ele, e num asset pronto o campo e' somente-leitura. Pegar o
    primeiro que o registry tiver mantem a suite rodavel em qualquer projeto:
    o que se testa e' a ida e volta do texto, e para isso tanto faz de quem e' o
    esqueleto.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    achados = registry.get_assets_by_class(
        unreal.TopLevelAssetPath('/Script/Engine', 'Skeleton'), False)

    return str(achados[0].package_name) if achados else None


def anim_grafo(nome, esqueleto):
    """Um AnimGraph vazio num AnimBlueprint novo."""
    caminho = '%s/%s' % (DESTINO, nome)

    relato = unreal.NodeScribeLibrary.create_asset(
        caminho, 'AnimBlueprint', 'TargetSkeleton = %s' % esqueleto)
    if relato.startswith('[erro]'):
        return None, relato

    bp = unreal.load_asset(caminho)
    if not bp:
        return None, 'nao consegui carregar %s' % caminho

    grafo = unreal.load_object(bp, 'AnimGraph')
    if not grafo:
        return None, 'nao achei o AnimGraph de %s' % caminho

    return grafo, ''


def maquina_de_estados_volta_igual():
    """Estado, conduto, alias e opcao de transicao: ida e volta.

    Sem asset nenhum de proposito. O que se testa aqui e' a estrutura -- quem
    e' estado, quem e' conduto, quem e' alias, o que cada transicao guarda no
    painel --, e por muito tempo nada disso cabia no texto: alias e conduto nao
    eram declarados, e uma transicao com regra automatica voltava identica a uma
    transicao morta.
    """
    esqueleto = qualquer_esqueleto()
    if not esqueleto:
        return True, ['pulado: este projeto nao tem nenhum Skeleton.']

    texto = """
variavel Ativo : Boolean

Maquina = State Machine
  estado Parado (Always Reset on Entry = true):
  estado Andando:
  alias Qualquer Um:
    Andando
    Parado
  conduto Passagem:
    Get Ativo
  Parado -> Passagem (Crossfade Duration = 0.35, Blend Mode = Cubic):
    Get Ativo
  Passagem -> Andando (Priority Order = 2):
    Get Ativo
  Andando -> Parado (Automatic Rule Based on Sequence Player in State = true):
"""

    grafo_a, erro = anim_grafo('NS_maquina_a', esqueleto)
    if not grafo_a:
        return False, ['nao consegui preparar o primeiro AnimBlueprint: ' + erro]

    escrita = unreal.NodeScribeLibrary.write_graph(grafo_a, texto)
    if '[erro]' in escrita:
        relato = ['a escrita reclamou:']
        relato.extend('  ' + l for l in escrita.splitlines())
        return False, relato

    t1 = unreal.NodeScribeLibrary.read_graph(grafo_a)

    # A transicao automatica nasce sem regra de proposito. Um aviso de "nunca
    # dispara" aqui seria falso, e ensinaria a ignorar o aviso que existe para
    # quando e' verdade.
    if 'nunca dispara' in escrita:
        relato = ['avisou que uma transicao nunca dispara, e a automatica dispara:']
        relato.extend('  ' + l for l in escrita.splitlines())
        return False, relato

    grafo_b, erro = anim_grafo('NS_maquina_b', esqueleto)
    if not grafo_b:
        return False, ['nao consegui preparar o segundo AnimBlueprint: ' + erro]

    unreal.NodeScribeLibrary.write_graph(grafo_b, t1)
    t2 = unreal.NodeScribeLibrary.read_graph(grafo_b)

    a, b = corpo(t1), corpo(t2)
    if a == b:
        return True, []

    relato = ['a segunda leitura nao bateu com a primeira:']
    relato.extend(difflib.unified_diff(a, b, 'T1', 'T2', lineterm='', n=2))
    relato.append('')
    relato.append('--- T1 inteiro ---')
    relato.extend(t1.splitlines())
    return False, relato


def blendspace_ganha_malha():
    """Preencher um BlendSpace tem que reconstruir a malha de interpolacao.

    Regressao do bug mais silencioso que o plugin ja' teve: os samples entravam,
    o asset abria, o editor desenhava os pontos -- e o BlendSpace Player devolvia
    pose de referencia, porque quem interpola e' a malha, e ela ficava vazia. O
    Blueprint compilava sem um aviso sequer.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    achados = registry.get_assets_by_class(
        unreal.TopLevelAssetPath('/Script/Engine', 'AnimSequence'), False)

    if not achados:
        return True, ['pulado: este projeto nao tem nenhuma AnimSequence.']

    sequencia = unreal.load_asset(str(achados[0].package_name))
    esqueleto = sequencia.get_editor_property('skeleton')
    if not esqueleto:
        return True, ['pulado: `%s` nao tem esqueleto.' % sequencia.get_name()]

    caminho = '%s/NS_blendspace' % DESTINO
    relato = unreal.NodeScribeLibrary.create_asset(
        caminho, 'BlendSpace1D', 'TargetSkeleton = %s' % esqueleto.get_path_name())
    if relato.startswith('[erro]'):
        return False, [relato]

    blendspace = unreal.load_asset(caminho)
    if not blendspace:
        return False, ['nao consegui carregar %s' % caminho]

    linhas = [
        'eixo X : Velocidade = 0 .. 600',
        '%s = 0' % sequencia.get_path_name(),
    ]
    escrita = unreal.NodeScribeLibrary.write_blend_space(blendspace, chr(10).join(linhas))

    if '[erro]' in escrita:
        return False, ['a escrita reclamou:'] + escrita.splitlines()

    texto = unreal.NodeScribeLibrary.read_object(blendspace, '')
    if 'malha de interpolacao esta' in texto:
        return False, [
            'o sample entrou e a malha continuou vazia: este BlendSpace devolve',
            'pose de referencia, e nada no grafo denuncia isso.',
            '',
        ] + texto.splitlines()

    return True, []


def cadeia_chega_no_function_entry():
    """Colar num grafo de funcao tem que ligar a cadeia na entrada.

    O Construction Script e' o caso a mao: a entrada dele ja' existe e nao se
    cria por linha, igual ao Output Pose de um AnimGraph. Sem a ligacao, a
    cadeia entra inteira, compila sem um aviso, e nunca roda -- e a leitura de
    volta so' denuncia isso pelo `Function Entry` aparecer sozinho no fim, em
    vez de na frente da cadeia que ele dispara.
    """
    caminho = '%s/NS_function_entry' % DESTINO

    relato = unreal.NodeScribeLibrary.create_asset(caminho, 'Actor', '')
    if relato.startswith('[erro]'):
        return False, [relato]

    bp = unreal.load_asset(caminho)
    grafo = unreal.load_object(bp, 'UserConstructionScript') if bp else None
    if not grafo:
        return False, ['nao achei o UserConstructionScript de %s' % caminho]

    escrita = unreal.NodeScribeLibrary.write_graph(
        grafo, 'Print String (In String = "do construction script")')

    if '[erro]' in escrita:
        return False, ['a escrita reclamou:'] + escrita.splitlines()

    linhas = corpo(unreal.NodeScribeLibrary.read_graph(grafo))

    # A leitura percorre a partir da entrada: com a cadeia ligada, o
    # `Function Entry` abre o texto. Solto, ele sai depois, como orfao.
    if not linhas or linhas[0].strip() != 'Function Entry':
        return False, [
            'o Function Entry nao abre a leitura: a cadeia entrou solta e',
            'nunca roda.',
            '',
        ] + linhas

    return True, []


OUTROS = [
    ('cadeia_chega_no_function_entry', cadeia_chega_no_function_entry),
    ('substituir_troca_o_grafo', substituir_troca_o_grafo),
    ('substituir_recusa_grafo_com_perda', substituir_recusa_grafo_com_perda),
    ('maquina_de_estados_volta_igual', maquina_de_estados_volta_igual),
    ('blendspace_ganha_malha', blendspace_ganha_malha),
]


def main():
    partes = []
    falhas = []

    for caso in CASOS:
        passou, relato = ida_e_volta(caso)
        partes.append('%s  %s' % ('ok   ' if passou else 'FALHA', caso['nome']))

        if not passou:
            falhas.append(caso['nome'])
            partes.extend('       ' + l for l in relato)
            partes.append('')
        elif relato:
            # Caso que passou e tem o que dizer so' diz uma coisa: que nao
            # rodou. Um teste pulado impresso como `ok` e' o mesmo buraco que o
            # plugin recusa em todo lugar -- parece cobertura e nao e'.
            partes.extend('       ' + l for l in relato)

    for nome, funcao in OUTROS:
        # Uma excecao aqui derrubava o `main` inteiro, e o relatorio -- que so'
        # e' escrito no fim -- nao saia: ficava no disco o de uma execucao
        # anterior, com o resultado antigo e a contagem antiga. Ler aquilo
        # depois de uma rodada que morreu e' pior do que nao ter relatorio.
        try:
            passou, relato = funcao()
        except Exception:
            passou, relato = False, traceback.format_exc().splitlines()

        partes.append('%s  %s' % ('ok   ' if passou else 'FALHA', nome))

        if not passou:
            falhas.append(nome)
            partes.extend('       ' + l for l in relato)
            partes.append('')
        elif relato:
            partes.extend('       ' + l for l in relato)

    resumo = '%d caso(s), %d falha(s)' % (len(CASOS) + len(OUTROS), len(falhas))
    partes.append('')
    partes.append(resumo)

    texto = '\n'.join(partes)

    saida = os.path.join(unreal.Paths.project_saved_dir(), 'NodeScribe', 'testes.txt')
    os.makedirs(os.path.dirname(saida), exist_ok=True)
    with open(saida, 'w', encoding='utf-8') as arquivo:
        arquivo.write(texto)

    unreal.log('NodeScribe testes: %s -- relatorio em %s' % (resumo, saida))

    if falhas:
        unreal.log_error('NodeScribe testes: falharam %s' % ', '.join(falhas))
        raise RuntimeError('NodeScribe: %s' % resumo)


main()
