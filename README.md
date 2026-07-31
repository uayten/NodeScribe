# NodeScribe

Cola uma lista de nodes escrita em texto e vira nodes de verdade no grafo do
Blueprint — posicionados e ligados.

## Para que ele existe

Gastar o mínimo de tokens na conversa entre a IA e a Unreal.

Não é sobre ser mais expressivo que as ferramentas do motor — não é. É sobre um
grafo inteiro caber numa mensagem, em vez de custar dezenas de idas e voltas.

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

Aparecem em qualquer editor de Blueprint — comum, Widget, Animation, Gameplay
Ability — e ficam invisíveis nos editores que não são de Blueprint.

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

## Pelo MCP

Com os plugins **ToolsetRegistry** e **Python Script Plugin** ligados, o
NodeScribe se expõe como toolset MCP: `write_graph`, `read_graph` e
`get_format_docs`.

O motivo é custo, não capacidade. Montar um grafo pelas ferramentas
convencionais gasta a maior parte dos tokens *descobrindo* identificadores de
node — uma chamada por tipo, cada uma devolvendo dezenas de resultados. O
catálogo do NodeScribe resolve os nomes localmente, então um grafo inteiro cabe
numa chamada e algumas centenas de tokens.

As três ferramentas juntas ocupam ~300 tokens de descrição, contra ~18.000 do
toolset de Blueprint da Engine.

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

## Por que o texto é legível

O formato economiza tokens **eliminando descoberta**, não encurtando texto.

O gasto de montar grafo por ferramenta convencional está em perguntar o nome
exato de cada node — uma chamada por tipo, cada uma devolvendo dezenas de
identificadores. O catálogo do NodeScribe resolve `Print String` localmente,
com busca tolerante. É daí que vem a diferença de ordem de grandeza.

O tamanho do texto em si é ~2% do custo. Trocar `Print String (In String =
"olá")` por um identificador opaco economizaria uns poucos tokens por node — e
custaria a única coisa que faz o formato ser confiável: **você conseguir ler o
que a IA escreveu**.

Os piores erros deste projeto foram achados assim: um ramo `Critical` que
imprimia `"Full"`, um `New Key` que tinha se desligado, três cadeias idênticas
onde bastava uma. Nenhum apareceria olhando o grafo, e nenhum apareceria num
formato que só a máquina lê.

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

### Em que ponto estamos

Grafo é o que existe. **Ficha de propriedades é o que está sendo construído** —
a especificação está em *Possíveis recursos futuros*, abaixo, e é a próxima
coisa a codar.

Uma frente por vez, e madura antes da seguinte. Ideia de ferramenta nova que
aparecer no meio do caminho vai para *Possíveis recursos futuros*, não para o
código: o plugin só vale se cada peça for confiável, e uma peça só fica
confiável com uso repetido.

Dentro da ficha, a ordem é **`read_object` primeiro, sozinho**, e `write_object`
só depois que o formato de leitura tiver sobrevivido a uso real. Ler não
estraga asset nenhum, então dá para testar à vontade enquanto o formato ainda
está mudando de ideia — e é onde está quase toda a economia de token. Escrever
num formato que ainda vai mudar é como se arrepender caro.

## Desenvolvimento

Esta seção existe para quem — pessoa ou agente de IA — for mexer no plugin sem
ter acompanhado a conversa em que ele foi desenhado.

Leia antes: [`CLAUDE.md`](CLAUDE.md) para a estrutura dos arquivos, e
[`Docs/FORMATO.md`](Docs/FORMATO.md) para o formato do texto.

### O ciclo fecha sozinho

A Unreal segura os binários do plugin enquanto está aberta, então recompilar
exige fechar o editor. **É para isso que `save_all_and_quit` existe**: com ela,
um agente fecha o editor, compila, reabre e testa sem ninguém clicar em nada.
O ciclo inteiro é automatizável.

1. Editar o C++.
2. `save_all_and_quit` pelo MCP. Recusa se houver Play In Editor rodando, e a
   conexão MCP cai logo depois — é esperado.
