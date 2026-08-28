# Formato NodeScribe

Uma linha = um node. Nada mais.

Tudo que **não** está escrito aqui é responsabilidade do plugin: posição dos nodes,
ligação de execução, cast implícito entre tipos, nome interno das funções.

---

## O básico

```
Get Player Controller
Print String (In String = "olá")
```

Duas linhas → dois nodes, já ligados na ordem em que aparecem.

## Guardar um resultado

Use `nome =` para dar nome à saída de um node e reaproveitá-la com `$nome`:

```
pc = Get Player Controller
Enable Input (Target = $pc)
```

`$nome` também funciona direto com variáveis do Blueprint, sem declarar nada —
o plugin cria o node de Get automaticamente:

```
Print String (In String = $NomeDoJogador)
```

### Quando o node tem mais de uma saída

`$nome` sozinho pega a saída principal. Se o node tiver várias — um evento com
parâmetros, um Break de struct, uma função com *out params* — diga qual com
`$nome.Pino`:

```
tecla = evento OnKeySelected
Map Player Key (New Key = $tecla.Selected Key Key)
```

Se o pino não existir, o plugin lista as saídas que o node realmente tem.

Isso também alcança **parte de uma struct**. Se você pedir `$tecla.Selected Key
Key` e o node só tiver o pino `Selected Key` inteiro, o plugin divide a struct
(o mesmo que *Split Struct Pin*) para achar a parte pedida.

## Declarar variáveis

```
variavel Pulos Totais : Integer = 3
variavel Duração do Pulo : Float = 1.0
variavel Timer Salto : Timer Handle
variavel Inputs : Array de Name
variavel Espelhos : Mapa de Int64 para BP_Espelho
variavel Já Vistos : Conjunto de Name
```

A variável é criada antes dos nodes que a usam. Se já existir, a linha é
ignorada — colar o mesmo texto duas vezes não faz mal.

O tipo é o nome que aparece na interface: `Float`, `Integer`, `Boolean`,
`Name`, `Text`, `String`, qualquer struct (`Timer Handle`, `Vector`), qualquer
enum (`EPlayerMappableKeySlot`) e qualquer classe (`BP_Golem`).

Coleção: `Array de X` para lista, `Conjunto de X` para set, `Mapa de X para Y`
para mapa. A chave e o valor de um mapa não podem ser coleção — a Unreal não
tem `TMap<int, TArray<X>>` —, e uma linha que peça isso é recusada em vez de
virar outra coisa.

**Widget do Designer não se declara.** Um `ScrollBox` da tela vira variável ao
ser colocado no Designer com *Is Variable* marcado — declarar uma com o mesmo
nome daria uma variável que compila e nunca aponta para o widget. O plugin
recusa e diz isso.

## Argumentos

Entre parênteses, separados por vírgula. Aceita nome do pino ou posição:

```
Print String (In String = "oi", Duration = 5.0)
Print String ("oi", 5.0)
```

Os nomes são comparados de forma tolerante — `In String`, `instring` e
`In_String` dão no mesmo. `Target` e `Alvo` apontam para o pino self.

A comparação também ignora acento, e isso vale em todo o formato: rótulo
(`então:`), nome de pino, nome de variável (`$Duração` acha `Duracao`, e o
contrário também). Escrever em português não deve depender de lembrar em que
palavra o plugin foi escrito sem acento.

## Quando dois nodes têm o mesmo nome

`Apply Settings` existe em `GameUserSettings` e em `EnhancedInputUserSettings`.
Nesse caso o plugin não escolhe — e você diz qual com `Classe.Funcao`:

```
EnhancedInputUserSettings.ApplySettings (Target = $settings)
```

Parênteses **não** servem para isso: eles já são a lista de argumentos.

## Ramos (Branch, loops)

Indente e use um rótulo terminado em `:`.

```
Branch (Condition = $bEstaVivo)
  verdadeiro:
    Print String (In String = "vivo")
  falso:
    Print String (In String = "morto")
```

Rótulos aceitos (PT ou EN): `verdadeiro`/`true`, `falso`/`false`,
`corpo`/`loop body`, `completo`/`completed`, `então`/`then`.
Qualquer outro nome é comparado direto com o nome do pino de saída.

Quando um node tem mais de uma saída de execução e você **não** abre um rótulo,
a cadeia para ali de propósito — escolher um ramo por você seria adivinhar.

## Switch

```
Switch on EJSL4UBatteryLevel (Selection = $nivel)
  Empty:
    Print String (In String = "sem bateria")
  Full:
    Print String (In String = "cheia")
```

