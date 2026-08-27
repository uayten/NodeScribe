# Continuar: suporte a AnimGraph no NodeScribe

Responda em português. Este é um trabalho de C++ em um plugin de Unreal Engine.

## Estado: escrito e compilando, **nada testado no editor**

Os seis passos do plano original foram implementados e compilam limpo. O que
falta é a única coisa que este documento não consegue dar: rodar.

## Caminhos

| O quê | Onde |
|---|---|
| Repositório canônico | `C:\Unreal Projects\BossRush\Plugins\NodeScribe` (git, `origin` = github.com/uayten/NodeScribe, branch `main`) |
| Projeto de teste (UE 5.8, C++) | `C:\Unreal Projects\Metahuman` |
| Cópia sincronizada para compilar | `C:\Unreal Projects\Metahuman\Plugins\NodeScribe` |
| Engine | `E:\Program Files\Epic Games\UE_5.8` |

**Edite no BossRush** (é o repo). Depois copie os arquivos alterados para a
cópia do Metahuman e compile lá.

## Compilar

O editor **precisa estar fechado** (segura as DLLs).

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MetahumanEditor Win64 Development -Project="C:\Unreal Projects\Metahuman\Metahuman.uproject" -WaitMutex
```

**Não confie no exit code.** Verifique `Result: Succeeded` no log E o timestamp
de `Plugins/NodeScribe/Binaries/Win64/UnrealEditor-NodeScribeEditor.dll`.

## O que foi feito

Três commits em `main`, não pushados:

| Commit | O quê |
|---|---|
| `9636df6` | base: schema, pinos de pose, índice de classes de node e de assets |
| `89de623` | escrita: pose, asset, máquina de estados |
| `b488217` | leitura: pose, máquina de estados, regra de transição |

Documentado em `Docs/FORMATO.md`, seção **AnimGraph**.

### As decisões que valem saber

- **Fluxo, não execução.** `FindFlowInput` / `GetFlowOutputs` /`IsFlowPin` no
  builder devolvem pino de execução no EventGraph e de pose no AnimGraph. O
  percurso é o mesmo; só muda qual pino carrega o fluxo.
- **Indentação abre uma *entrada* de pose**, não uma saída. Cada node do bloco
  liga por cima do anterior no pino do pai (quebrando o fio antes), então sobra
  o último — que é o resultado do bloco. Sem guardar ligação para o fim.
- **O schema agora é o do próprio grafo**, não o `UEdGraphSchema_K2` genérico.
  É o `UAnimationGraphSchema` que sabe ligar pose e inserir a conversão entre
  espaço local e de componente. *Isso mudou para todos os grafos* — para
  EventGraph e função o schema do grafo já era o K2, então deveria ser no-op,
  mas é o ponto onde procurar se algo do EventGraph regredir.
- **Layout de pose é uma árvore**, posicionada no fim a partir do Output Pose,
  andando para trás (`LayoutAnimNodes`). O layout de coluna não serve.
- **Sub-grafo tem contexto próprio**, na escrita e na leitura. Estado e
  transição não vivem no grafo de cima.
- **Output Pose e Transition Result não se criam e não viram linha na leitura.**

## O que falta: testar

Nada disto foi exercitado. A ordem abaixo vai do mais simples ao mais
arriscado; pare no primeiro que quebrar.

Abrir um AnimBlueprint no Metahuman, com o AnimGraph em foco, e usar **Colar**
na barra do editor de Blueprint.

1. **Uma linha, um asset.** `Idle_Parado` (trocar por uma AnimSequence que
   exista no projeto). Esperado: um Sequence Player com o asset, ligado no
   Output Pose sozinho.
2. **Cadeia reta.** Duas linhas, a segunda consumindo a primeira.
3. **Blend com rótulos.** O exemplo do `FORMATO.md`, seção AnimGraph.
4. **Round-trip.** *Copiar grafo inteiro* no que acabou de entrar, e conferir
   se o texto que sai é o mesmo que entrou.
5. **Máquina de estados.** O exemplo do `FORMATO.md`. Conferir: os estados
   existem, o Entry aponta para o primeiro, as transições ligam os certos, e a
   regra de cada uma chega no `Can Enter Transition`.
6. **Round-trip da máquina de estados.**

### Pontos onde eu não confiaria sem ver

- `FinalizeNode` chama `PostPlacedNewNode()` **antes** de `AllocateDefaultPins()`.
  Para `UAnimStateNode` e `UAnimStateTransitionNode` isso é o que cria o
  `BoundGraph`, e `CreateConnections` depende de `Pins[0]`/`Pins[1]` já
  existirem. A ordem parece certa, mas é a primeira coisa a checar se estado ou
  transição vier vazio ou sem fio.
- `FBlueprintEditorUtils::RenameGraph` no `BoundGraph` de um estado: o nome do
  estado é o nome do grafo. Se dois estados de máquinas diferentes tiverem o
  mesmo nome, não sei o que a Engine faz.
- **Saída de pose com mais de um destino.** Assumi que pode acontecer e quebro o
  fio do sink antes de ligar. Se a Engine já proibir, o `BreakAllPinLinks` é
  inofensivo; se permitir, ele é o que evita o node anterior alimentar dois
  lugares.
- A leitura de uma regra de transição usa `EmitDataNode`, que sempre escreve
  `nome = ...`. Funciona, mas talvez saia mais verboso que o necessário.

## Convenções do projeto

- **Leia `Docs/FORMATO.md` antes** de escrever ou interpretar qualquer grafo.
- Comentários em português, sem acentos, explicando **por quê**, não o quê.
- Erro nunca é silencioso: nome não encontrado vira comentário vermelho no
  grafo; ambiguidade lista candidatos e não escolhe.
- **Git: não criar branch nova.** Commitar direto na `main`. Não dar `push` sem
  pedido explícito.
