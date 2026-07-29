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
| `SNodeScribePanel` | a janela, para editar o texto antes de inserir. |

Leitor e builder são espelhos: ao ensinar um tipo de node novo, os dois mudam.