Os rótulos são os valores do enum. Também `Switch on Int`, `Switch on String`
e `Switch on Name`.

## Eventos

```
evento BeginPlay
evento MinhaHabilidadeAtivou
```

Se o nome existir na classe pai, vira o evento de override. Se não existir,
vira um Custom Event com esse nome (e o plugin avisa que fez isso).

Para o evento de um **dispatcher de outro objeto** — o que você cria clicando
com o botão direito num widget filho e escolhendo o dispatcher dele:

```
tecla = evento OnKeySelected de SelecionarInputKey
Print String (In String = $tecla.Selected Key Key)
```

`de` (ou `of`) separa o nome do dispatcher da variável que o expõe. Se a
variável não existir, ou não tiver esse dispatcher, o plugin lista os que ela
tem em vez de criar um evento solto.

Se o nome for um evento **que existe na Engine, mas não nesta classe** — o caso
clássico é `BeginPlay` num Widget Blueprint, que só tem `Construct` — o aviso
sobe para **amarelo** e lista os eventos que a classe pai realmente oferece.
Um Custom Event chamado `BeginPlay` compila, parece certo no grafo, e nunca
dispara; é o tipo de erro que só aparece em playtest.

## Evento de Input Action

```
EnhancedInputAction /Game/BossRush/Player/Inputs/IA_Ataque.IA_Ataque
  Started:
    Print String (In String = "atacou")
```

Os rótulos são os gatilhos: `Started`, `Triggered`, `Completed`, `Canceled`,
`Ongoing`.

Aceita o nome curto (`IA_Ataque`) se o asset já estiver carregado no editor,
mas o caminho completo é o que sempre funciona.

## Dispatchers

```
Call OnVidaMudou (Nova Vida = $vida)
Bind OnVidaMudou
Unbind OnVidaMudou
Clear OnVidaMudou
```

Em PT: `Chamar`, `Vincular`, `Desvincular`, `Limpar`.

Sem `Target`, o dispatcher é deste Blueprint. Com `Target = $obj`, é do objeto
apontado. Para o **evento** de um dispatcher, veja `evento X de $Variavel`.

## Select

```
Select (Index = $bEstaVivo, Option 0 = "morto", Option 1 = "vivo")
```

## Cast

```
boss = Cast to BP_Boss (Object = $ator)
```

## Variáveis

```
Set Vida (Vida = 100)
vida = Get Vida
```

Só é tratado como variável se ela existir no Blueprint. Por isso
`Get Player Controller` continua sendo a função, não uma variável chamada
"Player Controller".

### Variável de outro objeto

Passe `Target`, e o plugin acha a variável na classe dele:

```
pc = Get Player Controller
Set Show Mouse Cursor (Target = $pc, Show Mouse Cursor = true)
```

O nome pode ser o que aparece na tela: `Show Mouse Cursor` acha
`bShowMouseCursor`.

### Variável que ainda não existe

`$Alguma Coisa` que não existe **não perde a ligação**: o node de Get entra
assim mesmo, com o tipo do pino que ia consumi-lo. O Blueprint acusa o erro e
o botão direito no node oferece criar a variável — o mesmo comportamento de
colar nodes entre dois Blueprints diferentes.

Sai um aviso amarelo. O Blueprint continua sem compilar até você criar a
variável; o que muda é que a pendência fica no grafo, a um clique de resolver,
em vez de virar uma cadeia perdida.

Isso vale para `$nome` usado como argumento. Uma linha `Get X` solta, com `X`
inexistente, continua sendo erro — ali não há pino consumidor de onde tirar o
tipo.

## Structs

```
args = Make MapPlayerKeyArgs (Mapping Name = $Nome, Slot = First)
Break Vector ($posicao)
```

No `Make`, cada pino tem o nome do campo. No `Break` há um pino de entrada só —
por isso a forma sem nome, por posição, é a que sempre funciona.

**Algumas structs trazem a própria função de montar e quebrar**, e para elas o
plugin usa essa função em vez do node genérico: `Vector`, `Rotator`,
`Transform` e `Color` estão nesse caso. O node genérico ali compila com aviso da
Engine — *"the structure cannot be broken using generic 'break' node"* —, e esse
aviso não teria como ser evitado por quem escreve o texto, já que o formato não
tem como escolher entre os dois nodes.

Só muda o nome do pino de entrada, que passa a ser o do parâmetro da função
(`In Vec`, e não `Vector`). A forma por posição atravessa as duas.

