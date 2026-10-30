**Português** · [English](README.md)

# NodeScribe

NodeScribe é um plugin de editor da Unreal Engine que transforma texto simples
em nodes de Blueprint e faz o caminho de volta. Ele existe para que um
assistente de IA consiga montar e ler grafos gastando pouco: no formato
interno da Unreal, um único `Print String` ocupa ~1.000 tokens, e cada ligação
depende de um identificador de 32 caracteres bater entre dois pinos. Em
NodeScribe, o mesmo node é a linha `Print String (In String = "hello")`, de
~15 tokens, e o plugin resolve os nomes sozinho, sem as dezenas de consultas
que as ferramentas da Engine fazem para descobrir cada node. No uso, você copia
um grafo como texto, cola no chat, recebe o texto alterado e cola de volta com
um botão -- ou deixa o assistente fazer tudo direto pelo MCP. Além de grafos,
AnimGraphs e state machines, o mesmo texto lê e escreve as propriedades de
qualquer objeto, Blackboards, Behavior Trees, o Designer de um Widget
Blueprint, BlendSpaces e Gameplay Tags, e cria assets vazios.

## Sumário

- [Uso](#uso)
- [Com um assistente de IA](#com-um-assistente-de-ia)
- [Pelo MCP](#pelo-mcp)
- [Princípios](#princípios)
- [Estado](#estado)
- [Limitações conhecidas](#limitações-conhecidas)
- [Desenvolvimento](#desenvolvimento)

## Uso

Três botões na barra do editor de Blueprint, ao lado do **Compile**:

| botão | o que faz |
|---|---|
| **Paste** | lê o texto NodeScribe da área de transferência e cria os nodes no grafo aberto. `Ctrl+Z` desfaz. |
| **Copy Selected** | transcreve os nodes selecionados para texto. Não muda nada. |
| **Copy Whole Graph** | o mesmo, para o grafo inteiro. |

Eles aparecem em todo editor de Blueprint -- comum, Widget, Animation,
Gameplay Ability. Avisos e erros vão para o **Message Log**, canal
*NodeScribe*; a notificação tem um link que abre o log.

O formato do texto está em [`Docs/FORMAT.md`](Docs/FORMAT.md), em inglês.

## Com um assistente de IA

O assistente não adivinha o formato: ele precisa ler a especificação uma vez.
No **Claude Code**, cole isto no `CLAUDE.md` do seu projeto:

```markdown
## Grafos de Blueprint

- Todo grafo de Blueprint é entregue no formato NodeScribe, nunca em prosa nem
  em passos numerados: a resposta é colada no plugin como está.
- Ler `Plugins/NodeScribe/Docs/FORMAT.md` (o núcleo) antes de escrever ou
  interpretar um grafo. Um arquivo de `Docs/format/` só quando a tarefa chega
  no tópico dele: AnimGraph, Behavior Tree, Designer, os detalhes de uma
  leitura.
- Quando fizer falta, antes do grafo: o asset e o grafo de destino, e uma
  tabela das variáveis que ele precisa (nome | tipo | padrão | Instance
  Editable). Explicação vai depois do grafo, num bloco separado.
- Texto colado nesse formato vem do botão **Copy Whole Graph** do plugin.
```

Em outro assistente, cole o conteúdo de `Docs/FORMAT.md` no começo da
conversa, e um tópico de `Docs/format/` quando o trabalho chegar nele.

O ciclo fica: **Copy Whole Graph** → colar no chat → o assistente devolve o
texto alterado → **Paste**.

## Pelo MCP

O NodeScribe também se expõe como um toolset de MCP, e aí o assistente
escreve direto no grafo, sem você colar nada:

| ferramenta | para quê |
|---|---|
| `read_graph(graph)` | um grafo de Blueprint inteiro como texto |
| `write_graph(graph, text, replace)` | cria nodes a partir do texto; `after <linha da leitura>:` insere no meio de uma cadeia que já existe. `replace` apaga o grafo antes -- e recusa, sem mexer em nada, se a leitura do grafo atual perderia alguma coisa |
| `clear_graph(graph)` | esvazia o grafo e devolve como texto o que havia nele |
| `read_object(target, filter)` | propriedades de qualquer objeto, classe, CDO, ator ou asset. Filtro vazio = só o que difere do padrão |
| `write_object(target, text)` | aplica uma ficha. Uma lista de mudanças, não um estado final |
| `create_asset(path, parent, options)` | um asset vazio. Só dentro de `/Game/` |
| `write_blendspace(blend_space, text)` | os eixos e as amostras de um BlendSpace |
| `read_tags(filter)` / `write_tags(text, source)` | as Gameplay Tags declaradas / criar tags |
| `get_format_docs(topic)` | a especificação do formato: vazio dá o núcleo, um nome dá um tópico |
| `save_all_and_quit()` | salva e fecha o editor |

`read_object` e `write_object` mudam de forma conforme o alvo: um
**Blackboard** vira uma lista de `key Name : Type`, uma **Behavior Tree** vira
uma árvore indentada, um **Widget Blueprint** ganha, depois da ficha, a
seção `designer` com a hierarquia do Designer, e um **Input Mapping Context**,
a seção `mappings`, uma linha `Tecla -> Ação` por mapeamento. Num Blueprint de
ator, a mesma ficha cria e apaga componentes.

Medido: as ferramentas juntas têm ~2.600 tokens de descrição, contra ~18.000
do toolset de Blueprint da Engine, e um grafo de 15 nodes custa ~200 tokens
neste formato, contra ~15.000 no formato da Unreal.

### Ligando num projeto novo

O MCP depende de cinco peças, e quando falta uma as ferramentas simplesmente
não aparecem, sem aviso:

| peça | o que é | quem liga |
|---|---|---|
| **Python Script Plugin** | roda o `init_unreal.py` do plugin | o `.uplugin` do NodeScribe, sozinho |
| **ToolsetRegistry** | onde o toolset se registra | idem |
| **ModelContextProtocol** | o servidor MCP da Engine | idem |
| **`bAutoStartServer`** | o servidor da Engine nasce desligado | o NodeScribe, ao abrir o editor |
| **`.mcp.json`** | diz ao assistente onde o servidor está | idem |

As três primeiras são marcadas como `Optional`: quem só quer os botões não
precisa de servidor nenhum. As duas últimas o plugin acerta sozinho e conta no
Output Log o que fez; se o `.mcp.json` já tiver outra coisa no lugar, ele não
mexe e explica por quê, e **Tools → NodeScribe → Configure project MCP**
força a configuração.

Depois, **reinicie o assistente**: ferramentas de MCP só entram na
inicialização. Para conferir, com o editor aberto, peça `get_format_docs()`.
Se a ferramenta não existir, o Output Log, canal `LogPython`, diz qual peça
faltou.

## Princípios

**Quando não consegue decidir com segurança, ele não decide.** Nome de node
ambíguo vira um comentário vermelho no grafo, com os candidatos e nenhum
escolhido; asset não especificado vira um pino vazio, que impede a compilação
até você escolher. Um node plausível adivinhado é o pior resultado possível:
compila, roda e está errado.

**O texto é para gente ler.** A economia vem de eliminar a descoberta, não de
encurtar o texto, que é ~2% do custo. Trocar `Print String` por um código
opaco economizaria quase nada e tiraria a única defesa contra a IA ter escrito
outra coisa: você conseguir ler o que ela escreveu. Os piores bugs deste
projeto foram achados lendo -- um ramo `Critical` que imprimia `"Full"`, um
`New Key` desligado, três cadeias idênticas onde uma bastava.

**A volta avisa o que não cabe.** Quando o grafo tem algo que o texto não diz
-- uma cadeia de execução que se reencontra, um node sem nome estável, um pino
que vem de fora da seleção --, a leitura avisa em vez de sair com cara de
completa.

## Estado

Versão 0.4, Unreal 5.8.1.

- **Exercitado num projeto de verdade:** idas e voltas em grafos de interface
  e de gameplay -- eventos (override, custom, dispatcher, Input Action),
  Branch, For Each Loop, Switch, Cast, structs (Make/Break e pino dividido),
  subsystems, Create Widget com Expose on Spawn, ações assíncronas, maps e
  sets, variáveis próprias e de outro objeto, Bind/Unbind de dispatcher, e os
  três botões.
- **Compila mas nunca rodou:** Select, Call de dispatcher.
- **Idioma:** a interface, as mensagens, o código e o formato são em inglês.
  Este README é o principal; o [`README.md`](README.md) é a tradução dele.

O que falta fazer, em ordem, está no [ROADMAP.md](ROADMAP.md); as ideias ainda
não aceitas, em [Docs/ideas.md](Docs/ideas.md); o que cada versão mudou, em
[Docs/history.md](Docs/history.md).

## Limitações conhecidas

- Um node por linha; expressões aninhadas (`Print(Concat(a, b))`) não são
  aceitas. Quebre em duas linhas com `x = Concat(...)`.
- O layout é simples: execução da esquerda para a direita, uma coluna por
  passo, nodes de dado empilhados abaixo de quem os usa. Legível, não bonito.
- Timelines e variáveis locais ainda não têm forma.
- Nodes latentes (`Delay`, AbilityTasks) entram como qualquer função, mas as
  saídas extras exigem rótulos explícitos.
- As palavras-chave do formato são só em inglês. Textos com as palavras-chave
  antigas em português (`evento`, `verdadeiro:`, `variavel`) não colam mais.

## Desenvolvimento

Para mexer no plugin -- compilar, testar, o que não pode quebrar, como propor
uma ferramenta nova --, veja [Docs/development.md](Docs/development.md), em
inglês.
