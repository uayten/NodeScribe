# NodeScribe

Cola uma lista de nodes escrita em texto e vira nodes de verdade no grafo do
Blueprint — posicionados e ligados.

## O problema que ele resolve

Pedir a um assistente de IA para *descrever* nodes é rápido e barato. Pedir para
ele **colocar** os nodes no grafo é lento e caro, porque o formato interno da
Unreal é enorme:

| | tamanho aproximado |
|---|---|
| `Print String (In String = "olá")` | ~15 tokens |
| o mesmo node no formato de clipboard da Unreal | ~1.000 tokens |

Um grafo de 20 nodes passa de 20.000 tokens nesse formato — e cada ligação
depende de uma sequência de 32 caracteres bater exatamente entre dois pinos.
É trabalho de script, não de modelo de linguagem.

O NodeScribe faz essa expansão localmente, de graça e sem errar um identificador.

## Uso

1. **Janela → Ferramentas → NodeScribe**
2. Confira o **Destino** (qual Blueprint e qual grafo). Se acabou de abrir um
   Blueprint, clique em **Atualizar**.
3. Cole o texto.
4. **Inserir no grafo** — os nodes aparecem. `Ctrl+Z` desfaz.
   Ou **Copiar para clipboard** — não toca no asset; você cola com `Ctrl+V` onde quiser.

O formato do texto está em [`Docs/FORMATO.md`](Docs/FORMATO.md).

## Princípio de projeto

Quando não dá para decidir com segurança, **não decide**.

- Nome de node ambíguo → comentário vermelho no grafo com os candidatos, nenhum escolhido.
- Asset não especificado → pino vazio, e o Blueprint não compila até você escolher.
- Node com vários caminhos de execução → a cadeia para, esperando um rótulo.

Um node plausível chutado é o pior resultado possível: compila, roda, e está
errado. Falhar em voz alta é sempre preferível.

## Estado

Versão 0.1. Funciona no sentido texto → nodes.

O caminho inverso (grafo → texto, para editar algo que já existe) ainda não
existe — hoje, para pedir ajuda com um grafo pronto, use um print ou selecione
alguns nodes e cole o `Ctrl+C` no chat.

## Limitações conhecidas

- Um node por linha; expressões aninhadas (`Print(Concat(a, b))`) não são suportadas.
  Quebre em duas linhas com `x = Concat(...)`.
- Layout é simples: cadeia de execução da esquerda para a direita, ramos empilhados.
  Legível, não bonito.
- Delegates e event dispatchers ainda não têm forma própria.
- Nodes assíncronos/latentes (`Delay`, AbilityTasks) entram como qualquer função,
  mas as saídas extras exigem rótulos explícitos.