Aceita o nome interno (`MapPlayerKeyArgs`) ou o de exibição
(`Map Player Key Args`). Só vira node de struct se a struct existir — assim
`Make Literal Int` continua sendo a função que sempre foi.

## Subsistemas

```
settings = Get EnhancedInputLocalPlayerSubsystem (PlayerController = $pc)
```

O plugin escolhe o node certo conforme onde o subsistema vive: os de
LocalPlayer pedem um PlayerController, os de Engine não pedem nada.

## Criar widget e spawnar ator

```
linha = Create Widget (Class = WBP_LinhaRemapear, Owning Player = $pc, Nome do Input = $nome)
Spawn Actor from Class (Class = BP_Boss, Spawn Transform = $t)
```

Também `Construct Object from Class`.

Nesses nodes o pino `Class` vem primeiro **por necessidade**: os pinos de
*Expose on Spawn* só existem depois que a classe é escolhida. O plugin cuida
disso sozinho — você pode escrever os argumentos em qualquer ordem. Sem `Class`,
o node entra sem esses pinos e sai um aviso.

## Macros padrão

Nomes da biblioteca da Engine funcionam direto:

```
For Each Loop (Array = $Inimigos)
  corpo:
    Print String (In String = "um inimigo")
```

Também: `Do Once`, `Flip Flop`, `Gate`, `While Loop`, `Multi Gate`, `Is Valid`.

## Outros nomes que o plugin conhece

`Return` é o node de retorno de um grafo de função. `To Text` converte qualquer
valor em Text. `Self` é a referência a este Blueprint.

Uma **ação assíncrona** — os nodes que a Engine e os plugins expõem por
`UBlueprintAsyncActionBase`, com um pino de execução por evento — entra pelo
nome que aparece no grafo, e cada saída é um rótulo indentado:

```
Wait For Any Controller Changes
  On Connected:
    Print String (In String = "conectou")
  On Disconnected:
    Print String (In String = "caiu")
```

O pino que continua na hora chama-se `then`, e é rótulo como qualquer outro.

## Buracos: o que o plugin **não** decide por você

Escreva `?` num pino cujo valor é uma escolha sua:

```
Spawn Sound 2D (Sound = ?)
```

O node entra e o pino fica vazio. Alguns pinos impedem a compilação; outros
compilam com valor nulo e só falham ao rodar. Nos dois casos a pendência é sua
e está visível — melhor que um asset chutado, que passa despercebido e vira bug
de playtest.

**Confira os pinos com `?` antes de dar Play.** Nem todo pino vazio grita.

Assets também podem ser passados por caminho completo, se você souber:

```
Spawn Sound 2D (Sound = /Game/BossRush/Audio/SFX_Hit.SFX_Hit)
```

Um nome solto que não seja caminho **não** é aceito — o plugin avisa e deixa o
pino vazio em vez de adivinhar qual asset era.

## AnimGraph

Num grafo de animação as mesmas regras valem, com uma diferença: o fluxo não é
execução, é **pose** — e ele não continua, ele *alimenta*. A cadeia termina no
**Output Pose**, que já existe no grafo e nunca se cria.

```
Idle_Parado
```

Uma linha só. `Idle_Parado` é o nome de uma AnimSequence do projeto, e ela vira
o Sequence Player já com o asset preenchido. A ligação no Output Pose é do
plugin — como toda ligação que o formato não escreve.

**Nome solto de asset é aceito aqui, e só aqui.** Fora do AnimGraph o plugin o
recusa, porque não há node óbvio para embrulhar o asset. Aqui há um só, e é o
mesmo que arrastar o asset para o grafo produz: AnimSequence vira Sequence
Player, BlendSpace vira BlendSpace Player, e assim por diante. Dois assets com
o mesmo nome curto não viram escolha — sai a lista dos caminhos.

Node de anim entra pelo nome que aparece no menu do grafo:

```
BS_Locomotion (Speed = $Velocidade)
Apply Additive
```

Duas linhas, duas poses: o BlendSpace alimenta o Apply Additive, que alimenta o
Output Pose. Consecutivas, elas se ligam na ordem em que aparecem, igual ao
EventGraph.

### O que não é pino

Nem tudo que muda o que um node de anim faz é pino. `Loop Animation` e
`Play Rate` de um asset player ficam no painel de detalhes, e entram como
argumento igual:

```
MM_Jump (Loop Animation = false, Play Rate = 1.5)
```

