# Continuar: NodeScribe + locomoção da Sophia

Responda em português. C++ de plugin de Unreal Engine 5.8.

## Onde estamos

O suporte a **AnimGraph** no NodeScribe foi implementado e **validado no editor**:
pose, node por asset, leitura de volta, criação de asset de animação e
preenchimento de BlendSpace. A locomoção da MetaHuman Sophia está montada e
compilando.

O **crash ao fechar o editor** foi corrigido e exercitado duas vezes (tarefa 1).

A **máquina de estados** foi exercitada pela primeira vez e tinha **dois bugs**,
um na escrita e um na leitura. Os dois estão corrigidos e compilando; falta
confirmar o round-trip no editor (tarefa 2).

## Caminhos

| O quê | Onde |
|---|---|
| Repositório canônico | `C:\Unreal Projects\BossRush\Plugins\NodeScribe` (git, `origin` = github.com/uayten/NodeScribe, `main`) |
| Projeto de teste (UE 5.8, C++) | `C:\Unreal Projects\Metahuman` |
| Cópia sincronizada para compilar | `C:\Unreal Projects\Metahuman\Plugins\NodeScribe` |
| Engine | `E:\Program Files\Epic Games\UE_5.8` |

**Edite no BossRush** (é o repo). Copie os arquivos alterados para a cópia do
Metahuman e compile lá.

## Operação

### Compilar (editor fechado)

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MetahumanEditor Win64 Development -Project="C:\Unreal Projects\Metahuman\Metahuman.uproject" -WaitMutex
```

**Não confie no exit code.** Confira `Result: Succeeded` E o timestamp de
`Plugins/NodeScribe/Binaries/Win64/UnrealEditor-NodeScribeEditor.dll`.

### Abrir o editor

```powershell
Start-Process "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Unreal Projects\Metahuman\Metahuman.uproject"'
```

Leva ~2 min. O servidor MCP sobe sozinho (`bAutoStartServer=True` em
`Config/DefaultEditorPerProjectUserSettings.ini`); espere a porta **8000**
escutar.

### Recompilar **sem** fechar (prefira isto)

O projeto tem **Live Coding ligado**, e ele se dispara pelo MCP:

```
console-run-command  LiveCoding.Compile
```

Edite, copie para a cópia do Metahuman, chame isso, e espere no log
`LogLiveCoding: Display: Live coding succeeded` (ou o `patch_N.lib` sendo
criado). Leva ~10 s e o editor nem pisca. Foi assim que os bugs da tarefa 2
foram caçados: quatro ciclos de recompilação sem nenhum restart.

Não dá conta de: reflexão nova (`UPROPERTY`/`UFUNCTION`/`UCLASS`), arquivo novo,
módulo novo, mudança de layout de struct. Nesses, feche e use o `Build.bat`. E
os patches acumulam na memória — um rebuild completo de vez em quando não faz
mal.

### Fechar

`save_all_and_quit` pelo MCP.

### MCP

Dois servidores, ambos em `.mcp.json` do projeto:

| servidor | porta | o que dá |
|---|---|---|
| `unreal-mcp` | 8000 | o toolset do NodeScribe (`write_graph`, `read_graph`, `clear_graph`, `create_asset`, `write_blendspace`, `read_object`, `write_object`, `save_all_and_quit`, `get_format_docs`) |
| `ai-game-developer` | 8080 | plugin UnrealMCP: ler assets, `blueprint-compile`, screenshots |

O `unreal-mcp` está em modo de descoberta: use `list_toolsets` /
`describe_toolset` / `call_tool`, com
`toolset_name = "nodescribe_toolset.toolsets.graph.NodeScribeTools"`.

A **8080 agora sobe junto com o editor** — mas por um patch local, não porque o
plugin faça isso. Está em
`Plugins/UnrealMCP/Source/UnrealMcpEditor/Private/UnrealMcpEditorCoordinator.cpp`,
em `ApplyServerLaunchArgsResult`, e é uma linha:

```cpp
if (!ServerManager->ReattachIfRunning(Pending.Port, Args))
    ServerManager->Start(Pending.Port, Args);
