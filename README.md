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

Avisos e erros vão para o **Message Log**, no canal *NodeScribe*. O aviso
aparece como notificação; o link nela abre o log com os detalhes.

O formato do texto está em [`Docs/FORMATO.md`](Docs/FORMATO.md).

## Usando com um assistente de IA

O assistente não adivinha o formato — ele precisa ler a especificação uma vez.

Se você usa **Claude Code**, cole isto no `CLAUDE.md` do seu projeto e ele passa
a saber sozinho, em toda conversa:

```markdown
## Grafos de Blueprint

Ao entregar um grafo de Blueprint, use o formato do NodeScribe, especificado em
`Plugins/NodeScribe/Docs/FORMATO.md`. Leia esse arquivo antes de escrever ou
interpretar um grafo. Nunca descreva nodes em prosa.
```

Em qualquer outro assistente, cole o conteúdo de `Docs/FORMATO.md` no começo da
conversa. São ~150 linhas, uma vez por conversa.

O ciclo então fica: **Copiar grafo inteiro** → cola no chat → o assistente
devolve o texto alterado → **Colar**.

Um grafo de 15 nodes custa ~200 tokens nesse formato, contra ~15.000 no
formato de clipboard da Unreal.

O caminho de volta tem o mesmo cuidado do de ida: quando o grafo tem algo que
o texto não sabe dizer — uma cadeia de execução que reconverge, um node sem
nome estável, um pino que vem de fora da seleção — ele avisa em vez de emitir
um texto que parece completo e volta diferente.

## Princípio de projeto

Quando não dá para decidir com segurança, **não decide**.

- Nome de node ambíguo → comentário vermelho no grafo com os candidatos, nenhum escolhido.
- Asset não especificado → pino vazio, e a escolha fica visível esperando você.
- Node com vários caminhos de execução → a cadeia para, esperando um rótulo.

Um node plausível chutado é o pior resultado possível: compila, roda, e está
errado. Falhar em voz alta é sempre preferível.

## Estado

Versão 0.3. UE 5.8.1, build limpa sem avisos.

- **Exercitado num projeto real:** ida e volta em grafos de UI e de gameplay.
  Rodaram: eventos (override, custom, de dispatcher, de Input Action), Branch,
  For Each Loop, Switch, Cast, structs (Make/Break e pino dividido),
  subsistemas, Create Widget com Expose on Spawn, variáveis próprias e de
  outro objeto, e os três botões.
- **Compila mas nunca rodou:** Select, Call/Bind/Unbind de dispatcher.
- **Idioma:** interface e mensagens em português. O formato aceita palavras em
  PT e EN desde sempre (`evento`/`event`, `verdadeiro`/`true`).

## Limitações conhecidas

- Um node por linha; expressões aninhadas (`Print(Concat(a, b))`) não são suportadas.
  Quebre em duas linhas com `x = Concat(...)`.
- Layout é simples: execução da esquerda para a direita, uma coluna por passo;
  os nodes de dado descem em pilha embaixo do passo que os consome, recuando
  à esquerda conforme afundam na cadeia. Legível, não bonito.
- Timeline e variáveis locais ainda não têm forma.
- Nodes assíncronos/latentes (`Delay`, AbilityTasks) entram como qualquer função,
  mas as saídas extras exigem rótulos explícitos.
