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

No `Make`, cada pino tem o nome do campo. No `Break` há um pino de entrada só,
e ele se chama como a struct (`Vector`) — por isso a forma sem nome, por
posição, é a que se lê melhor.

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

### Três coisas que a leitura escreve e o parser descarta

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