```

O plugin diz no próprio código que não sobe sozinho de propósito ("It does NOT
auto-start — the user launches it from the MCP-server card's Start button"), e
não tem opção de configuração para isso. O `ReattachIfRunning` só *adota* um
servidor sobrevivente e devolve `false` quando não há nenhum — que é exatamente
quando `Start` é o que se queria.

**Esse patch é do projeto de teste, não do repo do NodeScribe, e some se o
UnrealMCP for atualizado.** Confira no log da abertura:
`[Unreal-MCP] spawned local server pid=... on port=8080`.

Se algum dia precisar subir na mão:

```powershell
Start-Process "C:\Unreal Projects\Metahuman\Intermediate\UnrealMCP\server\win-x64\gamedev-mcp-server.exe" -ArgumentList "--port","8080" -WindowStyle Hidden
```

Sobre reconexão, são **duas** falhas diferentes, e vale não confundi-las:

| quando | o que acontece | o que resolve |
|---|---|---|
| Claude Code inicia com o editor **fechado** | servidor que falha na largada é descartado e nunca mais tentado; nenhuma ferramenta aparece, e nada avisa | reiniciar o Claude com o editor já aberto |
| editor reinicia com o Claude **já rodando** | o transporte é HTTP: costuma religar sozinho na chamada seguinte | nada — mas se o servidor cair de vez do contexto, aí sim é restart |

A 8080 parecia ser o segundo caso e era o primeiro disfarçado: o
`gamedev-mcp-server.exe` **não subia sozinho**, então não havia nada para
religar. Isso foi corrigido — veja abaixo.

**A regra prática continua: editor primeiro, Claude depois.** E, uma vez de pé,
recompile com Live Coding em vez de reabrir o editor.

---

## Tarefa 1 — Crash ao fechar pelo `save_all_and_quit` (RESOLVIDO)

**Causa achada, corrigida no commit `785a138`, e exercitada no editor.**

### O que era

`SaveAllAndQuit` enfileirava `QUIT_EDITOR`. Esse comando cai em
`UUnrealEdEngine::CloseEditor` -> `RequestEngineExit` e **pula o desligamento do
Slate inteiro**: nunca chama `FMainFrameHandler::ShutDownEditor`, logo nunca
chama `GEditor->BroadcastEditorClose()`, que e' quem manda o
`UAssetEditorSubsystem` fechar os editores de asset abertos.

Resultado: os editores de asset ficavam vivos ate' o engine loop sair, e so'
eram desmontados **depois** que a janela principal ja' tinha morrido — com a
cena de preview apontando para coisa destruida. Dai' o
`EXCEPTION_ACCESS_VIOLATION` em `AnimationBlueprintEditor` no meio da destruicao
recursiva de widgets do Slate.

A engine avisa disso ao lado do proprio comando, em `EditorServer.cpp`
(`UEditorEngine::Exec`):

> QUIT_EDITOR - Closes the wx main editor frame. We need to do this in slate but
> it is routed differently. **Don't call quit_editor directly with slate**

E em `MainFrameHandler.cpp`, ao enfileirar o `QUIT_EDITOR` no fim do
`ShutDownEditor`: "Note this is the only place in slate that should be calling
QUIT_EDITOR".

### O que mudou

`SaveAllAndQuit` agora enfileira **`CLOSE_SLATE_MAINFRAME`**, que vai em
`IMainFrameModule::RequestCloseEditor()` -> `CanCloseEditor()` ->
`ShutDownEditor()`. Na ordem certa: fecha os editores de asset, desliga o
arquivo de restauracao do autosave, salva a posicao da janela, destroi a janela
raiz — e so' entao enfileira o `QUIT_EDITOR` ele mesmo.

Dois efeitos colaterais bons:

- `GetPackageAutoSaver().UpdateRestoreFile(false)` e' o que faltava para o editor
  **parar de oferecer "recuperar"** na abertura seguinte. Aquele convite nao era
  perda de dado, era o autosave nao ter sido descartado.
- `SaveOpenAssetEditors(true)`: os editores de asset que estavam abertos voltam a
  abrir na proxima sessao.

Um preflight novo: `CLOSE_SLATE_MAINFRAME` abre um dialogo modal ("Are you sure
you want to close the Unreal Editor?") quando `bConfirmEditorClose` esta'
ligado. Quem chama isto e' um programa, entao o tool recusa antes, com a
instrucao de desmarcar. Neste projeto ja' esta' `False`.

### Como foi testado

`ABP_Sophia` e `BP_ThirdPersonCharacter` abertos como editores de asset —
a condicao em que quebrava —, e `save_all_and_quit`. Fechou limpo: sem Crash
Reporter, sem pasta nova em `Saved/Crashes`, log terminando em `LogExit:
Exiting.`

A prova de que a causa era a ordem esta' no log. Antes, o `CleanupWorld` das
cenas de preview vinha **depois** de `Window 'Metahuman - Unreal Editor' being
destroyed`, seguido do aviso `Expected preview actor 'BP_ThirdPersonCharacter_C_0'
to be garbage collected, but it was not`. Agora vem **antes**, e o aviso sumiu:

```
15:334  Cmd: CLOSE_SLATE_MAINFRAME
15:510  UWorld::CleanupWorld for World_4 ... (as cenas de preview)
15:622  LogSlate: Window 'Metahuman - Unreal Editor' being destroyed
15:774  Cmd: QUIT_EDITOR
```

Se algum dia voltar a quebrar, o proximo suspeito e' mexer num grafo que um
editor de asset tem aberto (`clear_graph`/`write_graph` foram usados assim),
deixando widget de node orfao no painel. Nao ha' evidencia disso, mas e' o outro
ponto onde os dois lados se tocam.

---

## Tarefa 2 — Máquina de estados: dois bugs achados, falta confirmar

**Corrigidos no commit `dbfc48f`. Falta o round-trip final no editor.**

Exercitada pela primeira vez, num asset descartável
(`/Game/Retarget/ABP_TesteSM`, esqueleto da Sophia — **apagar quando terminar**).
O que funcionou de primeira: a máquina nasce com o nome do `nome =`, os dois
estados existem com o conteúdo certo, o Entry aponta para o primeiro declarado,
e as transições ligam os estados certos. O que quebrou foi **a regra**, nas duas
pontas.

### Bug 1 — na escrita: o último node criado não é o resultado do bloco

`BuildStateMachine` procurava o resultado da regra varrendo
`BoundGraph->Nodes` de trás para frente. Mas **o argumento de um node nasce
depois dele**: em `Greater (A = $Ground Speed, B = 10.0)` o último node criado é
o `Get Ground Speed`, não a comparação que o consome. Ligava-se um Float num
pino Boolean, e o único sinal era um aviso de tipo trocado apontando para a
linha errada:

```
linha 6 [aviso]: `Ground Speed` e' Float (double-precision), e o pino
                 `bCanEnterTransition` espera Boolean. A ligacao nao foi feita.