3. Compilar:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BossRushEditor Win64 Development -Project="C:\Unreal Projects\BossRush\BossRush.uproject" -WaitMutex
```

4. Reabrir:

```powershell
Start-Process "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Unreal Projects\BossRush\BossRush.uproject"
```

5. Esperar o editor subir. O MCP só responde depois disso.
6. Testar pelo MCP, ler o Message Log no canal *NodeScribe*, repetir.

Três coisas que custam tempo quando esquecidas:

- **Os caminhos acima são desta máquina** — Engine em `E:`, projeto em `C:`.
  Confira antes de rodar em outra.
- **Confira a saída do build, não só o código de saída.** `Build.bat` sai com 0
  em situações em que não compilou nada. Procure `Result: Succeeded`.
- **Live Coding não serve aqui.** Ele recompila o corpo de uma função que já
  existe, sem fechar o editor. `UFUNCTION` ou `UCLASS` nova é reflection nova,
  e reflection nova exige restart. Como quase toda mudança neste plugin mexe em
  superfície exposta, o caminho normal é o ciclo completo acima.

### Testar

O teste que mais pega defeito, e é de graça: **ida e volta**. Ler um grafo,
colar o texto num Blueprint vazio, ler de novo, comparar os dois textos.
Leitor e escritor são espelhos por projeto — quando o texto não fecha, um dos
dois está mentindo, e o diff diz qual.

Não há testes automatizados ainda. Quando houver, o molde é
`ToolsetRegistry/Source/ToolsetRegistry/Private/Tests/ToolsetLibraryTest.cpp`
na Engine (`BEGIN_DEFINE_SPEC`), e eles rodam sem abrir a interface:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Unreal Projects\BossRush\BossRush.uproject" -ExecCmds="Automation RunTests NodeScribe; Quit" -unattended -nopause -nosplash -NullRHI
```

Vale o investimento na hora em que a ficha começar a mexer em propriedade de
verdade: ali um erro escreve no asset, e ida e volta manual não cobre isso.

### O que não pode quebrar

Estes quatro pontos são o projeto. Mudança que os contrarie está errada mesmo
que funcione:

1. **Quando não dá para decidir com segurança, não decide.** Ambíguo vira
   comentário vermelho; asset não especificado vira pino vazio. Um node
   plausível chutado compila, roda e está errado — é o pior resultado possível.
2. **Leitor e escritor são espelhos.** Ao ensinar um tipo novo, os dois mudam,
   no mesmo commit.
3. **Legibilidade não se troca por bytes.** O texto é ~2% do custo; encurtá-lo
   economiza ~1% e destrói a única defesa contra a IA ter escrito outra coisa.
4. **Ferramenta nova é imposto fixo.** A descrição de cada ferramenta MCP fica
   no prompt a cada mensagem, sendo ela chamada ou não. Prefira um parâmetro
   novo numa ferramenta existente a uma ferramenta nova.

### Propondo uma ferramenta nova

Vai para *Possíveis recursos futuros* antes de virar código, e a proposta
precisa responder quatro coisas — as mesmas que a ficha responde:

- **Onde o token vaza hoje?** Com números, olhando o código da ferramenta
  nativa, não de memória.
- **Qual o formato comprimido?** Uma linha por item, nome que aparece na tela,
  legível por gente.
- **Quanto custa de superfície?** Quantas ferramentas MCP novas, e por que não
  dá para ser menos.
- **O que ela deixa desligar?** Se a resposta for "nada", ela não economiza —
  só adiciona.

## Possíveis recursos futuros

### Ficha: propriedades de objeto como texto

Grafo é metade do trabalho. A outra metade é ler e mexer em propriedade —
e ali o custo hoje é pior do que era no grafo.

O `EditorToolset` da Engine resolve isso com `list_properties`, que devolve o
JSON Schema inteiro da classe: tipo, descrição e valor de fábrica de cada
propriedade, herança toda incluída. Para um `Character` são ~250 propriedades,
ordem de **10.000 tokens** — e isso é só o formato, nenhum valor. Os valores
exigem uma segunda chamada.

A ideia é uma **ficha**: uma linha por propriedade, e só o que **difere do
padrão**.

```
ficha BP_Golem (Character)
# herda: BP_BossBase < Character < Pawn < Actor
variavel Vida Maxima : Float = 500
Mesh : SkeletalMeshComponent
  Skeletal Mesh = SKM_Golem
  Relative Location = (0, 0, -90)          # padrao (0, 0, 0)
CharacterMovement : CharacterMovementComponent
  Max Walk Speed = 250                     # padrao 600
~ 431 propriedades no padrao
```

É a mesma jogada do catálogo, aplicada a propriedade: **não mandar o que dá
para resolver do lado da engine**. `Gravity Scale = 1.0` não carrega
informação nenhuma — é o valor de fábrica, e o plugin sabe disso localmente.
Num CDO típico, 95% das propriedades estão no padrão.

O comentário `# padrao X` só aparece onde houve alteração. Custa zero nas
linhas não alteradas, e põe o desvio — a única coisa que vale conferir a olho
— em destaque. A contagem no fim existe para o silêncio ser explícito: sem
ela, "não apareceu" fica ambíguo entre *está no padrão* e *o plugin não sabe
ler*.

**Não há passo separado de listar o esquema.** Procurando uma propriedade, o
filtro vai na mesma chamada e a resposta já vem com os valores:

```
ficha CharacterMovement de BP_Golem  ~ "walk" (4 de 218)
Max Walk Speed : Float = 250             # padrao 600
Max Walk Speed Crouched : Float = 300
Walkable Floor Angle : Float = 44.765
Ignore Base Rotation on Base : Boolean = false
```

