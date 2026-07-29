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

Três botões na barra do editor de Blueprint, ao lado de **Compile**:

| botão | o que faz |
|---|---|
| **Colar** | lê o texto do NodeScribe que está no clipboard e cria os nodes no grafo aberto. `Ctrl+Z` desfaz. |
| **Copiar selecionado** | transcreve os nodes selecionados para texto. Não altera nada. |
| **Copiar grafo inteiro** | idem, para o grafo todo. |

Aparecem no editor de Blueprint comum, no de Widget e no de Animation.

Avisos e erros vão para o **Message Log**, no canal *NodeScribe* — ele abre
sozinho quando algo precisa da sua atenção e fica quieto quando não precisa.

Para editar o texto antes de inserir, a janela continua lá:
**Janela → Ferramentas → NodeScribe**.

O formato do texto está em [`Docs/FORMATO.md`](Docs/FORMATO.md).

## O ciclo completo

`Copiar grafo inteiro` → cola no chat → o assistente lê e devolve o texto
alterado → `Colar`. Um grafo de 15 nodes custa ~200 tokens nesse formato,
contra ~15.000 no formato de clipboard da Unreal.

O caminho de volta tem o mesmo cuidado do de ida: quando o grafo tem algo que
o texto não sabe dizer — uma cadeia de execução que reconverge, um node sem
nome estável, um pino que vem de fora da seleção — ele avisa em vez de emitir
um texto que parece completo e volta diferente.

## Princípio de projeto

Quando não dá para decidir com segurança, **não decide**.

- Nome de node ambíguo → comentário vermelho no grafo com os candidatos, nenhum escolhido.
- Asset não especificado → pino vazio, e o Blueprint não compila até você escolher.
- Node com vários caminhos de execução → a cadeia para, esperando um rótulo.

Um node plausível chutado é o pior resultado possível: compila, roda, e está
errado. Falhar em voz alta é sempre preferível.

## Estado

Versão 0.2. UE 5.8.1, build limpa sem avisos.

- **Funciona, no mínimo:** `evento BeginPlay` + `Print String` viraram dois
  nodes ligados, com a string preenchida.
- **Pouco testado:** só esse caminho foi exercitado de ponta a ponta. Ramos,
  casts, macros, variáveis, os três botões novos da barra e o caminho de volta
  (grafo → texto) compilam, mas nunca rodaram.
- **Idioma:** interface e mensagens em português. O formato aceita palavras em
  PT e EN desde sempre (`evento`/`event`, `verdadeiro`/`true`).

## Limitações conhecidas

- Um node por linha; expressões aninhadas (`Print(Concat(a, b))`) não são suportadas.
  Quebre em duas linhas com `x = Concat(...)`.
- Layout é simples: cadeia de execução da esquerda para a direita, ramos empilhados.
  Legível, não bonito.
- Delegates e event dispatchers ainda não têm forma própria. Na leitura o
  plugin avisa; na escrita, um `evento X` desses vira Custom Event solto.
- Nodes assíncronos/latentes (`Delay`, AbilityTasks) entram como qualquer função,
  mas as saídas extras exigem rótulos explícitos.
