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

Além dos botões, o NodeScribe se expõe como toolset MCP — `write_graph`,
`read_graph`, `read_object`, `write_object` e companhia. Aí o assistente
escreve no grafo direto, em vez de te mandar texto para colar.

### Ligando num projeto novo

A cadeia tem quatro elos, e o assistente não avisa quando um falta: as
ferramentas simplesmente não aparecem.

| elo | o que é | quem liga |
|---|---|---|
| **Python Script Plugin** | roda o `init_unreal.py` do plugin | o `.uplugin` do NodeScribe, sozinho |
| **ToolsetRegistry** | onde o toolset se registra | idem |
| **ModelContextProtocol** | o servidor MCP da Engine, que expõe o registro | idem |
| **`.mcp.json`** | diz ao assistente onde o servidor está | **você**, uma vez por projeto |

Os três primeiros são dependências declaradas no `NodeScribe.uplugin` e entram
sozinhos ao instalar o plugin. Estão marcados como `Optional`: quem só quer os
botões **Colar** e **Copiar** não é obrigado a subir servidor nenhum.

O quarto é do projeto, não do plugin — um `.mcp.json` na raiz:

```json
{
  "mcpServers": {
    "unreal-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:8000/mcp"
    }
  }
}
```

Depois disso, **reinicie o assistente**: as ferramentas MCP só entram no
contexto na inicialização.

### Conferindo que pegou

Com o editor aberto, peça `get_format_docs()`. Se a ferramenta não existir, o
elo que falta está acima dela na tabela — e o `init_unreal.py` do plugin
reclama no Output Log, canal `LogPython`, quando não acha o `toolset_registry`.

### Por que vale a pena

O motivo é custo, não capacidade. Montar um grafo pelas ferramentas
convencionais gasta a maior parte dos tokens *descobrindo* identificadores de
node — uma chamada por tipo, cada uma devolvendo dezenas de resultados. O
catálogo do NodeScribe resolve os nomes localmente, então um grafo inteiro cabe
numa chamada e algumas centenas de tokens.

As três ferramentas juntas ocupam ~300 tokens de descrição, contra ~18.000 do
toolset de Blueprint da Engine — cobrados quando o assistente pede a descrição
daquele toolset, não a cada mensagem. O servidor MCP da Engine usa descoberta
preguiçosa; quem contar isso como custo fixo por mensagem vai superestimar
bastante a economia.

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

Versão 0.4. UE 5.8.1, build limpa sem avisos.

- **Exercitado num projeto real:** ida e volta em grafos de UI e de gameplay.
  Rodaram: eventos (override, custom, de dispatcher, de Input Action), Branch,
  For Each Loop, Switch, Cast, structs (Make/Break e pino dividido),
  subsistemas, Create Widget com Expose on Spawn, ação assíncrona
  (`UBlueprintAsyncActionBase`), mapa e conjunto, variáveis próprias e de outro
  objeto, e os três botões.
- **Compila mas nunca rodou:** Select, Call/Bind/Unbind de dispatcher.
- **Idioma:** interface e mensagens em português. O formato aceita palavras em
  PT e EN desde sempre (`evento`/`event`, `verdadeiro`/`true`).

### O que a 0.4 mudou: a leitura parar de mentir

A 0.3 foi boa o bastante para achar bugs reais numa revisão longa de UI. Onde
ela falhou foi pior que falhar: em dois pontos o texto era **plausível e
errado**, e isso virou afirmação errada em mensagem de commit.

Três eram defeito de correção, não de diagnóstico:

- **Campo de struct sumia do pino.** `$x` puro saía quando o Break tinha um
  campo visível só — e os pinos de um Break são os campos *marcados* no painel
  dele. O Break recriado ao colar nasce com todos marcados, então `$x` entrava
  no primeiro campo da struct, que podia ser outro. Agora Break sempre nomeia o
  campo.
- **Nome de tela com parênteses não voltava.** `SetText (Text)` está em todo
  grafo de UI; o parser corta a linha no primeiro `(` e aquilo voltava como um
  node `SetText` recebendo um argumento `Text`. Nome com parênteses agora sai
  qualificado (`TextBlock.SetText`).
- **`then` de um node assíncrono saía como `True:`.** O apelido existe para o
  Branch, e fora dele inventava uma condição que não existe — quem lia entendia
  que havia um teste, e o ramo seguinte virava o `False` dele.

E o resto do que a revisão pediu: âncora nas duas pontas de uma reconvergência,
marca de conversão implícita de tipo, nome de cada node órfão, `Mapa de X para
Y` e `Conjunto de X`, ação assíncrona pelos dois lados, `refPath` no cabeçalho,
e valor apagado saindo como `""` em vez de sumir.

### Substituir o grafo, e até onde a guarda enxerga

`write_graph(graph, text, substituir=True)` apaga o grafo antes de escrever, em
vez de acrescentar. Existe porque iterar num grafo pelo modo aditivo acumula
node, e apagar na mão a cada volta é o atrito que faz alguém parar de usar a
ferramenta.

**A guarda:** antes de apagar, o plugin lê o grafo atual. Se a leitura tiver
qualquer aviso de "isso não volta igual", ou node de dado que ninguém consome, a
substituição é recusada e **nada** é alterado. Apagar a partir de um texto que
perdeu alguma coisa destrói justamente o que ninguém tem escrito.

Não há modo forçado, de propósito. Quem quer mesmo limpar seleciona tudo no
grafo e aperta Delete: é um gesto humano, visível, e com Ctrl+Z do lado. O
apagar e o escrever do plugin ficam na mesma transação, então o Ctrl+Z também
desfaz os dois de uma vez.

**Onde a guarda não enxerga, e isso precisa estar escrito.** Ela recusa o que o
leitor *sabe* que perde. Não cobre o que o leitor não sabe que perdeu:

- **pureza de um node** — um `Cast` puro volta impuro, porque o texto não diz
  qual dos dois é;
- **node desabilitado** que esteja no meio de uma cadeia — volta habilitado;
- **layout** — posição, tamanho de caixa de comentário, reroute.

Nenhum desses muda o que o grafo faz, fora o caso da pureza. Mas a lista existe
porque "a guarda passou" não é prova de que o texto reconstrói o grafo — é prova
de que o leitor não viu problema.