Vale escrever `Loop Animation` sempre que a animação não for de loop: ela
**nasce ligada**, então um `MM_Jump` que devia tocar uma vez fica repetindo sem
nada no texto dizendo isso. A leitura escreve de volta toda opção que diferir do
node recém-criado.

Opção não aceita `$referência`: é valor fixo, porque não há fio para ligar.

### Indentação, aqui, abre uma entrada

É o inverso do EventGraph. Lá o rótulo indentado abre uma **saída** — o que
acontece depois. Aqui abre uma **entrada** de pose — o que alimenta o node:

```
Blend Poses by bool (Active Value = $bParado)
  True Pose:
    Idle_Parado
  False Pose:
    BS_Locomotion (Speed = $Velocidade)
```

O rótulo é o nome do pino. Um bloco pode ter várias linhas: elas se encadeiam
entre si, e o resultado do bloco — a última linha — é o que entra no pino.

```
  False Pose:
    BS_Locomotion (Speed = $Velocidade)
    Apply Additive
```

Aqui o `Apply Additive` é quem alimenta o `False Pose`.

### Output Pose

Não precisa escrever: o fim da cadeia liga nele sozinho. Escrever funciona e
serve quando você quer deixar explícito onde a cadeia termina:

```
Idle_Parado
Output Pose
```

Se o grafo não tiver Output Pose, o plugin avisa e **não cria outro** — ele
nasce junto com o AnimGraph, e se sumiu é o grafo que está errado.

Entrada de pose vazia sai como aviso. Ela não quebra nada: compila, roda, e o
personagem fica na pose de referência, de braços abertos. É o buraco silencioso
deste tipo de grafo.

### Máquina de estados

```
Locomocao = State Machine
  estado Parado:
    Idle_Parado
  estado Correndo:
    BS_Locomotion (Speed = $Velocidade)
  Parado -> Correndo:
    Greater (A = $Velocidade, B = 10.0)
  Correndo -> Parado:
    Less Equal (A = $Velocidade, B = 10.0)
```

O `nome =` batiza a máquina — o nome de uma máquina de estados é o do sub-grafo
dela, e sem isso toda máquina nasceria "New State Machine".

`estado Nome:` abre um estado, e o bloco é o AnimGraph de dentro dele, com as
mesmas regras de tudo acima. O prefixo `estado` (ou `state`) é obrigatório: sem
ele, o rótulo seria indistinguível de uma entrada de pose.

`Origem -> Destino:` abre uma transição, e o bloco é a regra dela — um grafo de
dado que termina num bool. A última linha do bloco é quem entra no
`Can Enter Transition`; ligar isso é do plugin.

**O primeiro estado declarado é onde a máquina começa.** É a única leitura
possível sem inventar sintaxe: no grafo o Entry aponta para um estado só.

Os estados são criados **antes** de qualquer transição, então a ordem no texto
não importa — dá para escrever as transições primeiro. Em compensação, uma
transição que fale de um estado que não existe é erro, com a lista dos que
existem, em vez de um estado vazio criado por engano.

Numa regra, uma variável sozinha basta. As três formas abaixo dão o mesmo node:

```
    $Esta no Ar
    Get Esta no Ar
    Esta no Ar
```

### Opção de estado e de transição

O que fica no painel de detalhes entra entre parênteses, **antes** dos dois
pontos — a mesma sintaxe de `MM_Jump (Loop Animation = false)`:

```
  Aterrissagem -> Terra (Automatic Rule Based on Sequence Player in State = true):
```

Essa é a que mais importa. Com ela ligada, a transição dispara sozinha quando a
animação do estado de origem está acabando — e por isso **nasce sem regra
nenhuma**. É como a Epic escreve o `Land -> Locomotion` do `ABP_Unarmed`.

Sem ela, uma transição de bloco vazio é uma transição que compila e nunca
dispara, e sai aviso dizendo isso. As duas são idênticas na tela; a diferença
mora na opção, e é por isso que ela precisa caber no texto.

Entram aqui todas as opções do painel: `Duration` (o tempo de blend),
`Priority Order`, `Blend Mode`, `Min Time Before Re-entry`. A leitura escreve o
que difere de uma transição recém-criada. Um estado aceita as dele do mesmo
jeito, e um conduto também.

Opção não aceita `$referência`: é valor fixo, porque não há fio para ligar.

### Conduto

Um conduto é um cruzamento: **uma regra só**, por onde várias transições passam,
em vez de cada uma repetir a mesma condição.

