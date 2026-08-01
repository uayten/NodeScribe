# NodeScribe

Plugin de editor da Unreal que transcreve uma lista de nodes escrita em texto
para nodes reais no grafo de Blueprint, e faz o caminho de volta.

**A especificação do formato é [`Docs/FORMATO.md`](Docs/FORMATO.md). Leia esse
arquivo antes de escrever ou interpretar texto do NodeScribe** — as partes que
não dá para adivinhar (`$nome.Pino`, `Classe.Funcao`, `evento X de Y`, rótulos
indentados, `?` como buraco declarado) estão lá.

Quando alguém colar texto nesse formato pedindo ajuda com um grafo, é saída do
botão **Copiar grafo inteiro**. A resposta deve voltar no mesmo formato, para
ser colada de volta com **Colar**.

## Para que ele existe

Gastar o mínimo de tokens na conversa entre a IA e a Unreal. A economia vem de
**eliminar descoberta** — o catálogo resolve nomes localmente —, não de
encurtar o texto. Ao mexer aqui, não troque legibilidade por bytes: o texto
ser conferível por uma pessoa é o que torna o resto confiável.

## Princípio que orienta o código

Quando não dá para decidir com segurança, **não decide**. Node ambíguo vira
comentário vermelho no grafo; asset não especificado vira pino vazio que impede
compilar; node que o formato não sabe recriar não é criado.

Um node plausível chutado é o pior resultado possível: compila, roda, e está
errado. Ao mexer aqui, preserve isso — em caso de dúvida, falhe em voz alta.

## Estrutura

| arquivo | papel |
|---|---|
| `NodeScribeParser` | texto → statements. Não conhece a Unreal. |
| `NodeScribeCatalog` | índice de UFunctions chamáveis, montado em runtime. |
| `NodeScribeBuilder` | statements → nodes reais, ligados e posicionados. |
| `NodeScribeReader` | o caminho de volta: nodes → texto. |
| `NodeScribeGraphActions` | os três botões na barra do editor. |

Leitor e builder são espelhos: ao ensinar um tipo de node novo, os dois mudam.

## Antes de mexer no código

Leia **Desenvolvimento** e **Estado** no [`README.md`](README.md). Estão lá: em
que ponto o plugin está e qual é a frente atual, o ciclo de fechar o editor →
compilar → reabrir → testar (com os comandos prontos), como testar por ida e
volta, os quatro invariantes que não podem quebrar, e o que uma proposta de
ferramenta nova precisa responder antes de virar código.

Frente atual: **ler a IA de um inimigo inteira**, o Golem do BossRush. A ficha
(`read_object` / `write_object`), o blackboard, a árvore de BT, `create_asset` e
as Gameplay Tags (`read_tags` / `write_tags`) já estão de pé; falta **escrever
Behavior Tree** e **componentes de Gameplay Effect**. O roteiro com o estado de
cada etapa está em *Onde estamos nessa lista*, no README. Ideia de ferramenta
nova que aparecer no meio vai para *Possíveis recursos futuros*, não para o
código.