A versão forte disso seria o ponto fixo em memória: montar o texto lido num
grafo transiente, ler de volta e comparar, que é exatamente o que
`Testes/rodar_testes.py` faz por fora. Fica anotado como o próximo passo se a
substituição virar rotina.

### O que a 0.4 mudou: o resto

**Teste de ida e volta automatizado** (`Testes/rodar_testes.py`), que na
primeira execução já pegou um exemplo do `FORMATO.md` que não funcionava:
`Break Vector (In Vec = ...)`. O pino de entrada de um Break se chama como a
struct, então o exemplo certo é `Break Vector ($posicao)`.

**O catálogo agora é invalidado quando um Blueprint compila.** Ele é montado uma
vez por sessão, e `Invalidate()` existia sem ninguém chamar: criar uma função
num Blueprint e chamá-la de *outro*, na mesma sessão, dava "não achei nenhum
node chamado X" para algo que existe.

**Três coisas que faziam o texto ensinar o nome errado:**

- `100.000000` num pino virou `100.0`. A ficha já formatava assim, com o
  comentário explicando por quê; o leitor de grafo não — era a mesma decisão
  aplicada em metade do plugin.
- `BP_Golem_C` virou `BP_Golem`. O sufixo é da compilação, não aparece em lugar
  nenhum da interface, e a busca aceita as duas formas.
- `TargetMap` virou `Target Map`, e `bIsChecked` virou `Is Checked` — o nome do
  parâmetro em C++ dava lugar ao que a tela mostra. **Com uma guarda:** nome com
  sigla ou número sai cru, porque `NameToDisplayString` decide onde cabe espaço
  pela troca de caixa e erra em sigla — `JSL4UControllerInfo` saía como
  `JSL4UController Info`. Isso só apareceu relendo um grafo de verdade depois da
  mudança; o teste de ponto fixo passava, porque a busca normaliza as duas
  formas.

### `variavel X : Actor` criava uma struct

A ida e volta da 0.4 esbarrou nisto: `Mapa de Int64 para Actor` voltou como
`Mapa de Int64 para TypedElementActorTag`.

A Engine declara `FTypedElementActorTag` como
`USTRUCT(meta = (DisplayName = "Actor"))`, em
`Elements/Columns/TypedElementCompatibilityColumns.h`. O
`ResolvePinTypeFromNameInternal` procura struct antes de classe, e a busca de
struct aceita nome de tela — então "Actor" casava com ela primeiro. Era anterior
à 0.4, valia para qualquer declaração, e `Actor` é dos nomes de tipo mais
escritos que existem.

A regra nova é estreita: **struct que casou só pelo nome de tela perde para uma
classe de nome exato.** `Vector` e `TimerHandle` casam pelo nome interno da
struct e continuam ganhando; a busca de classe só conhece nome interno (e o `_C`
de Blueprint), então não abre uma segunda porta de nome de tela.

Conferido por regressão: `Vector`, `Timer Handle`, `Transform`, `Rotator`,
`Linear Color`, `Array de Vector`, `Mapa de Name para Vector` e um enum
continuam resolvendo como antes; `Actor` e `Pawn` agora dão a classe.

### Em que ponto estamos

Funcionam hoje: **grafo** (`read_graph` / `write_graph`), **ficha de objeto
único** (`read_object`, modo padrão e filtrado) e **Gameplay Tags**
(`read_tags` / `write_tags`).

A frente ativa é **ler a IA de um inimigo inteira** — o Golem do BossRush é o
caso real que guia o desenvolvimento. O roteiro está logo abaixo.

Uma frente por vez, e madura antes da seguinte. Ideia de ferramenta nova que
aparecer no meio do caminho vai para *Possíveis recursos futuros*, não para o
código: o plugin só vale se cada peça for confiável, e uma peça só fica
confiável com uso repetido.

Em toda frente, **leitura primeiro, sozinha.** Ler não estraga asset nenhum,
então dá para testar à vontade enquanto o formato ainda está mudando de ideia
— e é onde está quase toda a economia de token. Escrever num formato que
ainda vai mudar é se arrepender caro.

### Roteiro: ler a IA do inimigo

Os assets do caso real, em `Content/BossRush/`: `IA/Golem/Behavior/`
(`BT_Golem`, `BB_Golem`, `AIC_Golem`, `BTService_LocalizarJogador`,
`BTTask_Patrulhar`) e `GAS/Habilidades/Golem/` (quatro `GA_`).

**Uma suposição que se provou falsa e economiza trabalho:** BT e Blackboard
**não** dependem de componentes na ficha. Um node de BT é um UObject com
propriedades e nada mais. Componentes só fazem falta no `AIC_Golem` e para
afinar o movimento do Golem, e por isso desceram na fila.

#### Etapa 0 — Habilidades: pronto, sem código novo

`read_object` já entrega um `GA_` inteiro. GameplayAbility não tem componente,
então a lacuna da ficha não morde ali:

```
ficha GA_ChuvaDePedras (GameplayAbility)
Classe Pedra = /Game/BossRush/GAS/Habilidades/Golem/ChuvaDePedras/BP_Pedra.BP_Pedra_C
Quantidade De Pedras = 8
Raio Da Área = 1500.0
Intervalo Entre Pedras = 0.25
~ 22 propriedades no padrao
```

#### Etapa 1 — Chaves do Blackboard — **pronta**

Veio primeiro porque decorator de BT referencia chave **por nome**: sem as
chaves, a árvore sairia cheia de nome solto sem sentido.

Antes falhava com `nao sei escrever o valor de: Keys`.
`UBlackboardData::Keys` é um `TArray<FBlackboardEntry>`, e cada entrada guarda
um `KeyType` que é **subobjeto instanciado** — é isso que o formatador
genérico não abre.

```
blackboard BB_Golem
chave Jogador : Object (Actor)
chave Distância do Ataque : Float
chave Pode usar Laser? : Bool
```

O detalhe que qualifica o tipo (`BaseClass` de Object, `EnumType` de Enum) sai
**por reflexão**, não por um caso para cada subclasse conhecida: são dez tipos
na Engine e qualquer projeto pode escrever o seu, e um `switch` de casts
responderia vazio para a chave que o próprio projeto criou, sem dizer que
estava ignorando algo. Pelo mesmo motivo, o `Build.cs` ganhou `AIModule` mas só
dois headers entram por include.

O nome do tipo sai da classe do `KeyType`, sem o prefixo
`BlackboardKeyType_`. `UBlackboardData::Parent` vira uma linha de cabeçalho
quando existir.