```
  conduto Para o Ar:
    Get Esta no Ar
  Terra -> Para o Ar:
  Para o Ar -> Pulo:
    Greater (A = $Velocidade Z, B = 100.0)
  Para o Ar -> Queda:
    Less Equal (A = $Velocidade Z, B = 100.0)
```

O bloco de um conduto é **regra**, não pose. É a diferença que obriga a palavra
própria: `conduto X:` e `estado X:` seriam indistinguíveis no texto, e o plugin
criaria o node errado — um estado sem pose, que é a pose de referência.

Nas transições ele é uma ponta como qualquer outra, e vale `conduit` em inglês.

### Alias

Um alias é um apelido para vários estados de uma vez: uma transição que sai dele
sai de todos, sem repetir a regra em cada um. O bloco é a lista dos estados, uma
por linha.

```
  alias Para o Ar:
    Pulo
    Queda
  Para o Ar -> Aterrissagem:
    NOT Boolean (A = $Esta no Ar)
```

É o que a Epic usa no `ABP_Unarmed`: `To Falling` e `To Land` são alias, não
estados.

Alias só aponta para `estado`. Um conduto ou outro alias na lista é recusado —
a Engine varre o grafo por estados ao reconstruir as referências, e o que não
for estado **some no próximo save**, sem erro e sem aviso, levando junto a
transição que saía dali.

`alias Nome (Global Alias = true):` vale por todos os estados da máquina, e aí
o bloco fica vazio.

### Os getters de máquina de estado

Dentro de uma regra de transição existe um vocabulário que só existe ali:

```
  Pulo -> Queda:
    t = Get Relevant Anim Time Remaining
    Less (A = $t, B = 0.1)
```

`Get Relevant Anim Time Remaining`, `Get Relevant Anim Time Remaining Fraction`,
`Get Transition Time Elapsed` e os outros getters não são chamada de função,
apesar de aparecerem como uma no menu do editor. O que os faz funcionar não está
em pino nenhum: é o **estado de origem da transição**, que o plugin preenche
sozinho, porque a transição sabe de onde sai e o texto não teria como dizer.

Por isso eles só existem dentro de uma regra. Fora dali o nome cai no catálogo
de funções e acha a função homônima de `UAnimationStateMachineLibrary` — que
existe, é pública, entra no grafo, e pede dois pinos que uma regra de transição
não tem de onde alimentar. Era um node plausível que não compila, que é
exatamente o que o plugin promete não fazer.

### O caminho de volta

**Copiar grafo inteiro** num AnimGraph escreve neste mesmo formato. Duas coisas
não voltam iguais, e as duas saem avisadas:

| Situação | O que acontece |
|---|---|
| A mesma pose alimentando dois lugares | âncora nas duas pontas: o formato é árvore, e a segunda ligação se perde ao colar |
| Node de anim que tem asset mas não volta por ele (um Sequence *Evaluator*) | sai o título do node + aviso: o asset não vai no texto |

O Output Pose e o resultado de uma transição não viram linha na leitura — eles
já existem no grafo de destino, e a ligação neles é do plugin.

---

## Comentários

`#` ou `//` até o fim da linha. Para criar uma caixa de comentário no grafo:

```
Comentario Lógica de rebind começa aqui
```

## O que o parser ignora sozinho

Ao colar uma resposta de chat, isto some sem atrapalhar: cercas ```` ``` ````,
marcadores `-` e `*`, e numeração de passo em qualquer forma comum —
`1.`, `1)`, `[2]`, `3.1`.

---

## O caminho de volta

**Copiar selecionado** e **Copiar grafo inteiro** produzem texto neste mesmo
formato, pronto para colar num chat e editar.

O cabeçalho diz de onde o texto veio e declara as variáveis:

```
# WBP_LinhaRemapear -> EventGraph (selecao parcial)
# refPath: /Game/UI/WBP_LinhaRemapear.WBP_LinhaRemapear:EventGraph
# do Designer (crie na tela, marcando Is Variable):
#   NomedaHabilidadeText : Text Object Reference
variavel Nome do Input : Name
variavel KeySlot : EPlayerMappableKeySlot
```

O `refPath` é o caminho exato daquele grafo, para pedir de volta sem adivinhar.
São duas coisas que não dá para deduzir: o nome do grafo (`EventGraph` num
asset, `Gameplay Ability Graph` em outro) e a raiz de conteúdo — um asset de
plugin mora em `/NomeDoPlugin/`, não em `/Game/`.

Colar num Blueprint vazio recria as variáveis junto com os nodes. As do
Designer saem como comentário, porque não é o texto que as cria.

Só o que o próprio Blueprint declara — as herdadas seriam centenas de linhas
da Engine.

### Quatro coisas que a leitura escreve e o parser descarta

**Âncora de reconvergência.** Duas cadeias que caem no mesmo node não cabem numa
árvore. Em vez de o segundo ramo sair vazio — igualzinho a um ramo que ninguém
ligou —, o node de destino ganha um `# ancora N` no fim da linha, e o ponto de
volta diz para onde vai:

