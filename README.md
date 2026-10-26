[Português](README.pt-BR.md) · **English**

# NodeScribe

NodeScribe is an Unreal Engine editor plugin that turns plain text into
Blueprint nodes and back. It exists so an AI assistant can build and read
graphs cheaply: in Unreal's internal format a single `Print String` takes
~1,000 tokens, and every link depends on a 32-character identifier matching
between two pins. In NodeScribe the same node is the line
`Print String (In String = "hello")`, ~15 tokens, and the plugin resolves the
names by itself, without the dozens of lookups the Engine's tools make to
discover each node. In use, you copy a graph as text, paste it into the chat,
get the changed text back and paste it in with one button -- or let the
assistant do it all directly through MCP. Besides graphs, AnimGraphs and state
machines, the same text reads and writes any object's properties, Blackboards,
Behavior Trees, a Widget Blueprint's Designer, BlendSpaces and Gameplay Tags,
and creates empty assets.

## Contents

- [Usage](#usage)
- [With an AI assistant](#with-an-ai-assistant)
- [Through MCP](#through-mcp)
- [Principles](#principles)
- [Status](#status)
- [Known limitations](#known-limitations)
- [Development](#development)

## Usage

Three buttons on the Blueprint editor toolbar, next to **Compile**:

| button | what it does |
|---|---|
| **Paste** | reads the NodeScribe text in the clipboard and creates the nodes in the open graph. `Ctrl+Z` undoes it. |
| **Copy Selected** | transcribes the selected nodes to text. Changes nothing. |
| **Copy Whole Graph** | same, for the whole graph. |

They show up in every Blueprint editor -- regular, Widget, Animation, Gameplay
Ability. Warnings and errors go to the **Message Log**, *NodeScribe* channel;
the notification has a link that opens the log.

The text format is in [`Docs/FORMAT.md`](Docs/FORMAT.md).

## With an AI assistant

The assistant does not guess the format: it needs to read the specification
once. In **Claude Code**, paste this into your project's `CLAUDE.md`:

```markdown
## Blueprint graphs

- Every Blueprint graph is delivered in the NodeScribe format, never as prose
  or numbered steps: the answer is pasted into the plugin as is.
- Read `Plugins/NodeScribe/Docs/FORMAT.md` (the core) before writing or
  interpreting a graph. A file in `Docs/format/` only when the task touches
  its topic: AnimGraph, Behavior Tree, Designer, the details of a reading.
- When needed, before the graph: the target asset and graph, and a table of
  the variables it needs (name | type | default | Instance Editable). Any
  explanation goes after the graph, in a separate block.
- Text pasted in this format comes from the plugin's **Copy Whole Graph**.
```

In another assistant, paste the contents of `Docs/FORMAT.md` at the start of
the conversation, and a topic from `Docs/format/` when the work reaches it.

The cycle becomes: **Copy Whole Graph** → paste into the chat → the assistant
returns the changed text → **Paste**.

## Through MCP

NodeScribe also exposes itself as an MCP toolset, and then the assistant
writes into the graph directly, with nothing for you to paste:

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
| `get_format_docs(topic)` | the format specification: empty gives the core, a name gives a topic |
| `save_all_and_quit()` | saves and closes the editor |

`read_object` and `write_object` change shape with the target: a
**Blackboard** becomes a list of `key Name : Type`, a **Behavior Tree** an
indented tree, a **Widget Blueprint** gets, after its sheet, a `designer`
section with the Designer's hierarchy, and an **Input Mapping Context** a
`mappings` section, one `Key -> Action` line per mapping. In an actor
Blueprint, the same sheet adds and removes components.

Measured: the tools together take ~2,600 tokens of description, against
~18,000 for the Engine's Blueprint toolset, and a 15-node graph costs ~200
tokens in this format, against ~15,000 in Unreal's.

### Hooking it up in a new project

The MCP depends on five pieces, and when one is missing the tools simply do
not show up, with no warning:

| piece | what it is | who turns it on |
|---|---|---|
| **Python Script Plugin** | runs the plugin's `init_unreal.py` | NodeScribe's `.uplugin`, by itself |
| **ToolsetRegistry** | where the toolset registers | same |
| **ModelContextProtocol** | the Engine's MCP server | same |
| **`bAutoStartServer`** | the Engine's server is born off | NodeScribe, when the editor opens |
| **`.mcp.json`** | tells the assistant where the server is | same |

The first three are marked `Optional`: whoever only wants the buttons needs no
server. The last two the plugin sets by itself and says in the Output Log what
it did; if `.mcp.json` already has something else in place, it leaves it and
explains why, and **Tools → NodeScribe → Configure project MCP** forces it.

Then **restart the assistant**: MCP tools only come in at startup. To check,
with the editor open, ask for `get_format_docs()`. If the tool does not exist,
the Output Log, `LogPython` channel, says which piece was missing.

## Principles

**When it cannot decide safely, it does not decide.** An ambiguous node name
becomes a red comment in the graph, with the candidates and none chosen; an
unspecified asset becomes an empty pin, which blocks compiling until you
choose. A guessed plausible node is the worst possible result: it compiles,
runs and is wrong.

**The text is for people to read.** The saving comes from eliminating
discovery, not from shortening the text, which is ~2% of the cost. Swapping
`Print String` for an opaque code would save almost nothing and remove the
only defence against the AI having written something else: you being able to
read what it wrote. The worst bugs in this project were found by reading -- a
`Critical` branch that printed `"Full"`, an unlinked `New Key`, three identical
chains where one was enough.

**The way back warns about what does not fit.** When the graph has something
the text cannot say -- an execution chain that reconverges, a node without a
stable name, a pin fed from outside the selection --, the reading warns
instead of coming out looking complete.

## Status

Version 0.4, Unreal 5.8.1.

- **Exercised in a real project:** round trips on UI and gameplay graphs --
  events (override, custom, dispatcher, Input Action), Branch, For Each Loop,
  Switch, Cast, structs (Make/Break and split pin), subsystems, Create Widget
  with Expose on Spawn, async actions, maps and sets, own and other-object
  variables, dispatcher Bind/Unbind, and the three buttons.
- **Compiles but never ran:** Select, dispatcher Call.
- **Language:** the interface, messages, code and format are in English. The
  main README is [`README.pt-BR.md`](README.pt-BR.md), in Portuguese; this one
  is its translation.

The pending work, in order, is in [ROADMAP.md](ROADMAP.md); ideas not accepted
yet are in [Docs/ideas.md](Docs/ideas.md); what each version changed is in
[Docs/history.md](Docs/history.md).

## Known limitations

- One node per line; nested expressions (`Print(Concat(a, b))`) are not
  supported. Break them into two lines with `x = Concat(...)`.
- The layout is simple: execution left to right, one column per step, data
  nodes stacked below whoever uses them. Readable, not pretty.
- Timelines and local variables have no form yet.
- Latent nodes (`Delay`, AbilityTasks) go in like any function, but the extra
  outputs require explicit labels.
- The format's keywords are English only. Texts with the old Portuguese
  keywords (`evento`, `verdadeiro:`, `variavel`) no longer paste.

## Development

To work on the plugin -- building, testing, what must not break, how to
propose a new tool --, see [Docs/development.md](Docs/development.md).