#### Etapa 2 — Árvore do BT — **pronta**

A árvore inteira numa chamada. Antes, ver o que a IA faz exigia seguir ponteiro
node a node, e **o `AIModuleToolset` nem está ligado neste projeto** — não
havia alternativa nativa.

```
arvore BT_Golem  (blackboard BB_Golem)
Selector
  Sequence
    Move To (Blackboard Key = Jogador)
  Sequence
    Patrulhar
    Wait (Wait Time = 1.00)
```

Percurso: `UBehaviorTree::RootNode` e `RootDecorators`, depois
`UBTCompositeNode::Children` — cada `FBTCompositeChild` tem os seus
`Decorators` —, mais `Services` do composite e da task. Há guarda contra ciclo,
que BT não tem por construção mas asset corrompido pode.

**Decorator e service saem como linha com palavra-chave** (`decorador X`,
`servico Y`), e não como rótulo indentado terminado em `:` como o esboço antigo
previa. Rótulo funciona para ramo de grafo, onde cada ramo é um caminho; aqui
um node pode ter vários decorators, e aninhar cada um criaria níveis de
indentação que não existem na árvore. Decorator sai junto com o filho que ele
guarda, que é onde o editor mostra.

Os parâmetros saem pelo formatador da ficha, comparados com o CDO da classe do
node — é por isso que a ficha veio antes. `Move To` com raio de fábrica não
ganha parâmetro nenhum.

Duas formas precisaram de tratamento próprio:

- **Seletor de chave de blackboard** vira só o nome da chave. A forma canônica
  traz junto a lista de tipos aceitos, que enche a linha e esconde qual é a
  chave.
- **`FValueOrBBKey_*`** (`Wait Time`, `Acceptable Radius`) tem `ToString()`
  próprio, que dá o número quando o valor é fixo e o nome da chave quando está
  amarrado ao blackboard. **A 5.8 aposentou `FAIDataProviderValue` em favor
  dessa família** — mirar só na antiga compila, roda e devolve
  `(DefaultValue=1.000000)`. Foi o que aconteceu na primeira tentativa.

`GetNodeName()` resolve os nodes da Engine, mas em classe de Blueprint só tira
o `_C`: `BTTask_Patrulhar` aparecia com o prefixo técnico do lado de um
`Move To` limpo. O prefixo é removido na leitura.

**BT é o melhor encaixe que este formato já teve:** uma BT é literalmente uma
árvore e não reconverge, então a perda que o `FORMATO.md` declara para grafo
("cadeia de execução que reconverge, essa volta se perde") não existe aqui.

#### Etapa 3 — Componentes na ficha — **pronta**

```
ficha BP_Golem (Character)
# herda: Character < Pawn < Actor
variavel Facção : GameplayTag = '(TagName="Facção.Inimigos")'
Auto Possess AI = PlacedInWorldOrSpawned # padrao PlacedInWorld
CollisionCylinder : CapsuleComponent
  Capsule Half Height = 98.0 # padrao 88.0
  Capsule Radius = 80.0      # padrao 34.0
CharMoveComp : CharacterMovementComponent
  Max Walk Speed = 300.0 # padrao 600.0
~ 620 propriedades no padrao
# [nota]: mudou, mas o valor e' longo demais para a visao geral -- peca pelo
#   nome para ver: CollisionCylinder.Body Instance, CharacterMesh0.Body Instance
```

`read_object(BP_Golem, "walk")` agora responde `11 de 637`, com
`Max Walk Speed : Float = 300.0 # padrao 600.0`. Antes respondia `0 de 78`.

**Componente vem de dois lugares e é preciso ler os dois:** o que veio do
construtor em C++ vive no CDO (`AActor::GetComponents()`), e o que foi
arrastado no editor vive como template no `SimpleConstructionScript`. A busca
sobe a cadeia de Blueprints pais, porque componente que o pai criou também é
do filho. Ler só uma das origens esconde metade dos componentes sem avisar.

**Struct longa demais abre e mostra só o membro que mudou.** É o princípio da
ficha um nível abaixo. `Body Instance` de uma cápsula sai plano com a tabela
inteira de resposta de colisão — ~2.000 caracteres, mais outros ~2.000 do valor
de fábrica ao lado, sozinho maior que toda a ficha do Golem. Aberto, são três
linhas que dizem a coisa:

```
  Body Instance:
    Object Type = ECC_GameTraceChannel2 # padrao ECC_Pawn
    Collision Profile Name = Corpo      # padrao Pawn
```

**Abre só quando a forma plana estoura os 160 caracteres.** Abrir sempre
custaria legibilidade nas structs pequenas: `Relative Location` vale mais como
uma linha do que como três, e o `Z` sozinho perderia a companhia do `X` e do
`Y` que dizem que aquilo é uma posição.

Recursão até três níveis. Quando abrir não ajuda — o membro de dentro também é
grande e não é struct —, tudo é desfeito, **inclusive o que a tentativa
anotou**: sem isso a nota nomeava o membro de dentro e o de fora, que são a
mesma coisa dita duas vezes. O que sobra é nomeado com o caminho inteiro
(`CharacterMesh0.Body Instance.Collision Responses`) e sai inteiro se você
pedir pelo nome — o corte não vale no modo filtrado.

**Teto na coluna de alinhamento (64).** Sem ele, uma linha larga empurra o
comentário de todas as outras para a mesma distância — com aquela struct de
2.000 caracteres, as vizinhas ganhavam 2.000 espaços cada. Alinhamento é para
ler; passou disso, atrapalha e ainda custa token.

#### Etapa 4 — Variáveis do Blueprint com a linha `variavel` — **pronta**

Saem em bloco próprio, com tipo, antes das propriedades herdadas. Variável
declarada pelo próprio Blueprint aparece **sempre**, mesmo no valor de fábrica:
ela não existe na classe pai, então a existência dela já é a informação.

#### Etapa 5 — Escrita — `write_object` **pronto**

```
Forca Vertical = 900
CollisionCylinder:
  Capsule Radius = padrao
  Body Instance = padrao
CharMoveComp:
  Max Walk Speed = 420
```

**O texto é uma lista de mudanças, não o estado final.** Nada é apagado, e
colar de volta uma ficha inteira não mexe em nada além do que as linhas dizem.
`= padrao` devolve ao valor de fábrica. Não cria variável nem componente — a
linha não está pedindo isso.

