# Handoff: NodeScribe + Sophia's locomotion

Context for picking up an earlier round of work: AnimGraph support, tested in a
separate MetaHuman project. Unreal Engine 5.8 plugin C++.

## Contents

- [Where we are](#where-we-are)
- [Paths](#paths)
- [Operation](#operation)
- [Task 1 -- Crash on closing through `save_all_and_quit` (SOLVED)](#task-1----crash-on-closing-through-save_all_and_quit-solved)
- [Task 2 -- State machine: two bugs found, confirmation pending](#task-2----state-machine-two-bugs-found-confirmation-pending)
- [Task 3 -- Sophia's locomotion: what is missing](#task-3----sophias-locomotion-what-is-missing)
- [What was done in this round](#what-was-done-in-this-round-context-not-a-task)
- [Conventions](#conventions)

## Where we are

**AnimGraph** support in NodeScribe was implemented and **validated in the
editor**: poses, nodes by asset, reading back, animation asset creation and
BlendSpace filling. The MetaHuman Sophia's locomotion is assembled and
compiling.

The **crash when closing the editor** was fixed and exercised twice (task 1).

The **state machine** was exercised for the first time and had **two bugs**,
one on writing and one on reading. Both are fixed and compiling; the round
trip in the editor is still to be confirmed (task 2). Since then the automated
suite (`Tests/run_tests.py`) gained a `state_machine_round_trip` case with
states, a conduit, an alias and transition options, and it passes.

## Paths

| What | Where |
|---|---|
| Canonical repository | `C:\Unreal Projects\BossRush\Plugins\NodeScribe` (git, `origin` = github.com/uayten/NodeScribe, `main`) |
| Test project (UE 5.8, C++) | `C:\Unreal Projects\Metahuman` |
| Synced copy for compiling | `C:\Unreal Projects\Metahuman\Plugins\NodeScribe` |
| Engine | `E:\Program Files\Epic Games\UE_5.8` |

**Edit in BossRush** (it is the repo). Copy the changed files to the Metahuman
copy and compile there.

## Operation

### Compiling (editor closed)

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MetahumanEditor Win64 Development -Project="C:\Unreal Projects\Metahuman\Metahuman.uproject" -WaitMutex -NoUBA
```

**Do not trust the exit code.** Check `Result: Succeeded` AND the timestamp of
`Plugins/NodeScribe/Binaries/Win64/UnrealEditor-NodeScribeEditor.dll`.

### Opening the editor

```powershell
Start-Process "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Unreal Projects\Metahuman\Metahuman.uproject"'
```

It takes ~2 min. The MCP server starts by itself (`bAutoStartServer=True` in
`Config/DefaultEditorPerProjectUserSettings.ini`); wait for port **8000** to
listen.

### Recompiling **without** closing (prefer this)

The project has **Live Coding on**, and it can be triggered through the MCP:

```
console-run-command  LiveCoding.Compile
```

Edit, copy to the Metahuman copy, call that, and wait for
`LogLiveCoding: Display: Live coding succeeded` in the log (or the
`patch_N.lib` being created). It takes ~10 s and the editor does not even
blink. That is how task 2's bugs were hunted: four recompile cycles without a
single restart.

It cannot handle: new reflection (`UPROPERTY`/`UFUNCTION`/`UCLASS`), a new
file, a new module, a struct layout change. For those, close and use
`Build.bat`. And the patches pile up in memory -- a full rebuild now and then
does no harm.

### Closing

`save_all_and_quit` through the MCP.

### MCP

Two servers, both in the project's `.mcp.json`:

| server | port | what it gives |
|---|---|---|
| `unreal-mcp` | 8000 | the NodeScribe toolset (`write_graph`, `read_graph`, `clear_graph`, `create_asset`, `write_blendspace`, `read_object`, `write_object`, `save_all_and_quit`, `get_format_docs`) |
| `ai-game-developer` | 8080 | the UnrealMCP plugin: reading assets, `blueprint-compile`, screenshots |

`unreal-mcp` is in discovery mode: use `list_toolsets` / `describe_toolset` /
`call_tool`, with
`toolset_name = "nodescribe_toolset.toolsets.graph.NodeScribeTools"`.

**Port 8080 now starts together with the editor** -- but through a local patch,
not because the plugin does it. It is in
`Plugins/UnrealMCP/Source/UnrealMcpEditor/Private/UnrealMcpEditorCoordinator.cpp`,
in `ApplyServerLaunchArgsResult`, and it is one line:

```cpp
if (!ServerManager->ReattachIfRunning(Pending.Port, Args))
    ServerManager->Start(Pending.Port, Args);
```

The plugin says in its own code that it does not start by itself on purpose
("It does NOT auto-start — the user launches it from the MCP-server card's
Start button"), and has no configuration option for that. `ReattachIfRunning`
only *adopts* a surviving server and returns `false` when there is none --
which is exactly when `Start` is what is wanted.

**That patch belongs to the test project, not to the NodeScribe repo, and it
goes away if UnrealMCP is updated.** Check the launch log for
`[Unreal-MCP] spawned local server pid=... on port=8080`.

If it ever needs starting by hand:

```powershell
Start-Process "C:\Unreal Projects\Metahuman\Intermediate\UnrealMCP\server\win-x64\gamedev-mcp-server.exe" -ArgumentList "--port","8080" -WindowStyle Hidden
```

About reconnecting, there are **two** different failures, and it is worth not
mixing them up:

| when | what happens | what solves it |
|---|---|---|
| Claude Code starts with the editor **closed** | a server that fails at startup is discarded and never retried; no tool shows up, and nothing warns | restarting Claude with the editor already open |
| the editor restarts with Claude **already running** | the transport is HTTP: it usually reconnects by itself on the next call | nothing -- but if the server drops out of the context for good, then it is a restart |

Port 8080 looked like the second case and was the first in disguise:
`gamedev-mcp-server.exe` **did not start by itself**, so there was nothing to
reconnect. That was fixed -- see above.

**The practical rule stands: editor first, Claude afterwards.** And, once it is
up, recompile with Live Coding instead of reopening the editor.

---

## Task 1 -- Crash on closing through `save_all_and_quit` (SOLVED)

**Cause found, fixed in commit `785a138`, and exercised in the editor.**

### What it was

`SaveAllAndQuit` queued `QUIT_EDITOR`. That command lands in
`UUnrealEdEngine::CloseEditor` -> `RequestEngineExit` and **skips the whole
Slate shutdown**: it never calls `FMainFrameHandler::ShutDownEditor`, so it
never calls `GEditor->BroadcastEditorClose()`, which is what tells the
`UAssetEditorSubsystem` to close the open asset editors.

Result: the asset editors stayed alive until the engine loop exited, and were
only torn down **after** the main window had already died -- with the preview
scene pointing at destroyed things. Hence the `EXCEPTION_ACCESS_VIOLATION` in
`AnimationBlueprintEditor` in the middle of Slate's recursive widget
destruction.

The engine warns about it next to the command itself, in `EditorServer.cpp`
(`UEditorEngine::Exec`):

> QUIT_EDITOR - Closes the wx main editor frame. We need to do this in slate but
> it is routed differently. **Don't call quit_editor directly with slate**

And in `MainFrameHandler.cpp`, when queuing `QUIT_EDITOR` at the end of
`ShutDownEditor`: "Note this is the only place in slate that should be calling
QUIT_EDITOR".

### What changed

`SaveAllAndQuit` now queues **`CLOSE_SLATE_MAINFRAME`**, which goes to
`IMainFrameModule::RequestCloseEditor()` -> `CanCloseEditor()` ->
`ShutDownEditor()`. In the right order: it closes the asset editors, turns off
the autosave restore file, saves the window position, destroys the root window
-- and only then queues `QUIT_EDITOR` itself.

Two good side effects:

- `GetPackageAutoSaver().UpdateRestoreFile(false)` was what was missing for the
  editor to **stop offering "recover"** on the next launch. That offer was not
  data loss, it was the autosave not having been discarded.
- `SaveOpenAssetEditors(true)`: the asset editors that were open come back open
  in the next session.

A new preflight: `CLOSE_SLATE_MAINFRAME` opens a modal dialog ("Are you sure
you want to close the Unreal Editor?") when `bConfirmEditorClose` is on. What
calls this is a program, so the tool refuses first, with the instruction to
untick it. In that project it is already `False`.

### How it was tested

`ABP_Sophia` and `BP_ThirdPersonCharacter` open as asset editors -- the
condition where it broke --, and `save_all_and_quit`. It closed cleanly: no
Crash Reporter, no new folder in `Saved/Crashes`, log ending in `LogExit:
Exiting.`

The proof that the cause was the order is in the log. Before, the preview
scenes' `CleanupWorld` came **after** `Window 'Metahuman - Unreal Editor' being
destroyed`, followed by the warning `Expected preview actor
'BP_ThirdPersonCharacter_C_0' to be garbage collected, but it was not`. Now it
comes **before**, and the warning is gone:

```
15:334  Cmd: CLOSE_SLATE_MAINFRAME
15:510  UWorld::CleanupWorld for World_4 ... (the preview scenes)
15:622  LogSlate: Window 'Metahuman - Unreal Editor' being destroyed
15:774  Cmd: QUIT_EDITOR
```

If it ever breaks again, the next suspect is touching a graph that an asset
editor has open (`clear_graph`/`write_graph` were used that way), leaving an
orphan node widget in the panel. There is no evidence of that, but it is the
other point where the two sides touch.

---

## Task 2 -- State machine: two bugs found, confirmation pending

**Fixed in commit `dbfc48f`. The final round trip in the editor is pending.**

Exercised for the first time, on a throwaway asset (`/Game/Retarget/ABP_TesteSM`,
Sophia's skeleton -- **delete it when done**). What worked on the first try: the
machine is born with the `name =` name, both states exist with the right
content, the Entry points at the first one declared, and the transitions link
the right states. What broke was **the rule**, on both ends.

### Bug 1 -- on writing: the last node created is not the block's result

`BuildStateMachine` looked for the rule's result by sweeping
`BoundGraph->Nodes` backwards. But **a node's argument is born after it**: in
`Greater (A = $Ground Speed, B = 10.0)` the last node created is the
`Get Ground Speed`, not the comparison that consumes it. A Float was linked into
a Boolean pin, and the only sign was a wrong-type warning pointing at the wrong
line:

```
line 6 [warning]: `Ground Speed` is Float (double-precision), and pin
                  `bCanEnterTransition` expects Boolean. The link was not made.
```

What knows which node is a block's result is the block's walk, not the order
in which the nodes landed in the graph. `BuildSubGraph` now returns
`Nested.Frames[0].LastNode` -- the node of the last line, even when that line
is a pure node (which does not enter the flow chain and so does not show up in
`PendingExec`).

### Bug 2 -- on reading: the rule graph is not an animation graph

`IsAnimationGraph()` asks the schema. And:

```cpp
class UAnimationTransitionSchema : public UEdGraphSchema_K2
```

It does not descend from `UAnimationGraphSchema` -- which is consistent, because
the rule has no pose: it is a data chain ending in a bool. Result:
`EmitTransitionRule()` was never called. The transition came back **empty even
with the rule linked in the graph**, and its nodes still showed up in the
orphan note. The comment inside `IsAnimationGraph` claimed exactly the opposite
("the schema covers ... the transition rule at once") and was fixed along with
it.

### How the two hid behind each other

Worth knowing, because it cost a lot: after fixing the write, the reading kept
showing the transition empty -- and it is tempting to conclude the write was not
fixed. **Using the reader to test the reader is circular.** What broke the tie
was a temporary probe reading the pin directly, around
`MarkBlueprintAsStructurallyModified`:

```
[PROBE before] ...AnimStateTransitionNode_0.Transition : links=1 nodes=3
[PROBE after]  ...AnimStateTransitionNode_0.Transition : links=1 nodes=3
```

`links=1` on both ends: the write was right since the first fix.

### What is missing

1. `read_graph` on `ABP_TesteSM` and check that the rule comes back as a line
   under `Idle -> Running:`, and that the orphan note is gone.
2. Paste that text back with `write_graph` and compare -- it is the round trip
   the task asked for.
3. `blueprint-compile` without errors.
4. **Delete `/Game/Retarget/ABP_TesteSM`.**

### Two things that came up on the side

- **The format doc's example did not work as written.** `Greater` and
  `Less Equal` are ambiguous (`Greater_DoubleDouble`, `GreaterEqual_IntInt`,
  `GreaterGreater_VectorRotator`, ...), and the plugin refuses both asking for
  the exact name. **Fixed:** the examples in `FORMAT.md` now use
  `KismetMathLibrary.Greater_DoubleDouble`, which is what the reading writes,
  and the test suite's state machine case uses it.
- **`clear_graph` seemed to leave `BoundGraph` orphaned.** After a few
  write/clear cycles, `obj list class=AnimationTransitionGraph` showed seven
  graphs hanging directly off the asset
  (`/Game/Retarget/ABP_TesteSM.AnimationTransitionGraph_3`), outside any node.
  The likely explanation: `FBlueprintEditorUtils::RemoveGraph` renames a removed
  graph into the package so it can be garbage collected, and `obj list` shows
  it until the next collection. It does not get in the way of anything visible.
  Not investigated further.

---

## Task 3 -- Sophia's locomotion: what is missing

Assembled and compiling (0 errors, 0 warnings):

| asset | what |
|---|---|
| `/Game/Retarget/BS_Sophia_Locomotion` | 1D BlendSpace, axis `Speed 0..600`, samples `MM_Idle` 0 / `MF_Unarmed_Walk_Fwd` 300 / `MF_Unarmed_Jog_Fwd` 600 |
| `/Game/Retarget/ABP_Sophia` | AnimGraph: `BS_Sophia_Locomotion (Speed = $Ground Speed)`. The Event Graph computes `Ground Speed` in `BlueprintUpdateAnimation` |
| `BP_ThirdPersonCharacter` | Mesh = `SKM_Sophia_BodyMesh`, Anim Class = `ABP_Sophia_C` |

Skeleton of everything:
`/Game/MetaHumans/Common/Female/Medium/NormalWeight/Body/metahuman_base_skel`

Pending:

1. **Play was never pressed.** First thing to do.
2. **Mesh Z at `-89`** -- Manny's height. Sophia is shorter; expect sunken or
   floating feet, and adjust.
3. **Only the body.** Face, hair and clothes are separate components of
   `BP_Sophia` and were not hooked up.
4. **No jump or fall.** The AnimGraph is a single line. `MM_Jump`,
   `MM_Fall_Loop` and `MM_Land` are already retargeted and waiting -- it is the
   natural use case for task 2.
5. The BlendSpace is 1D on purpose: the character uses `Orient Rotation to
   Movement`, so `Direction` is always ~0 and a 2D blendspace would waste 24 of
   the 27 samples.

---

## What was done in this round (context, not a task)

Commits on `main`:

| commit | what |
|---|---|
| `9636df6` | AnimGraph base: schema, pose pins, class and asset indexes |
| `89de623` | writing: poses, assets, state machine |
| `b488217` | reading back |
| `563ee51` | format doc, AnimGraph section |
| `10fe368` | anim vocabulary only where there is a pose |
| `40e4c2a` | declaring `ModelContextProtocol` as a `.uplugin` dependency |
| `99e5afb` | `create_asset` with `options` (factory properties) and `write_blendspace` |
| `0fbdfa5` | `clear_graph`, and refusing properties the Engine retired |

Decisions worth knowing:

- **Flow, not execution.** `IsFlowPin`/`FindFlowInput`/`GetFlowOutputs` in the
  builder return an execution pin in the EventGraph and a pose pin in the
  AnimGraph. The walk is the same.
- **In the AnimGraph indentation opens a pose *input***, not an output. Each
  node of the block links over the previous one on the parent's pin, so the
  last one remains.
- **The schema became the graph's own**, not the generic `UEdGraphSchema_K2`.
  That changed for *every* graph -- it is where to look if something in the
  EventGraph regresses.
- **`create_asset` accepts `options`**, written into the *factory* before
  creating. That is what unlocked AnimBlueprint and BlendSpace: on the finished
  asset the skeleton is read-only.
- **`clear_graph` is not a forced mode.** It returns the transcribed graph in
  the response, with the reading's warnings. `write_graph`'s `replace` keeps
  refusing what the text cannot describe.

---

## Conventions

- **Read `Docs/FORMAT.md` first** before writing or interpreting any graph.
- Everything in English: identifiers, comments, messages, documentation.
  Comments explain **why**, not what, and plain ASCII in the source files.
  Follow the tone of the neighbouring files.
- An error is never silent: a name not found becomes a red comment in the graph;
  ambiguity lists candidates and does not choose.
- **Git: do not create a new branch.** Commit straight to `main`. Do not `push`
  without an explicit request.