```

Quem sabe qual é o resultado de um bloco é o percurso dele, não a ordem em que
os nodes caíram no grafo. `BuildSubGraph` passou a devolver
`Nested.Frames[0].LastNode` — o node da última linha, inclusive quando essa
linha é um node puro (que não entra na cadeia de fluxo e por isso não aparece no
`PendingExec`).

### Bug 2 — na leitura: o grafo de regra não é um grafo de animação

`IsAnimationGraph()` pergunta ao schema. E:

```cpp
class UAnimationTransitionSchema : public UEdGraphSchema_K2
```

Não desce de `UAnimationGraphSchema` — o que é coerente, porque a regra não tem
pose: é uma cadeia de dado terminando num bool. Resultado: `EmitTransitionRule()`
nunca era chamado. A transição voltava **vazia mesmo com a regra ligada no
grafo**, e os nodes dela ainda apareciam na nota de órfãos. O comentário dentro
de `IsAnimationGraph` afirmava justamente o contrário ("o schema cobre ... regra
de transição de uma vez") e foi corrigido junto.

### Como esses dois se esconderam um atrás do outro

Vale saber, porque custou caro: depois de corrigir a escrita, a leitura
continuava mostrando a transição vazia — e é tentador concluir que a escrita não
foi corrigida. **Usar o leitor para testar o leitor é circular.** O que
desempatou foi uma sonda temporária lendo o pino direto, em volta do
`MarkBlueprintAsStructurallyModified`:

```
[SONDA antes]  ...AnimStateTransitionNode_0.Transition : links=1 nodes=3
[SONDA depois] ...AnimStateTransitionNode_0.Transition : links=1 nodes=3
```

`links=1` nas duas pontas: a escrita estava certa desde a primeira correção.

### O que falta

1. `read_graph` no `ABP_TesteSM` e conferir que a regra volta como linha debaixo
   de `Parado -> Correndo:`, e que a nota de órfãos sumiu.
2. Colar esse texto de volta com `write_graph` e comparar — é o round-trip que a
   tarefa pedia.
3. `blueprint-compile` sem erro.
4. **Apagar o `/Game/Retarget/ABP_TesteSM`.**

### Duas coisas que apareceram de lado

- **O exemplo do `FORMATO.md` não funciona como está escrito.** `Greater` e
  `Less Equal` são ambíguos (`Greater_DoubleDouble`, `GreaterEqual_IntInt`,
  `GreaterGreater_VectorRotator`, ...), e o plugin recusa os dois pedindo o nome
  exato. O texto do teste que funciona usa `KismetMathLibrary.Greater_DoubleDouble`.
  Confira o que a leitura corrigida escreve (provavelmente `float > float`) e
  ajuste o exemplo da seção **Máquina de estados** para algo que role.
- **`clear_graph` deixa `BoundGraph` órfão.** Depois de alguns ciclos de
  escrever/limpar, `obj list class=AnimationTransitionGraph` mostrava sete
  grafos pendurados direto no asset
  (`/Game/Retarget/ABP_TesteSM.AnimationTransitionGraph_3`), fora de qualquer
  node. `RemoveDeletableNodes` apaga o node da máquina mas não o sub-grafo dele.
  Não atrapalha nada visível — infla o asset e polui `obj list`. Não foi
  investigado.

---

## Tarefa 3 — Locomoção da Sophia: o que falta

Montado e compilando (0 erros, 0 avisos):

| asset | o quê |
|---|---|
| `/Game/Retarget/BS_Sophia_Locomotion` | BlendSpace 1D, eixo `Speed 0..600`, samples `MM_Idle` 0 / `MF_Unarmed_Walk_Fwd` 300 / `MF_Unarmed_Jog_Fwd` 600 |
| `/Game/Retarget/ABP_Sophia` | AnimGraph: `BS_Sophia_Locomotion (Speed = $Ground Speed)`. Event Graph calcula `Ground Speed` no `BlueprintUpdateAnimation` |
| `BP_ThirdPersonCharacter` | Mesh = `SKM_Sophia_BodyMesh`, Anim Class = `ABP_Sophia_C` |

Esqueleto de tudo:
`/Game/MetaHumans/Common/Female/Medium/NormalWeight/Body/metahuman_base_skel`

Pendências:

1. **Nunca foi dado Play.** Primeira coisa a fazer.
2. **Z do Mesh em `-89`** — altura do Manny. A Sophia é mais baixa; espere pé
   afundado ou flutuando, e ajuste.
3. **Só o corpo.** Rosto, cabelo e roupa são componentes separados do
   `BP_Sophia` e não foram ligados.
4. **Sem pulo nem queda.** O AnimGraph é uma linha só. `MM_Jump`,
   `MM_Fall_Loop` e `MM_Land` já estão retargetados e esperando — é o caso de
   uso natural da tarefa 2.
5. O BlendSpace é 1D de propósito: o personagem usa `Orient Rotation to
   Movement`, então `Direction` é sempre ~0 e um blendspace 2D desperdiçaria 24
   dos 27 samples.

---

## O que foi feito nesta rodada (contexto, não tarefa)

Cinco commits em `main`, **não pushados**:

| commit | o quê |
|---|---|
| `9636df6` | base de AnimGraph: schema, pinos de pose, índice de classes e de assets |
| `89de623` | escrita: pose, asset, máquina de estados |
| `b488217` | leitura de volta |
| `563ee51` | `FORMATO.md`, seção AnimGraph |
| `10fe368` | vocabulário de anim só onde há pose |
| `40e4c2a` | declarar `ModelContextProtocol` como dependência do `.uplugin` |
| `99e5afb` | `create_asset` com `options` (propriedades da factory) e `write_blendspace` |
| `0fbdfa5` | `clear_graph`, e recusar propriedade que a Engine aposentou |

Decisões que valem saber:

- **Fluxo, não execução.** `IsFlowPin`/`FindFlowInput`/`GetFlowOutputs` no
  builder devolvem pino de execução no EventGraph e de pose no AnimGraph. O
  percurso é o mesmo.
- **No AnimGraph a indentação abre uma *entrada* de pose**, não uma saída. Cada
  node do bloco liga por cima do anterior no pino do pai, então sobra o último.
- **O schema passou a ser o do próprio grafo**, não o `UEdGraphSchema_K2`
  genérico. Isso mudou para *todos* os grafos — é onde procurar se algo do
  EventGraph regredir.
- **`create_asset` aceita `options`**, escritas na *factory* antes de criar. Foi
  o que destravou AnimBlueprint e BlendSpace: no asset pronto o esqueleto é
  somente-leitura.
- **`clear_graph` não é modo forçado.** Ele devolve o grafo transcrito na
  resposta, com os avisos da leitura. O `substituir` do `write_graph` continua
  recusando o que o texto não sabe descrever.

---

## Convenções

- **Leia `Docs/FORMATO.md` antes** de escrever ou interpretar qualquer grafo.
- Comentários em português, **sem acentos**, explicando **por quê**, não o quê.
  Siga o tom dos arquivos vizinhos.
- Erro nunca é silencioso: nome não encontrado vira comentário vermelho no
  grafo; ambiguidade lista candidatos e não escolhe.
- **Git: não criar branch nova.** Commitar direto na `main`. Não dar `push` sem
  pedido explícito.