Bloco indentado alcança dentro de componente e dentro de struct. **Indentação
zero fecha o bloco**, e isso foi o defeito do primeiro teste: sem fechar, uma
linha do próprio ator escrita depois de um componente continuava sendo aplicada
no componente. Se ele tivesse uma propriedade com aquele nome, gravava no lugar
errado calado — o "compila, roda e está errado" de novo, agora gravado em
asset.

Nome que não resolve vira diagnóstico **com os nomes parecidos**, e as outras
linhas continuam sendo aplicadas.

Verificado por ida e volta num asset de rascunho: ler, escrever, ler,
reverter com `padrao`, ler — a última leitura bate com a primeira.

Um efeito que vale saber: `Collision Profile Name = Corpo` também mexe em
`Object Type` e `Collision Responses`. É o `PostEditChangeProperty` aplicando o
perfil, comportamento da própria Unreal — a ficha mostra o resultado real, não
só o que a linha pediu.

#### Etapa 5 — Escrita — blackboard **pronto**

Mesmo formato do leitor. Cria a chave que não existe, troca o tipo da que
existe, e não apaga nenhuma:

```
blackboard BB_Golem
chave Alvo : Object (Actor)
chave Fase : Int
chave Vida : Float sincronizada
```

A classe do tipo sai por varredura (`Object` → `UBlackboardKeyType_Object`),
não por tabela fixa — pelo mesmo motivo do leitor: qualquer projeto pode
escrever o seu tipo de chave, e uma tabela responderia "não conheço" para o que
o próprio projeto criou. Tipo desconhecido lista os que existem.

**`sincronizada` virou palavra, não comentário.** O leitor emitia isso como
`# sincronizada entre instancias`, e o escritor descarta comentário — uma chave
que voltasse dessincronizada seria um bug que só aparece com dois inimigos na
tela ao mesmo tempo.

Trocar o tipo troca o subobjeto inteiro. Reaproveitar o antigo deixaria as
propriedades da chave anterior penduradas na nova.

Exercitado em `Testes/BB_Teste`: quatro chaves novas criadas, uma que já existia
não duplicou, `sincronizada` voltou pela leitura, `Object (Pawn)` resolveu a
classe pelo nome curto, e tipo inexistente foi recusado listando os doze que
existem.

**Falta escrever BT**, e é a cara: o asset guarda a hierarquia de execução *e*
um grafo de editor (`UBehaviorTreeGraph`) que precisa ficar em sincronia.
Escrever só o lado de runtime dá um asset que roda e aparece vazio na tela.

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

**Se o MCP estiver fora do ar**, o passo 2 tem um caminho que não depende dele:

```powershell
New-Item -ItemType Directory -Force "C:\Unreal Projects\BossRush\Saved\NodeScribe" | Out-Null
Set-Content "C:\Unreal Projects\BossRush\Saved\NodeScribe\comando.txt" "quit" -NoNewline
```

O plugin vigia esse arquivo meio a meio segundo, apaga ao ler e chama o mesmo
`SaveAllAndQuit` — com as mesmas recusas, inclusive a de Play In Editor
rodando. A saída vai para `resposta.txt` ao lado.

Não é economia de token: escrever o arquivo custa o mesmo que a chamada MCP, ou
um pouco mais. É para o ciclo não travar quando a peça do meio cai — o que já
aconteceu, com o editor aberto e o MCP desconectado, e custou pedir para uma
pessoa clicar no X. Matar o processo resolveria e é a pior saída: perde
trabalho não salvo e pula a checagem de PIE.

**Um servidor próprio dentro do plugin não vale.** Seria mais código para
resolver menos: continuaria sendo um protocolo de rede que precisa estar de pé,
só que mantido por nós em vez da Epic. A vantagem do arquivo é não ter nada que
possa cair.

Três coisas que custam tempo quando esquecidas:

- **Os caminhos acima são desta máquina** — Engine em `E:`, projeto em `C:`.
  Confira antes de rodar em outra.
- **Confira a saída do build, não só o código de saída.** `Build.bat` sai com 0
  em situações em que não compilou nada. Procure `Result: Succeeded`.
- **Live Coding não serve aqui, e ainda atrapalha.** Ele recompila o corpo de
  uma função que já existe, sem fechar o editor; `UFUNCTION` ou `UCLASS` nova é
  reflection nova, e reflection nova exige restart. Pior: com o editor aberto,
  o `Build.bat` **nem tenta compilar** — para com `Unable to build while Live
  Coding is active` e não mostra erro de compilação nenhum. Não dá para
  adiantar a correção de sintaxe com o editor aberto. Fechar vem primeiro,
  sempre.

### Testar

O teste que mais pega defeito, e é de graça: **ida e volta**. Ler um grafo,
colar o texto num Blueprint vazio, ler de novo, comparar os dois textos.
Leitor e escritor são espelhos por projeto — quando o texto não fecha, um dos
dois está mentindo, e o diff diz qual.