```
Branch (Condition = $bLigado)
  verdadeiro:
    Print String (In String = "ligou")
    Atualizar Tela  # ancora 1
  falso:
    # -> volta para a ancora 1 (`Atualizar Tela`)
```

A volta continua se perdendo ao colar — o que mudou é você conseguir ver que ela
existe, e onde.

**Entrada lateral.** Um fio de execução nem sempre cai na entrada principal do
node do outro lado. O `Reset` de um Do Once, o `Stop` de uma Timeline, o `Close`
de um Gate: esses não continuam a cadeia, mandam um comando para um node que
vive em outro lugar do grafo. Sai com a mesma âncora, dizendo por qual pino
entra:

```
Branch (Condition = $Rotation Mode?)
  verdadeiro:
    Do Once  # ancora 2
      completo:
        Set Actor Location (New Location = $Home Location)
        # -> entra em `Do Once` pelo pino `Reset` (ancora 3)
  falso:
    Do Once  # ancora 3
```

Antes isso era seguido como se fosse continuação, e a leitura escrevia uma
cadeia que não existe — dois Do Once que se resetam saíam empilhados, um debaixo
do outro, como se um chamasse o outro. A ligação em si continua se perdendo ao
colar; o que mudou é ela aparecer, e no pino certo.

**Conversão de tipo.** Ligar um `Integer` num pino de `String` faz a Unreal
inserir um node de conversão. Ele não vira linha (o plugin o recria sozinho ao
refazer a ligação), mas fica marcado:

```
Append (A = "n = ", B = $Contador (Integer -> String))
```

Sem isso, um `Device Id` promovido para `int64` e um `Connection Id` que já era
`int64` escrevem exatamente a mesma linha — e um dos dois faz todo controle
colidir na mesma chave do mapa.

**Pino sem nome.** Quando o plugin não consegue nomear a saída de onde um valor
sai, ele escreve `$x.<pino desconhecido>` em vez de `$x`, que pareceria a saída
principal e ligaria em outro pino na volta.

### Pino que não aparece na linha

Um pino omitido está no **valor de fábrica dele** — nunca significa "não tem
nada ligado". `Set Is Enabled (Target = $X)` é o `bInIsEnabled` no padrão, que é
`false`. Valor apagado de propósito, que difere do padrão, sai explícito: `""`.

### O resto do que não é perfeito

| Situação | O que acontece |
|---|---|
| Cadeia de execução que reconverge | âncora nas duas pontas + aviso: a volta se perde ao colar |
| Fio que entra por pino lateral (`Reset`, `Stop`, `Close`) | âncora + o nome do pino, e aviso: a ligação se perde ao colar |
| Pino alimentado por node fora da seleção | aviso: o pino sai sem valor |
| Node que o plugin não sabe nomear de volta | sai o título do node + aviso de que pode não voltar igual |
| Valor com aspas dos dois tipos | aviso: não há escape, copie na mão |
| Node de dado que não alimenta ninguém | nota, com o nome de cada um |
| Evento ligado a dispatcher/delegate | aviso na leitura; comentário vermelho ao colar |
| Entrada de função (`Function Entry`) | aviso com a assinatura: crie a função e cole dentro dela |

Reroute (os pontinhos de organizar fio) some na volta — é layout, não lógica.
Get de variável do próprio Blueprint vira `$Nome` direto, sem linha própria.

---

## Quando não dá

Nada disso falha em silêncio:

| Situação | O que acontece |
|---|---|
| Nome de node não encontrado | comentário **vermelho** no grafo com a linha original |
| Evento que já existe no grafo | comentário vermelho; nada é criado nem alterado |
| Nome ambíguo | comentário vermelho listando os candidatos, nenhum escolhido |
| Pino inexistente | erro no painel listando os pinos que o node realmente tem |
| `$algo` que não existe | erro no painel |
| Tipos incompatíveis | node entra, ligação não; aviso no painel |
| Asset não decidido | pino vazio → Blueprint não compila |

Se algo entrou errado, `Ctrl+Z` desfaz tudo de uma vez.
