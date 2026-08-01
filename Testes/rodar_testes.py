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
    relato = unreal.NodeScribeLibrary.create_asset(caminho, 'Actor')
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

    resumo = '%d caso(s), %d falha(s)' % (len(CASOS), len(falhas))
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