`Testes/rodar_testes.py` faz isso sozinho, sem interface, em ~20 segundos:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" --% "C:/Unreal Projects/BossRush/BossRush.uproject" -run=pythonscript -script="C:/Unreal Projects/BossRush/Plugins/NodeScribe/Testes/rodar_testes.py" -unattended -nopause -nosplash -NullRHI
```

O relatório sai em `Saved/NodeScribe/testes.txt`, e falhou, o processo sai com
código diferente de zero.

**Duas armadilhas no comando, as duas por causa do espaço em "Unreal
Projects".** O `--%` faz o PowerShell parar de interpretar e passar o resto
literal — sem ele o caminho é cortado no espaço. E o caminho vai com **barra
normal**: dentro das aspas a Engine come a barra invertida como escape, e
`Testes\rodar` vira `Testesodar`.

Cada caso é escrito num Blueprint vazio, lido, e essa leitura é escrita num
*segundo* Blueprint vazio e lida de novo. Espelho de verdade dá as duas
leituras idênticas. É o formato de teste que não precisa de resposta guardada a
mão — nada para atualizar quando o formato mudar, e nenhum caso que passa por
estar desatualizado junto com o código.

Ele não cobre legibilidade: `True:` num node que não é Branch fecha o ponto fixo
igual, e só apareceu porque alguém leu. Para isso ainda é preciso ler um grafo
real de vez em quando — `read_graph` num asset de verdade, e olho no texto.

Quando fizer falta rodar dentro da suíte da Engine, o molde é
`ToolsetRegistry/Source/ToolsetRegistry/Private/Tests/ToolsetLibraryTest.cpp`
(`BEGIN_DEFINE_SPEC`). Os casos migram como estão; o que muda é quem chama.

### O vocabulário que falta

`Saved/NodeScribe/vocabulario.txt` acumula uma linha por **nome escrito que não
casou com nada**: node não encontrado, node ambíguo, pino de entrada inexistente,
saída inexistente num `$x.Pino`.

```
node     Fazer Cafe
ambiguo  Apply Settings
pino     In Vec  em  Break Vector
saida    Xicara  em  $pc
```

Não é log de erro, e a distinção importa. O erro já volta no retorno da chamada,
já vai para o Message Log e já fica como **comentário vermelho dentro do grafo**
— que é onde ele serve, junto do problema, e de onde o próprio `read_graph` o
traz de volta. Duplicar aquilo seria uma cópia mais pobre, e cara de ler.

Este arquivo responde outra pergunta, que sem ele não dá para responder: **quais
apelidos faltam no catálogo.**

```bash
sort vocabulario.txt | uniq -c | sort -rn
```

Depois de algumas semanas de uso, isso é uma lista de tarefas ordenada por
frequência. Foi um caso desses que mostrou que o exemplo `Break Vector (In Vec =
...)` da documentação nunca funcionou.

Por isso não há data na linha: repetido precisa sair **idêntico**, senão a
contagem não existe — e a ordem do arquivo já é cronológica. E por isso não entra
aqui o que não é vocabulário (tipo incompatível, pino vazio, evento duplicado):
esses têm causa conhecida e viram ruído numa contagem que existe para achar
padrão.

### Armadilhas que já morderam

Compilam sem reclamar e falham em tempo de execução. A build passar não diz
nada sobre elas.

- **`UEdGraphSchema_K2()` na pilha derruba o editor.** Classe UObject não pode
  ser instanciada assim: o construtor chama `FObjectInitializer::Get()`, que só
  vale dentro de um construtor de UObject, e o erro é fatal na hora. Use
  `GetDefault<UEdGraphSchema_K2>()`. Já aconteceu duas vezes — no `cac90d8`, e
  de novo ao escrever o `NodeScribePropertyText`. Vale conferir toda vez que
  aparecer `ConvertPropertyToPinType`.

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
4. **Ferramenta nova custa escolha.** Não é o prompt — a descoberta é
   preguiçosa e a descrição só é lida quando pedida. É que seis portas
   parecidas fazem o assistente escolher errado e gastar um turno descobrindo
   isso. Prefira um parâmetro novo numa ferramenta existente a uma ferramenta
   nova.

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

### MCP próprio: falar direto com o cliente

Hoje as nove ferramentas do NodeScribe chegam ao cliente por três camadas da
Epic: o plugin `ModelContextProtocol` (o servidor HTTP), o `ToolsetRegistry` (o
registro) e o `AllToolsets`/`EditorToolset` (que expõem as três ferramentas
genéricas). O cliente vê `list_toolsets`, `describe_toolset` e `call_tool` — e
todo `read_graph` viaja dentro de um `call_tool`.

A proposta é o plugin falar MCP por conta própria. **Em dois estágios, e o
primeiro serve sozinho** — o segundo só troca quem hospeda.

#### Estágio 1 — registrar as ferramentas direto

`IModelContextProtocolModule::GetChecked().AddTool(...)` aceita qualquer
`IModelContextProtocolTool`: nome, descrição, JSON Schema de entrada e um `Run`.
São exatamente as quatro coisas que a ferramenta já tem, hoje escritas em
Python. Registrando direto, `read_graph` vira uma ferramenta de primeira classe
no cliente, e a camada Python inteira sai.

#### Estágio 2 — servir o protocolo

O `HTTPServer` é módulo do **core** da Engine (`Runtime/Online/HTTPServer`), não
plugin — é sobre ele que o servidor da Epic é construído. Ele é um
`FTSTickerObjectBase`, então os handlers rodam na thread de jogo, que é
justamente onde se pode mexer em Blueprint: nada de marshalling.

O mínimo de MCP para um servidor só de ferramentas é `initialize`,
`notifications/initialized`, `tools/list` e `tools/call`, em JSON-RPC 2.0 sobre
POST. **SSE não é obrigatório** — o servidor da Epic só abre `text/event-stream`
quando o cliente pede progresso, e responder JSON puro é conforme a
especificação. O `.mcp.json` do projeto passa a apontar para a nossa porta.

#### Onde o token vaza hoje

Medido, não de memória:

- As nove descrições somam **4.551 caracteres (~1.140 tokens)**, mais o schema
  dos parâmetros. É o que `describe_toolset` devolve, uma vez por sessão.
- O envelope do `call_tool` custa ~30–40 tokens por chamada — o nome do toolset
  sozinho (`nodescribe_toolset.toolsets.graph.NodeScribeTools`) são 46
  caracteres repetidos em toda chamada.
- `list_toolsets` são ~600 tokens quando chamado.

**Seja honesto com essa conta: não é uma economia de ordem de grandeza.** As
descrições que hoje vêm por `describe_toolset` passam a morar no prompt, então
nesse eixo é quase empate — troca-se uma chamada de descoberta por presença
permanente. O que sobra de ganho limpo é o envelope (~1.000 tokens numa sessão
de trinta chamadas) e as três descrições genéricas que deixam de existir.

A economia grande deste plugin já foi feita, e foi o catálogo. Quem prometer
mais que isso aqui está vendendo.

#### O que ela deixa desligar

Essa é a pergunta que decide. Com o estágio 1: `ToolsetRegistry`,
`PythonScriptPlugin` (para nós) e a camada `toolset_registry` inteira — some
junto o `__pycache__` que suja o repositório a cada execução. Com o estágio 2:
o `ModelContextProtocol` também.

Desligar `AllToolsets`/`EditorToolset` é **uma decisão à parte, e tem preço**:
vão junto o `duplicate`, o `move` e o `delete` de asset, que este projeto usa e
que o NodeScribe não substitui. Medido em *Criar asset*: ~4.000 tokens de
descoberta uma vez por sessão e ~50 por chamada.

#### O que melhora além de token

É aqui que a proposta se paga:

- **O erro de caminho errado passa a ser nosso.** Hoje `/Game/UI/...` num asset
  de plugin devolve `... is not valid Object for property 'target'`, do
  `ReferenceConverter` da Engine, antes de qualquer código nosso rodar. Com o
  parâmetro chegando como string, o plugin resolve o caminho e pode sugerir as
  raízes de conteúdo parecidas.
- **Parâmetro opcional funciona de verdade.** O registro em Python ignora
  `= ''` e só respeita `str | None`; com schema nosso, `filter` é opcional
  porque nós dizemos que é.
- **Três plugins experimentais a menos entre nós e o cliente.** Os três são
  `IsExperimentalVersion` e podem mudar de forma entre versões da Engine.

#### O que continua igual, e não adianta fingir

O README já registra, em *Desenvolvimento*, que um servidor próprio "não vale"
— aquilo foi dito sobre substituir o **arquivo de comando**, que existe
justamente para quando a rede cai. O argumento sobrevive nesta parte: o estágio
2 não elimina o risco de ter um protocolo de rede de pé, só muda de dono. O
`Saved/NodeScribe/comando.txt` continua sendo o caminho que não depende de nada,
e não sai.

#### Custo e ordem

| etapa | onde | custo |
|---|---|---|
| 1 | interface própria de ferramenta + 9 adaptadores + resolução de `refPath` | ~400 linhas |
| 1 | apagar `Content/Python/` e a dependência do `ToolsetRegistry` | ~0 |
| 2 | servidor JSON-RPC sobre `FHttpServerModule` (`initialize`, `tools/list`, `tools/call`) | ~350 linhas |
| 2 | porta configurável, `bind` em 127.0.0.1, checagem de `Origin` | ~50 linhas |

O estágio 1 é pré-requisito do 2 de qualquer jeito: as ferramentas precisam
existir como objeto com nome, schema e `Run` antes de importar quem as serve. E
o desenho que os dois compartilham é o mesmo — **as ferramentas se escrevem
contra uma interface nossa, e um adaptador as entrega a quem estiver
servindo**: o servidor da Epic no estágio 1, o nosso no 2.

Duas coisas para decidir antes de escrever código: a **porta** (8000 é a da
Epic; conviver exige outra) e se `AllToolsets` fica ligado pelo `duplicate`/
`move`/`delete`.

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
  Skeletal Mesh = /Game/BossRush/Bosses/SKM_Golem.SKM_Golem
  Relative Location = (X=0.0,Y=0.0,Z=-90.0)   # padrao (X=0.0,Y=0.0,Z=0.0)
CharacterMovement : CharacterMovementComponent
  Max Walk Speed = 250                        # padrao 600
~ 431 propriedades no padrao
```

