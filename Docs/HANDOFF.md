# Continuar: NodeScribe + locomoção da Sophia

Responda em português. C++ de plugin de Unreal Engine 5.8.

## Onde estamos

O suporte a **AnimGraph** no NodeScribe foi implementado e **validado no editor**:
pose, node por asset, leitura de volta, criação de asset de animação e
preenchimento de BlendSpace. A locomoção da MetaHuman Sophia está montada e
compilando.

Falta: **um crash ao fechar o editor**, e a **máquina de estados**, que foi
escrita e nunca exercitada.

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

### Fechar

`save_all_and_quit` pelo MCP — **mas veja a tarefa 1: ele está crashando.**

### MCP

Dois servidores, ambos em `.mcp.json` do projeto:

| servidor | porta | o que dá |
|---|---|---|
| `unreal-mcp` | 8000 | o toolset do NodeScribe (`write_graph`, `read_graph`, `clear_graph`, `create_asset`, `write_blendspace`, `read_object`, `write_object`, `save_all_and_quit`, `get_format_docs`) |
| `ai-game-developer` | 8080 | plugin UnrealMCP: ler assets, `blueprint-compile`, screenshots |

O `unreal-mcp` está em modo de descoberta: use `list_toolsets` /
`describe_toolset` / `call_tool`, com
`toolset_name = "nodescribe_toolset.toolsets.graph.NodeScribeTools"`.

O da **8080 não sobe junto com o editor** — inicie na mão:

```powershell
Start-Process "C:\Unreal Projects\Metahuman\Intermediate\UnrealMCP\server\win-x64\gamedev-mcp-server.exe" -ArgumentList "--port","8080" -WindowStyle Hidden
```

**As ferramentas MCP só entram no contexto quando o Claude Code inicia, e a
conexão só pega se o editor já estiver aberto.** Ordem certa: editor primeiro,
Claude depois. Se reiniciar o Claude com o editor fechado, nenhuma ferramenta
aparece — e nada avisa.

---

## Tarefa 1 — Crash ao fechar pelo `save_all_and_quit`

Recorrente. É a próxima coisa a consertar.

### O que o usuário observou

Crash Reporter ao fechar pelo plugin; ao reabrir, o editor ofereceu **recuperar
o Animation Blueprint**. A suspeita dele era que o editor fecha bruscamente sem
conseguir salvar.

### O que o log mostra — a suspeita está errada

Crash em
`C:\Unreal Projects\Metahuman\Saved\Crashes\UECC-Windows-CE8AFCBB4BE352324A21A8B5F71A517F_0000\`
(tem `Metahuman.log`, `CrashContext.runtime-xml`, `UEMinidump.dmp`).

**O save funcionou.** Sequência no log:

```
21:23:41.865  Dispatching toolset tool: ...save_all_and_quit
21:23:41.953  LogFileHelpers: Saving Package: /Game/Retarget/ABP_Sophia
21:23:41.961  LogSavePackage: Moving '...tmp' to '.../Content/Retarget/ABP_Sophia.uasset'
21:23:41.969  Cmd: QUIT_EDITOR
```

O `.uasset` foi gravado. O convite a "recuperar" veio do autosave das 21:23:13,
que a Unreal oferece depois de **qualquer** crash — não é prova de perda.

### Onde ele quebra

`EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000058`, e a pilha é
`AnimationBlueprintEditor` (×5) → `UnrealEd` (×5) → **`Slate`/`SlateCore`** (dezenas).

As últimas linhas do log, depois do `QUIT_EDITOR`:

```
LogSlate: Window 'Metahuman - Unreal Editor' being destroyed
LogBlueprint: Warning: Expected preview actor 'BP_ThirdPersonCharacter_C_0'
              to be garbage collected, but it was not:
LogReferenceChain: ... is not currently reachable.
```

Ou seja: **a janela principal morre primeiro, e só depois os editores de asset
abertos são desmontados** — com o mundo de preview deles apontando para coisa
já destruída. Havia dois abertos: o Animation Blueprint Editor (`ABP_Sophia`,
que tem cena de preview própria) e o Blueprint Editor
(`BP_ThirdPersonCharacter`, dono do preview actor da mensagem).

### Hipótese a testar primeiro

Fechar os editores de asset **antes** de enfileirar o `QUIT_EDITOR`, em
`UNodeScribeLibrary::SaveAllAndQuit`
(`Source/NodeScribeEditor/Private/NodeScribeLibrary.cpp`):

```cpp
if (UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
{
    Subsystem->CloseAllAssetEditors();
}
```

O `QUIT_EDITOR` já vai por `GEngine->DeferredCommands`, que roda no tick
seguinte — pode ser que um tick baste para os editores terminarem de morrer. Se
não bastar, adiar o quit por mais um tick.

**Reproduzir antes de consertar:** abra `ABP_Sophia` e
`BP_ThirdPersonCharacter` no editor, chame `save_all_and_quit`. Sem janela de
asset aberta o fechamento sempre funcionou nesta sessão — foi só com elas
abertas que quebrou.

Vale também rever se mexer num grafo que um editor de asset tem aberto (o
`clear_graph`/`write_graph` foram usados assim) deixa o painel do editor com
widget de node órfão. Não há evidência de que seja a causa, mas é o outro
ponto onde os dois lados se tocam.

---

## Tarefa 2 — Máquina de estados nunca foi testada

Está escrita, compila, e **nunca rodou**. É o único pedaço do AnimGraph sem
validação.

Sintaxe em `Docs/FORMATO.md`, seção **AnimGraph**:

```
Locomocao = State Machine
  estado Parado:
    MM_Idle
  estado Correndo:
    MF_Unarmed_Jog_Fwd
  Parado -> Correndo:
    Greater (A = $Ground Speed, B = 10.0)
  Correndo -> Parado:
    Less Equal (A = $Ground Speed, B = 10.0)
```

Conferir: os estados existem; o **Entry aponta para o primeiro declarado**; as
transições ligam os estados certos; a regra de cada uma chega no
`Can Enter Transition`; e o round-trip pelo `read_graph` volta igual.

Onde eu não confiaria sem ver:

- `FinalizeNode` chama `PostPlacedNewNode()` antes de `AllocateDefaultPins()`.
  É o que cria o `BoundGraph` de estado e de transição, e `CreateConnections`
  depende de `Pins[0]`/`Pins[1]` já existirem. Se estado ou transição vier
  vazio ou sem fio, é aí.
- `FBlueprintEditorUtils::RenameGraph` no `BoundGraph` de um estado: o nome do
  estado **é** o nome do grafo. Não sei o que a Engine faz com dois estados de
  máquinas diferentes com o mesmo nome.

Assets prontos para o teste, todos no esqueleto da Sophia, em
`/Game/Retarget/Animations/`: `MM_Idle`, `MM_Jump`, `MM_Fall_Loop`, `MM_Land`,
`MM_Dash`, e as 8 de Walk e 8 de Jog (`MF_Unarmed_*`).

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
