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

## Eventos

```
evento BeginPlay
evento MinhaHabilidadeAtivou
```

Se o nome existir na classe pai, vira o evento de override. Se não existir,
vira um Custom Event com esse nome (e o plugin avisa que fez isso).

Se o nome for um evento **que existe na Engine, mas não nesta classe** — o caso
clássico é `BeginPlay` num Widget Blueprint, que só tem `Construct` — o aviso
sobe para **amarelo** e lista os eventos que a classe pai realmente oferece.
Um Custom Event chamado `BeginPlay` compila, parece certo no grafo, e nunca
dispara; é o tipo de erro que só aparece em playtest.

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

## Structs

```
args = Make MapPlayerKeyArgs (Mapping Name = $Nome, Slot = First)
Break Vector (In Vec = $posicao)
```

Aceita o nome interno (`MapPlayerKeyArgs`) ou o de exibição
(`Map Player Key Args`). Só vira node de struct se a struct existir — assim
`Make Literal Int` continua sendo a função que sempre foi.

## Subsistemas

```
settings = Get EnhancedInputLocalPlayerSubsystem (PlayerController = $pc)
```

O plugin escolhe o node certo conforme onde o subsistema vive: os de
LocalPlayer pedem um PlayerController, os de Engine não pedem nada.

## Macros padrão

Nomes da biblioteca da Engine funcionam direto:

```
For Each Loop (Array = $Inimigos)
  corpo:
    Print String (In String = "um inimigo")
```

Também: `Do Once`, `Flip Flop`, `Gate`, `While Loop`, `Multi Gate`, `Is Valid`.

## Buracos: o que o plugin **não** decide por você

Escreva `?` num pino cujo valor é uma escolha sua:

```
Spawn Sound 2D (Sound = ?)
```

O node entra, o pino fica vazio, e o Blueprint não compila até você escolher.
Isso é intencional: um asset chutado passa despercebido e vira bug de playtest.

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
marcadores `-` e `*`, numeração `1.` / `1)`.

---

## O caminho de volta

**Copiar selecionado** e **Copiar grafo inteiro** produzem texto neste mesmo
formato, pronto para colar num chat e editar.

A tradução não é perfeita, e onde ela não é o plugin fala:

| Situação | O que acontece |
|---|---|
| Cadeia de execução que reconverge | aviso: o formato é uma árvore, essa volta se perde |
| Pino alimentado por node fora da seleção | aviso: o pino sai sem valor |
| Node que o plugin não sabe nomear de volta | sai o título do node + aviso de que pode não voltar igual |
| Valor com aspas dos dois tipos | aviso: não há escape, copie na mão |
| Node de dado que não alimenta ninguém | nota: ficou de fora |
| Evento ligado a dispatcher/delegate | aviso: o vínculo se perde, recrie na mão |

Reroute (os pontinhos de organizar fio) some na volta — é layout, não lógica.
Get de variável do próprio Blueprint vira `$Nome` direto, sem linha própria.

---

## Quando não dá

Nada disso falha em silêncio:

| Situação | O que acontece |
|---|---|
| Nome de node não encontrado | comentário **vermelho** no grafo com a linha original |
| Nome ambíguo | comentário vermelho listando os candidatos, nenhum escolhido |
| Pino inexistente | erro no painel listando os pinos que o node realmente tem |
| `$algo` que não existe | erro no painel |
| Tipos incompatíveis | node entra, ligação não; aviso no painel |
| Asset não decidido | pino vazio → Blueprint não compila |

Se algo entrou errado, `Ctrl+Z` desfaz tudo de uma vez.