Duas coisas parecem longas de propósito. **Asset sai por caminho completo**,
igual ao grafo — `SKM_Golem` seria mais curto e ambíguo, e duas pastas podem
ter um asset com esse nome. **Struct sai na forma canônica da Engine**,
com `X=`, `Y=`, `Z=`: encurtar para `(0, 0, -90)` economizaria uns cinco
tokens numa minoria das linhas e abriria uma classe de erro de ida e volta
que hoje não existe. É exatamente a troca que o resto deste documento
recomenda não fazer. O que **é** limpo na saída são os zeros à direita
(`-90.0`, não `-90.000000`), que escondiam o número no meio do ruído.

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
que importam. `ACharacter::bIsCrouched` é `BlueprintReadOnly` sem `Edit`: não
aparece no painel — não faz sentido *digitar* se o personagem está agachado —
e é lida no grafo o tempo todo. Um filtro só de Details responderia "não achei"
para ela.

O transiente sai pelo motivo oposto: `APawn::LastHitBy` é `BlueprintReadOnly,
transient`, estado de execução que nem chega a ser salvo no arquivo. Numa ficha
seria ruído que muda sozinho entre duas leituras.

**Superfície:** duas ferramentas, não seis. `read_object` e `write_object`;
`tipos <` entra como sintaxe de filtro, e a doc nova vira um parâmetro de
`get_format_docs`, não uma ferramenta a mais. Não é pelo custo no prompt — com
descoberta preguiçosa esse custo quase não existe (veja *O resto do mapa*) —
é para o modelo não ter que escolher entre seis portas parecidas, que é onde
ele erra e gasta um turno descobrindo que escolheu errado.

### Behavior Tree e Blackboard

Saiu daqui: virou frente ativa. O plano completo — formato, percurso de
implementação e ordem das etapas — está em **Roteiro: ler a IA do inimigo**,
mais acima.

Fica só a nota que não cabia lá, sobre o toolset nativo. O `AIModuleToolset`
da Engine existe (7 ferramentas) mas **não está ligado neste projeto**, e
mesmo ligado seria só leitura — não edita nada. E a leitura é das piores que
vi: `list_nodes` devolve referências de UObject e `get_node_depths` devolve um
array paralelo de inteiros que o modelo tem que casar de cabeça, e ainda
faltam N chamadas de `get_properties` para saber o que cada node é.

### O resto do mapa

O `EditorToolset` expõe **224 ferramentas**, e a tentação é achar que elas
custam um imposto fixo por mensagem. **Não custam.** O servidor MCP desta
Engine usa descoberta preguiçosa: o que fica no prompt são três ferramentas
(`list_toolsets`, `describe_toolset`, `call_tool`), e as 224 descrições só
aparecem quando alguém pede a de um toolset específico.

Isso muda a estratégia, para melhor:

- **Desligar o `EditorToolset` economiza pouco por si só** — o `list_toolsets`
  inteiro são ~600 tokens, e só quando chamado. Não é meta que valha
  perseguir sozinha.
- **Cada ferramenta substituída já paga na hora.** Não é preciso cobrir 224
  antes de ver benefício. Uma ficha que evita um dump de 10.000 tokens
  economizou 10.000 tokens, com as outras 223 no lugar.

O desperdício está no **uso**, não na presença: dumps grandes, fluxos de dois
turnos, e padrões de N chamadas por objeto. É de lá que sai a economia, e é o
que o mapa abaixo persegue.

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

### Criar asset: medido, e **não vale** por token

Ficou a dúvida se criar asset deveria entrar aqui para economizar. Medimos.

| | custo |
|---|---|
| `describe_toolset` do `AssetTools` (21 ferramentas) | ~4.000 tokens, **uma vez por sessão** |
| a chamada em si (`duplicate`, `move`, `save_assets`…) | ~50 tokens |