Um turno em vez de dois, quatro linhas em vez de oitocentas.

A volta é o mesmo texto sem os comentários, com a disciplina de sempre: linha
que não resolve não é adivinhada, e `?` num valor significa "não decidi" e é
pulado de propósito.

**Implementação:** `NodeScribeObjectReader` e `NodeScribeObjectWriter` em
espelho, com `NodeScribePropertyText` compartilhado — onde o espelho não pode
torcer. `TFieldIterator` percorre; `Identical_InContainer` contra
`GetArchetype()` detecta o desvio (deliberado: é a mesma referência da setinha
de *Reset to Default*, então o que a ficha chama de alterado é o que aparece
alterado na tela); `ExportTextItem_InContainer` / `ImportText_InContainer`
cobrem struct, enum, array e referência sem código por tipo. O parser e o
`Normalize` do catálogo servem sem mudança. Variável de Blueprint com valor
lido do CDO já existe no leitor de grafo.

**Filtro de visibilidade — decidido:** entra a propriedade que aparece no
painel Details (`CPF_Edit`) **ou** que tem node de Get no Blueprint
(`CPF_BlueprintVisible`). Sai a transiente (`CPF_Transient`), que nem é salva
em disco.

A maioria tem os dois carimbos, mas não todas, e as exceções são justamente as
que importam: `Velocity` não aparece no painel — não faz sentido digitar a
velocidade atual de um personagem — mas é lida no grafo o tempo todo. Um
filtro só de Details responderia "não achei" para ela. Os dois carimbos juntos
cobrem as duas metades das perguntas que se faz sobre um Blueprint.

**Superfície:** duas ferramentas, não seis. `read_object` e `write_object`;
`tipos <` entra como sintaxe de filtro, e a doc nova vira um parâmetro de
`get_format_docs`, não uma ferramenta a mais. Descrição de ferramenta é
imposto cobrado em toda mensagem.

### O resto do mapa

A meta é desligar o `EditorToolset` do `.uproject`. Ele expõe **224
ferramentas**; as descrições ficam no prompt a cada mensagem, sendo elas
chamadas ou não — ~18.000 tokens fixos. Otimizar uma de 224 não economiza
nada enquanto as outras 223 continuarem lá. A economia só é realizada quando
o pacote sai inteiro.

| bloco | ferramentas | destino |
|---|---|---|
| `object` | 6 | a ficha, acima |
| `scene` + `actor` | 37 | consulta de mundo com projeção de campos |
| `asset` | 21 | idem, mesma ferramenta |
| `blueprint` | 53 | já é território daqui, mais CRUD de variável e função |
| material, meshes, tabelas, textura | 107 | ← |

Essas 107 da cauda longa são edição por tipo de asset — e um parâmetro de
Material Instance é uma propriedade, uma linha de Data Table é uma
propriedade, um build setting de mesh é uma propriedade. Se a ficha for
genérica de verdade — e ela é, porque `TFieldIterator` não sabe o que é um
material —, boa parte delas some sem ferramenta nova nenhuma. É por isso que
a ficha vem primeiro: os 6 tools que ela substitui são o mecanismo que torna
outros ~107 dispensáveis.

Os números de token acima são estimativa a partir da contagem de propriedades
e do formato que o `StructToJsonSchema` produz, não medição.

### Enviar só o que mudou

Hoje, editar um node num grafo de 40 custa o grafo inteiro em cada direção:
ler tudo, devolver tudo, reescrever tudo.

A ideia é `read_graph` devolver um identificador do estado junto do texto, e
`write_graph` aceitar só as linhas alteradas mais esse identificador. Se o
grafo tiver mudado no meio do caminho, a escrita é recusada em vez de
sobrescrever cego.

**Por que vale:** é a única compressão que escala sem custar legibilidade. As
outras — encurtar nomes, usar identificadores numéricos — economizam ~1% do
custo total (o texto já é ~2% dele; o resto era descoberta, e essa já foi
eliminada) e destroem a capacidade de conferir o que a IA escreveu. Mandar
menos linhas economiza ordens de grandeza, e as poucas que trafegam continuam
sendo texto legível.

O ganho aparece em grafos grandes e em edições pequenas — que é exatamente o
caso comum depois que o grafo existe.

## Limitações conhecidas

- Um node por linha; expressões aninhadas (`Print(Concat(a, b))`) não são suportadas.
  Quebre em duas linhas com `x = Concat(...)`.
- Layout é simples: execução da esquerda para a direita, uma coluna por passo;
  os nodes de dado descem em pilha embaixo do passo que os consome, recuando
  à esquerda conforme afundam na cadeia. Legível, não bonito.
- Timeline e variáveis locais ainda não têm forma.
- Nodes assíncronos/latentes (`Delay`, AbilityTasks) entram como qualquer função,
  mas as saídas extras exigem rótulos explícitos.
