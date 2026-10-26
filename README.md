# NodeScribe

Unreal Engine editor plugin that turns plain text into Blueprint nodes and
back. With it you can:

- **Paste a node list written as text** and get real nodes in the Blueprint
  graph -- positioned and linked.
- **Copy a graph, or a selection, as text** that pastes back the same.
- **Write AnimGraphs and state machines** -- poses, blends, states, conduits,
  aliases and transition rules -- from the same text.
- **Read and change any object's properties as a sheet** that shows only what
  differs from the default, including Blueprint variables and components.
- **Read and write Blackboards, Behavior Trees and a Widget Blueprint's
  Designer** as text.
- **Fill BlendSpaces**, **create Gameplay Tags** and **create empty assets**.
- **Let an AI assistant do all of the above through MCP**, in a fraction of the
  tokens the Engine's own tools spend.

## Contents

- [Why it exists](#why-it-exists)
- [The problem it solves](#the-problem-it-solves)
- [Usage](#usage)
- [Using it with an AI assistant](#using-it-with-an-ai-assistant)
- [Through MCP](#through-mcp)
- [Design principle](#design-principle)
- [Why the text is readable](#why-the-text-is-readable)
- [Status](#status)
- [Development](#development)
- [Known limitations](#known-limitations)

## Why it exists

To spend as few tokens as possible in the conversation between the AI and
Unreal.

It is not about being more expressive than the engine's tools -- it is not.
It is about a whole graph fitting in one message, instead of costing dozens of
round trips.

## The problem it solves

Asking an AI assistant to *describe* nodes is fast and cheap. Asking it to
**place** the nodes in the graph is slow and expensive, because Unreal's
internal format is huge:

| | approximate size |
|---|---|
| `Print String (In String = "hello")` | ~15 tokens |
| the same node in Unreal's clipboard format | ~1,000 tokens |

A 20-node graph goes past 20,000 tokens in that format -- and every link
depends on a 32-character sequence matching exactly between two pins. That is
a job for a script, not for a language model.

NodeScribe does that expansion locally, for free and without getting an
identifier wrong.

## Usage

Three buttons on the Blueprint editor toolbar, next to **Compile**:

| button | what it does |
|---|---|
| **Paste** | reads the NodeScribe text in the clipboard and creates the nodes in the open graph. `Ctrl+Z` undoes it. |
| **Copy Selected** | transcribes the selected nodes to text. Changes nothing. |
| **Copy Whole Graph** | same, for the whole graph. |

They show up in every Blueprint editor -- regular, Widget, Animation, Gameplay
Ability -- and stay invisible in editors that are not Blueprint editors.

Warnings and errors go to the **Message Log**, in the *NodeScribe* channel. The
warning shows up as a notification; its link opens the log with the details.

The text format is in [`Docs/FORMAT.md`](Docs/FORMAT.md).

## Using it with an AI assistant

The assistant does not guess the format -- it needs to read the specification
once.

If you use **Claude Code**, paste this into your project's `CLAUDE.md` and it
will know by itself, in every conversation:

```markdown
## Blueprint graphs

- Every Blueprint graph is delivered in the NodeScribe format, never as prose
  or numbered steps: the answer is pasted into the plugin as is.
- Read `Plugins/NodeScribe/Docs/FORMAT.md` (the core) before writing or
  interpreting a graph. A file in `Docs/format/` only when the task touches
  its topic: AnimGraph, Behavior Tree, Designer, the details of a reading.
- When needed, before the graph: the target asset and graph, and a table of
  the variables it needs (name | type | default | Instance Editable). Any
  explanation goes after the graph, in a separate block, never between nodes.
- Text pasted in this format comes from the plugin's **Copy Whole Graph**.
```

In any other assistant, paste the contents of `Docs/FORMAT.md` at the start of
the conversation, once, and a topic from `Docs/format/` when the work reaches
it. Through MCP, `get_format_docs()` gives the core and
`get_format_docs("animgraph")` a topic.

The cycle then becomes: **Copy Whole Graph** → paste into the chat → the
assistant returns the changed text → **Paste**.

## Through MCP

Besides the buttons, NodeScribe exposes itself as an MCP toolset --
`write_graph`, `read_graph`, `read_object`, `write_object` and company. Then the
assistant writes into the graph directly, instead of sending you text to paste.

| tool | what for |
|---|---|
| `read_graph(graph)` | a whole Blueprint graph as text |
| `write_graph(graph, text, replace)` | creates nodes from text. `replace` erases the graph first -- and refuses, touching nothing, if reading the current graph would lose anything |
| `clear_graph(graph)` | empties the graph and returns as text what was in it |
| `read_object(target, filter)` | properties of any object, class, CDO, actor or asset. An empty filter = only what differs from the default |
| `write_object(target, text)` | applies a sheet. A list of changes, not a final state |
| `create_asset(path, parent, options)` | an empty asset. Only inside `/Game/` |
| `write_blendspace(blend_space, text)` | a BlendSpace's axes and samples |
| `read_tags(filter)` / `write_tags(text, source)` | the declared Gameplay Tags / create tags |
| `get_format_docs()` | the format specification |
| `save_all_and_quit()` | saves and closes the editor |

`read_object` and `write_object` change shape by themselves depending on the
target: a **Blackboard** becomes a list of `key Name : Type`, a **Behavior
Tree** becomes an indented tree.

### Hooking it up in a new project

The chain has five links, and the assistant does not warn when one is
missing: the tools simply do not show up.

| link | what it is | who turns it on |
|---|---|---|
| **Python Script Plugin** | runs the plugin's `init_unreal.py` | NodeScribe's `.uplugin`, by itself |
| **ToolsetRegistry** | where the toolset registers | same |
| **ModelContextProtocol** | the Engine's MCP server, which exposes the registry | same |
| **`bAutoStartServer`** | the Engine's server is born off | `NodeScribeMcpSetup`, on launch |
| **`.mcp.json`** | tells the assistant where the server is | same |

The first three are dependencies declared in `NodeScribe.uplugin` and come in
by themselves when the plugin is installed. They are marked `Optional`: whoever
only wants the **Paste** and **Copy** buttons does not have to run any server.

The last two used to be manual work, and they are the part that did the most
damage: both fail silently. With the server off the assistant connects and
gets an empty list; with `.mcp.json` missing it does not even try. In both
cases nobody is warned -- a whole session went by with the server down before
anyone noticed.

Today `NodeScribeMcpSetup` takes care of both, one second after launch, and
says in the Output Log what it did:

```
LogNodeScribe: MCP: auto-start was off; turned it on and started the server at http://127.0.0.1:8000/mcp
LogNodeScribe: MCP: wrote the entry "unreal-mcp" -> http://127.0.0.1:8000/mcp in .../.mcp.json.
```

None of this links against Epic's plugin: the settings are read through
reflection by class name and the server starts through a console command, so
without ModelContextProtocol both fail quietly -- which is what `Optional`
promises.

**`.mcp.json` is treated as someone else's file.** It is read, changed and
rewritten preserving whatever is there: another server's entry is not
touched, and an entry that already points at the right address counts,
whatever its name. Two situations make the plugin back off without writing --
invalid JSON, and an entry already called `unreal-mcp` pointing somewhere else
(it may be a tunnel, or another editor). In that case the Output Log explains,
and **Tools → NodeScribe → Configure project MCP** fixes it, if that really is
what you want.

It could not be solved with a plugin ini. Unreal injects `<Plugin>/Config` into
the project's hierarchy, but matching the file name with a branch it already
knows, and the result does not reach `EditorPerProjectUserSettings`. That was
measured with both possible names, not deduced.

After that, **restart the assistant**: MCP tools only enter the context at
startup.

### Checking that it took

With the editor open, ask for `get_format_docs()`. If the tool does not exist,
the missing link is above it in the table -- and the plugin's `init_unreal.py`
complains in the Output Log, `LogPython` channel, when it does not find
`toolset_registry`.

### Why it is worth it

The reason is cost, not capability. Building a graph with the conventional
tools spends most of the tokens *discovering* node identifiers -- one call per
type, each returning dozens of results. NodeScribe's catalog resolves the
names locally, so a whole graph fits in one call and a few hundred tokens.

The tools together take ~1,100 tokens of description, against ~18,000 for the
Engine's Blueprint toolset -- charged when the assistant asks for that
toolset's description, not on every message. The Engine's MCP server uses lazy
discovery; whoever counts that as a fixed per-message cost will overestimate
the savings quite a bit.

A 15-node graph costs ~200 tokens in this format, against ~15,000 in Unreal's
clipboard format.

The way back has the same care as the way in: when the graph has something the
text cannot say -- an execution chain that reconverges, a node without a
stable name, a pin that comes from outside the selection -- it warns instead of
emitting a text that looks complete and comes back different.

## Design principle

When it cannot decide safely, **it does not decide**.

- Ambiguous node name → red comment in the graph with the candidates, none chosen.
- Unspecified asset → empty pin, and the choice stays visible waiting for you.
- Node with several execution paths → the chain stops, waiting for a label.

A guessed plausible node is the worst possible result: it compiles, runs, and
is wrong. Failing out loud is always preferable.

## Why the text is readable

The format saves tokens by **eliminating discovery**, not by shortening text.

The cost of building a graph with conventional tools is in asking for the
exact name of each node -- one call per type, each returning dozens of
identifiers. NodeScribe's catalog resolves `Print String` locally, with a
tolerant lookup. That is where the order-of-magnitude difference comes from.

The size of the text itself is ~2% of the cost. Swapping `Print String (In
String = "hello")` for an opaque identifier would save a few tokens per node --
and would cost the only thing that makes the format trustworthy: **you being
able to read what the AI wrote**.

The worst bugs in this project were found that way: a `Critical` branch that
printed `"Full"`, a `New Key` that had come unlinked, three identical chains
where one was enough. None of them would show up looking at the graph, and
none would show up in a format only the machine reads.

## Status

Version 0.4. UE 5.8.1, clean build with no warnings.

Working today: **graphs**, AnimGraphs included (`read_graph` /
`write_graph`), **single-object sheets** (`read_object` / `write_object`, which
also read and write Blackboards, Behavior Trees and a Widget Blueprint's
Designer), **BlendSpaces**, **Gameplay Tags** (`read_tags` / `write_tags`) and
**empty assets** (`create_asset`).

- **Exercised in a real project:** round trips on UI and gameplay graphs.
  These ran: events (override, custom, dispatcher, Input Action), Branch,
  For Each Loop, Switch, Cast, structs (Make/Break and split pin),
  subsystems, Create Widget with Expose on Spawn, async actions
  (`UBlueprintAsyncActionBase`), maps and sets, own and other-object variables,
  dispatcher Bind/Unbind, and the three buttons.
- **Compiles but never ran:** Select, dispatcher Call.
- **Language:** English throughout -- interface, messages, code and format.

One front at a time, and mature before the next; reading first, on its own,
because reading breaks no asset and is where almost all the token savings
are. The pending work, in order, is in [ROADMAP.md](ROADMAP.md); ideas not
accepted yet are in [Docs/ideas.md](Docs/ideas.md); what each version changed
is in [Docs/history.md](Docs/history.md).

## Development

This section exists for whoever -- person or AI agent -- is going to touch the
plugin without having followed the conversation in which it was designed.

Read first: [`CLAUDE.md`](CLAUDE.md) for the file structure, and
[`Docs/FORMAT.md`](Docs/FORMAT.md) for the text format.

### The cycle closes by itself

Unreal holds the plugin's binaries while it is open, so recompiling requires
closing the editor. **That is what `save_all_and_quit` exists for**: with it,
an agent closes the editor, compiles, reopens and tests without anybody
clicking anything. The whole cycle can be automated.

1. Edit the C++.
2. `save_all_and_quit` through the MCP. It refuses if Play In Editor is
   running, and the MCP connection drops right after -- that is expected.
3. Compile:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BossRushEditor Win64 Development -Project="C:\Unreal Projects\BossRush\BossRush.uproject" -WaitMutex -NoUBA
```

4. Reopen:

```powershell
Start-Process "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Unreal Projects\BossRush\BossRush.uproject"'
```

5. Wait for the editor to come up. The MCP only answers after that.
6. Test through the MCP, read the Message Log in the *NodeScribe* channel,
   repeat.

**If the MCP is down**, step 2 has a path that does not depend on it:

```powershell
New-Item -ItemType Directory -Force "C:\Unreal Projects\BossRush\Saved\NodeScribe" | Out-Null
Set-Content "C:\Unreal Projects\BossRush\Saved\NodeScribe\command.txt" "quit" -NoNewline
```

The plugin watches that file every half second, deletes it on reading and
calls the same `SaveAllAndQuit` -- with the same refusals, including the one
for Play In Editor running. The output goes to `response.txt` next to it.

It is not about saving tokens: writing the file costs the same as the MCP call,
or a bit more. It is so the cycle does not get stuck when the middle piece goes
down -- which already happened, with the editor open and the MCP disconnected,
and cost asking a person to click the X. Killing the process would work and is
the worst way out: it loses unsaved work and skips the PIE check.

**A server of our own inside the plugin is not worth it.** It would be more
code to solve less: it would still be a network protocol that needs to be up,
only maintained by us instead of Epic. The file's advantage is having nothing
that can go down.

Four things that cost time when forgotten:

- **The paths above belong to one machine** -- Engine on `E:`, project on `C:`.
  Check them before running on another.
- **`-NoUBA` is there on purpose.** On that machine the Unreal Build
  Accelerator's DLL injection is blocked and the build dies with error
  740/9006. Elsewhere it can go.
- **The quotes inside `-ArgumentList` are not decoration.** The path has a
  space in "Unreal Projects", and `Start-Process` does not add quotes by
  itself: without them the editor gets `C:\Unreal` as the project and opens
  *"Missing Modules"*, which looks like a broken build and is not.
- **Check the build's output, not just the exit code.** `Build.bat` exits with
  0 in situations where it compiled nothing. Look for `Result: Succeeded`.
- **Live Coding does not work here, and gets in the way.** It recompiles the
  body of a function that already exists, without closing the editor; a new
  `UFUNCTION` or `UCLASS` is new reflection, and new reflection requires a
  restart. Worse: with the editor open, `Build.bat` **does not even try to
  compile** -- it stops with `Unable to build while Live Coding is active` and
  shows no compile error at all. Syntax fixes cannot be rushed with the editor
  open. Closing comes first, always.

### Testing

The test that catches the most defects, and it is free: **the round trip**.
Read a graph, paste the text into an empty Blueprint, read again, compare the
two texts. Reader and writer are mirrors by design -- when the text does not
close, one of the two is lying, and the diff says which.

`Tests/run_tests.py` does that by itself, without an interface, in ~20 seconds:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" --% "C:/Unreal Projects/BossRush/BossRush.uproject" -run=pythonscript -script="C:/Unreal Projects/BossRush/Plugins/NodeScribe/Tests/run_tests.py" -unattended -nopause -nosplash -NullRHI
```

The report goes to `Saved/NodeScribe/tests.txt`, and if anything failed, the
process exits with a non-zero code.

**Two traps in the command, both because of the space in "Unreal
Projects".** The `--%` makes PowerShell stop interpreting and pass the rest
literally -- without it the path is cut at the space. And the path goes with
**forward slashes**: inside the quotes the Engine eats the backslash as an
escape, and `Tests\run` becomes `Testsun`.

Each case is written into an empty Blueprint, read, and that reading is
written into a *second* empty Blueprint and read again. A true mirror gives
two identical readings. It is the kind of test that needs no hand-kept answer
-- nothing to update when the format changes, and no case that passes because
it went stale together with the code.

It does not cover readability: `True:` on a node that is not a Branch closes
the fixed point just the same, and only showed up because someone read it. For
that someone still has to read a real graph now and then -- `read_graph` on a
real asset, and eyes on the text.

When it becomes necessary to run inside the Engine's suite, the template is
`ToolsetRegistry/Source/ToolsetRegistry/Private/Tests/ToolsetLibraryTest.cpp`
(`BEGIN_DEFINE_SPEC`). The cases migrate as they are; what changes is who calls
them.

### The missing vocabulary

`Saved/NodeScribe/vocabulary.txt` piles up one line per **written name that
matched nothing**: node not found, ambiguous node, nonexistent input pin,
nonexistent output in a `$x.Pin`.

```
node       Make Coffee
ambiguous  Apply Settings
pin        In Vec  in  Break Vector
output     Cup  in  $pc
```

It is not an error log, and the distinction matters. The error already comes
back in the call's return value, already goes to the Message Log and already
stays as a **red comment inside the graph** -- which is where it is useful,
next to the problem, and where `read_graph` itself brings it back from.
Duplicating that would be a poorer copy, and an expensive one to read.

This file answers another question, one that cannot be answered without it:
**which aliases are missing from the catalog.**

```bash
sort vocabulary.txt | uniq -c | sort -rn
```

After a few weeks of use, that is a to-do list sorted by frequency. It was a
case like that which showed that the doc's `Break Vector (In Vec = ...)`
example never worked.

That is why there is no date on the line: a repeat has to come out
**identical**, otherwise the count does not exist -- and the file's order is
already chronological. And that is why what is not vocabulary does not go in
here (incompatible type, empty pin, duplicate event): those have a known cause
and become noise in a count that exists to find patterns.

### Traps that already bit

They compile without complaint and fail at runtime. The build passing says
nothing about them.

- **`UEdGraphSchema_K2()` on the stack brings the editor down.** A UObject class
  cannot be instantiated that way: the constructor calls
  `FObjectInitializer::Get()`, which is only valid inside a UObject constructor,
  and the error is fatal on the spot. Use `GetDefault<UEdGraphSchema_K2>()`. It
  already happened twice -- in `cac90d8`, and again while writing
  `NodeScribePropertyText`. Worth checking every time
  `ConvertPropertyToPinType` shows up.

### What must not break

These four points are the project. A change that contradicts them is wrong even
if it works:

1. **When it cannot decide safely, it does not decide.** Ambiguous becomes a red
   comment; an unspecified asset becomes an empty pin. A guessed plausible node
   compiles, runs and is wrong -- it is the worst possible result.
2. **Reader and writer are mirrors.** When teaching a new type, both change, in
   the same commit.
3. **Readability is not traded for bytes.** The text is ~2% of the cost;
   shortening it saves ~1% and destroys the only defence against the AI having
   written something else.
4. **A new tool costs a choice.** It is not the prompt -- discovery is lazy and
   the description is only read when asked for. It is that six similar doors
   make the assistant choose wrong and spend a turn finding out. Prefer a new
   parameter on an existing tool over a new tool.

### Growing without getting heavier

What an assistant pays grows with the plugin unless each new piece lands
where only the tasks that need it read it:

- **A new kind of asset** is a new shape of `read_object` / `write_object`,
  not a new tool (D-016), and its text gets a topic of its own in
  `Docs/format/`, named in the core's *Topics* table. The core
  (`Docs/FORMAT.md`) only takes rules every graph uses.
- **A new family of nodes** is born in its own reader/builder pair of files,
  as `NodeScribeAnimGraph` did, instead of growing `NodeScribeBuilder.cpp` and
  `NodeScribeReader.cpp`.
- **History and proposals stay out of the README:** what a version changed
  goes to [Docs/history.md](Docs/history.md), an idea to
  [Docs/ideas.md](Docs/ideas.md).

The suite's `token_budget` case holds the line: it fails when the core, a
topic, the tools' descriptions, the number of tools, or the size of a reading
against the text that made it passes its ceiling (`BUDGET`, at the top of
`Tests/run_tests.py`), and prints the numbers when it passes. Raising a ceiling
is allowed; doing it without saying why in the commit is not.

### Proposing a new tool

It goes to [Docs/ideas.md](Docs/ideas.md) before becoming code, and the proposal
needs to answer four things -- the same ones the sheet answers:

- **Where does the token leak today?** With numbers, looking at the native
  tool's code, not from memory.
- **What is the compressed format?** One line per item, the name shown on
  screen, readable by people.
- **How much surface does it cost?** How many new MCP tools, and why it cannot
  be fewer.
- **What does it let you turn off?** If the answer is "nothing", it does not
  save -- it only adds.

## Known limitations

- One node per line; nested expressions (`Print(Concat(a, b))`) are not
  supported. Break them into two lines with `x = Concat(...)`.
- The layout is simple: execution left to right, one column per step; data
  nodes stack below the step that consumes them, indenting to the left as they
  go deeper into the chain. Readable, not pretty.
- Timelines and local variables have no form yet.
- Async/latent nodes (`Delay`, AbilityTasks) go in like any function, but the
  extra outputs require explicit labels.
- The format's keywords are English only. Texts written with the old
  Portuguese keywords (`evento`, `verdadeiro:`, `variavel`) no longer paste.