O gasto é **descoberta**, não uso — e trazer a operação para cá só evitaria
aquela descoberta se nunca precisássemos de mais nada daquele toolset. Mas
`find_assets`, `get_referencers`, `get_dependencies`, `move` e `delete` moram
todos lá.

E o principal: **esse toolset não tem payload gordo para comprimir.**
`find_assets` devolve uma lista de caminhos, `get_dependencies` idem,
`duplicate` devolve um booleano. É o caso bem resolvido — não há dump, não há
fluxo de dois turnos, não há N chamadas por objeto. Pela pergunta que este
README manda fazer antes de construir ferramenta nova — *o que ela deixa
desligar?* —, a resposta é "nada".

**Há um buraco, mas é de capacidade, não de token:** o toolset nativo tem
`duplicate`, `move` e `delete`, e **não tem `create_asset`**. Criar um
BlackboardData ou uma BehaviorTree do zero não dá; só duplicando um existente.
Se isso incomodar, vale construir — com `IAssetTools::CreateAsset` e a factory
do tipo, é pequeno. Mas então é para poder fazer algo novo, e o README não deve
fingir que é economia.

### `Target` sai do leitor e não entra no builder

Pior que o Cast, porque quebra a ida e volta em node comum. `Get Blackboard`
é lido como `Get Blackboard (Target = $controller)` — e escrever exatamente
isso falha, com um diagnóstico que se contradiz:

```
O node nao tem pino `Target`. Pinos de entrada: Target
```

O pino existe e a lista o mostra. O casamento de nome do pino *self* rejeita
`Target` para esse node, enquanto o leitor o emite. Passar por posição —
`Get Blackboard ($controller)` — funciona.

Achado escrevendo o `BTService_Distancia` do BossRush. Significa que
`read_graph` → `write_graph` não fecha para qualquer grafo que use esse node,
e provavelmente para outros com a mesma forma de pino. **É o primeiro defeito
que quebra a promessa central do formato**, e deve vir antes de recurso novo.

### Cast com continuação não volta igual

Achado usando o plugin de verdade. No `Gameplay Ability Graph` do
`GA_GolemSalto`, a cadeia depois de um `Cast to Character` sai **sem rótulo e
sem aviso**. O `FORMATO.md` diz que node com mais de uma saída de execução para
a cadeia de propósito e espera um rótulo — mas ali o leitor parou e não disse.

O texto lido, colado de volta, deixaria o `Launch Character` desligado: um
grafo que compila e não faz nada. É exatamente o modo de falha que este projeto
existe para evitar, e passou porque leitor e escritor foram exercitados em
grafos sem Cast no meio da execução.

**Conserto:** o leitor emite o rótulo do pino sob o Cast, ou avisa que não
sabe. Falhar em voz alta é o mínimo.

### Listar os grafos de um Blueprint — **pronto**

`read_object` num Blueprint agora traz `# grafos: ...`. Sem isso o nome do
grafo era adivinhação: `EventGraph` funciona quase sempre e falha sem dizer por
que. O `GA_GolemSalto` usa `Gameplay Ability Graph`, com espaços, e o
`GA_ChuvaDePedras` usa `EventGraph` — dois assets do mesmo tipo, criados por
caminhos diferentes. Sem a listagem, restava tentar nomes até acertar.

### Onde estamos nessa lista

| item | estado | o que trava enquanto falta |
|---|---|---|
| 1 — Variável de Blueprint | **pronto** | — |
| 2 — Criar asset | **pronto** | — |
| 3 — Escrever Behavior Tree | falta | toda mudança na árvore |
| 4 — Componentes de Gameplay Effect | falta | todo cooldown novo |
| 5 — Criar Gameplay Tag | **pronto** | — |
| 6 — Consertos de leitura | falta | `Default Starting Data`; Cast que não volta igual |

Com 1, 2 e 5 prontos, o que resta para a IA do Golem fechar sem clique é 3 e 4.
O 3 é o maior e o único onde errar grava um asset que parece certo e está
vazio — por isso vai por último.

### O que falta para não precisar pedir cliques

Levantado revisando uma sessão inteira de trabalho real, onde montar uma
mecânica de GAS exigiu doze pedidos de "clique aqui". A lista é o que faltou.

**Antes: nem tudo que foi pedido precisava ser pedido.** `Cooldown Gameplay
Effect Class` e `Activation Owned Tags` são propriedade — `write_object` já
resolve, inclusive `GameplayTagContainer`, que aceita a forma canônica
`(GameplayTags=((TagName="X")))`. Foram cliques pedidos por não ter testado.
Vale checar se a ficha já cobre, antes de propor ferramenta.

Em ordem do que mais travou:

**1. Variável de Blueprint — criar, apagar, marcar Instance Editable.**
**Pronto.**

```
variavel Alcance : Float editavel = 800
variavel Classe da Habilidade : GameplayAbility Class editavel
variavel Lista de Tags : Array de Name
apagar variavel Contador Interno
```

Coube na sintaxe que já existia: `variavel X : Tipo` era lido e ignorado na
escrita, agora cria quando não existe. `editavel` fecha a declaração, como
`sincronizada` no blackboard, e **o leitor emite de volta** — sem isso uma ida
e volta apagaria a marcação calada, e a variável sumia do painel de quem usa o
Blueprint.

**Apagar tem palavra própria.** Este escritor não apaga nada por princípio, e
apagar variável derruba todo node que a usava. Não pode acontecer por descuido
de formatação.

**Tipo já existente não é trocado.** Trocar tipo de variável em uso quebra os
nodes que a consomem; isso pede uma decisão, não um efeito colateral.

Duas coisas que só o teste real mostrou:

- **Criar variável exige recompilar o Blueprint.** `AddMemberVariable` mexe na
  lista, mas a propriedade só existe na classe depois de
  `FKismetEditorUtilities::CompileBlueprint`. Sem isso a variável recém-criada
  não aparecia nem na ficha nem no painel, e quem chamou teria que pedir um
  clique em Compile. Criar algo que não dá para ver é pior que não criar.
- **`X Class` não era um tipo.** `ResolvePinTypeFromName` só produzia referência
  a objeto, então `GameplayAbility Class` — exatamente o que uma BTTask precisa
  para apontar habilidade — falhava. O sufixo agora vira `PC_Class`, e
  `DescribePinType` o emite de volta. Era um furo no espelho que ninguém tinha
  esbarrado porque nenhum grafo testado declarava variável de classe.

**2. Criar asset.** **Pronto.**

```
create_asset("/Game/BossRush/IA/Golem/Behavior/BTTask_UsarHabilidade",
             "BTTask_BlueprintBase")
```

**É capacidade, não economia** — o toolset nativo também não tem
`create_asset`, só `duplicate`. Foi construído para poder fazer algo que
ninguém fazia, e este README não finge que economiza token.

Dois caminhos, decididos por `FKismetEditorUtilities::CanCreateBlueprintOfClass`:
o que é Blueprintable — `GameplayEffect`, `BTTask_BlueprintBase`,
`BTService_BlueprintBase` — vira Blueprint com aquele pai; o resto —
`BlackboardData`, `BehaviorTree` — vai pela factory do tipo. A factory sai por
varredura, não por tabela, pelo mesmo motivo dos tipos de chave de blackboard:
plugin e projeto podem trazer a sua.

Duas guardas, e a segunda quase não existiu:

- **Só dentro de `/Game/`.** Escrever em `/Engine` a partir de uma linha de
  texto é o tipo de acidente que não se desfaz.
- **Nunca sobrescreve** — e a primeira versão sobrescrevia. `DoesPackageExist`
  só olha o disco, e asset recém-criado e ainda não salvo mora só na memória,
  que é exatamente o estado em que ele fica. Chamar duas vezes seguidas passava
  direto pela guarda e recriava por cima. Agora checa também `FindPackage`.

O asset fica sujo, sem salvar, como qualquer recém-criado no editor — quem
criou por engano fecha sem salvar.

**3. Escrever Behavior Tree.** É a etapa 5 do roteiro, e a que mais pesa: sem
ela, toda mudança na árvore é clique. O asset guarda a hierarquia de execução
*e* um `UBehaviorTreeGraph` que precisa ficar em sincronia — escrever só o lado
de runtime dá um asset que roda e aparece vazio na tela.

**4. Componentes de Gameplay Effect.** O `Grant Tags to Target Actor` é um
subobjeto instanciado dentro de `UGameplayEffect::GEComponents`, e
`write_object` não cria subobjeto dentro de array. Sem isso, todo cooldown novo
é clique. Vale como caso geral: **array de subobjeto instanciado** aparece em
muito lugar da Engine.

**5. Criar Gameplay Tag.** **Pronto.**

```
read_tags("Cooldown")        →  tags  ~ "Cooldown" (1 de 31)
                                tag Cooldown.Golem.Salto

write_tags("tag Cooldown.Golem.Laser", "BossRush.ini")
                             →  1 tag(s) criada(s) em BossRush.ini.
```

`Cooldown.Golem.Salto` precisou existir antes de ser usada, e tag não é asset
nem propriedade — vive num ini —, então nem `create_asset` nem a ficha
alcançavam. Quem cria é `IGameplayTagsEditorModule::AddNewGameplayTagToINI`, do
módulo de editor; não é `UGameplayTagsManager`, onde ela não está.

Aceita `tag X` ou só `X`, e **pula o cabeçalho que o leitor emite** — sem isso a
ida e volta falhava numa linha escrita pelo próprio plugin. Não apaga nem
renomeia: as duas coisas quebram todo asset que usa a tag, e isso pede uma
decisão, não um efeito de formatação.

Três coisas que só o teste real mostrou:

- **"Já existe" tem que ser `IsDictionaryTag`, não `RequestGameplayTag`.** A
  pergunta é se a tag foi *declarada*, não se ela resolve. `Cooldown.Golem`
  resolve porque `Cooldown.Golem.Salto` existe, mas não está declarada em ini
  nenhum — e o leitor, que só lista as declaradas, nunca a mostraria. A primeira
  versão pulava por resolver: pedia-se a tag, ouvia-se "já existia", e ela não
  aparecia na listagem. A própria Engine faz essa distinção, e pelo mesmo motivo.
- **O ini de destino não é o mesmo dos outros.** Com a fonte vazia a Engine
  grava em `DefaultGameplayTags.ini`, e as tags do BossRush moram em
  `Config/Tags/BossRush.ini`. O parâmetro `source` escolhe, e o retorno **diz em
  qual arquivo caiu** — o destino sair calado espalharia as tags em dois lugares
  sem ninguém notar. Fonte que não existe é recusada com a lista das que
  existem, em vez de virar um ini novo por erro de digitação.
- **Não adivinhe por que a Engine recusou um nome.** A primeira versão dizia
  "aceita letras, numeros, ponto e underscore" — e este projeto tem
  `Facção.Inimigos` e `Estado.Ação.Defendendo`. Acento passa; o que vale é o
  `InvalidTagCharacters` do projeto. `IsValidGameplayTagString` devolve o motivo
  da Engine **e um nome corrigido para sugerir**, e é isso que sai:
  ``Teste,Virgula (Tag may not contain the following characters: , tente
  `Teste_Virgula`)``.

**6. Dois defeitos de leitura**, que não são recurso e sim conserto:
`Default Starting Data` do AbilitySystemComponent não é legível pela ficha — e
é justamente onde se descobre quais habilidades um personagem tem —, e o Cast
com continuação não volta igual (seção acima).

### O que não vale trazer para cá

O toolset nativo já resolve bem, e duplicar só adiciona superfície:

| | por que já está bom |
|---|---|
| mover, duplicar, apagar, achar asset | payload é lista de caminhos; medido em *Criar asset* |
| salvar, estado de source control, dirty | booleano por chamada |
| console variables, controle de PIE, câmera do viewport | thin, uma chamada e pronto |
| ler o output log | texto que se filtra na origem; nada a comprimir |
| colocar ator no nível | devolve uma referência, sem dump |
| propriedade de material, mesh, textura, data asset | **a ficha já cobre** — são propriedades, e `read_object`/`write_object` são genéricos |

Essa última linha é a que mais rendeu: as ~107 ferramentas de edição por tipo
de asset do `EditorToolset` são, na maioria, propriedade — e a ficha as alcança
sem uma linha de código por tipo, porque `TFieldIterator` não sabe o que é um
material.

**Ainda em aberto, e vale:** consulta de mundo com projeção de campos —
`find_actors` devolve referências e cada atributo é outra chamada, então quinze
atores viram cinquenta chamadas. É o único item do mapa original que continua
valendo e nunca foi feito.

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
