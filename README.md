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
- **Read Blackboards and Behavior Trees**, and write Blackboard keys.
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
- [Possible future features](#possible-future-features)
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

When delivering a Blueprint graph, use the NodeScribe format, specified in
`Plugins/NodeScribe/Docs/FORMAT.md`. Read that file before writing or
interpreting a graph. Never describe nodes in prose.
```

In any other assistant, paste the contents of `Docs/FORMAT.md` at the start of
the conversation. Once per conversation.

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

- **Exercised in a real project:** round trips on UI and gameplay graphs.
  These ran: events (override, custom, dispatcher, Input Action), Branch,
  For Each Loop, Switch, Cast, structs (Make/Break and split pin),
  subsystems, Create Widget with Expose on Spawn, async actions
  (`UBlueprintAsyncActionBase`), maps and sets, own and other-object variables,
  and the three buttons.
- **Compiles but never ran:** Select, dispatcher Call/Bind/Unbind.
- **Language:** English throughout -- interface, messages, code and format.

### The format is English-only

The format's keywords used to accept Portuguese and English side by side
(`evento`/`event`, `verdadeiro`/`true`, `variavel`/`variable`). They are now
English only: `event`, `true:`, `false:`, `variable`, `state`, `conduit`,
`key`, `sheet`, `delete variable`, `Map of X to Y`, and so on -- the full list
is at the end of [`Docs/FORMAT.md`](Docs/FORMAT.md#keyword-reference). **Texts
written with the Portuguese keywords no longer paste.** Names that come from a
project (variables, pins, assets) are unaffected and may be in any language;
the lookup still ignores accents.

The same pass fixed a handful of defects found along the way:

- **A parameter literally called `Target` could not be written.**
  `Get Blackboard (Target = $controller)` came out of the reader and failed in
  the builder with *"the node has no pin `Target`. Input pins: Target"*: the
  builder only tried `Target` as an alias for the self pin. It now tries the
  name as written first.
- **`Switch Has Authority` could not be pasted,** for two reasons stacked. A
  bare `Switch ` prefix was read as `Switch on <enum>` and died as "enum not
  found"; and behind that, the macro lives in `ActorMacros`, while the builder
  only looked in `StandardMacros`. Only `Switch on X` promises an enum now, and
  the macro lookup goes through `ActorMacros` and `ActorComponentMacros` too,
  for Blueprints of those parents.
- **Components came out as variables.** The reading's header declared a
  component added in the Components panel as `variable Mesh :
  StaticMeshComponent`; pasted into another Blueprint, that creates a plain
  variable that never points at any component. Components now come out as a
  comment, the way Designer widgets already did.
- **`Map ` swallowed struct names.** `variable X : Map Player Key Args` was
  read as a map and refused. Only `Map of ` opens a map now.
- **Pasting a whole sheet back ended in an error.** The sheet's
  `~ N properties at default` footer was parsed as a block header. It is
  skipped now, as the header already was.
- **A boolean read from a sheet created a second variable.** The sheet writes
  `bIsAlive` as `Is Alive`, and `write_object` only compared the internal name,
  so pasting it back created a new `Is Alive` next to it. Variables are now
  matched by their display name too.
- **`Class.Function` did not accept a Blueprint class.** `BP_Golem.Jump` was
  compared against `BP_Golem_C` and never matched.
- **The paste notification counted every warning as a pin waiting for a
  choice.** It now says how many warnings there are, of any kind.
- **The format doc's state machine example did not paste.** `Greater` and
  `Less Equal` exist for every numeric type and are ambiguous; the examples now
  use `KismetMathLibrary.Greater_DoubleDouble`, which is also what the reading
  writes.

The test suite grew to 17 cases with one for each of these, and all of them
pass.

### What 0.4 changed: the reader stopped lying

0.3 was good enough to find real bugs in a long UI review. Where it failed was
worse than failing: in two places the text was **plausible and wrong**, and
that turned into a wrong statement in a commit message.

Three were correctness defects, not diagnostic ones:

- **A struct field vanished from the pin.** A bare `$x` came out when the
  Break had a single visible field -- and a Break's pins are the fields *ticked*
  in its panel. The Break recreated on pasting is born with all of them ticked,
  so `$x` went into the struct's first field, which could be another one. Now a
  Break always names the field.
- **A display name with parentheses did not come back.** `SetText (Text)` is in
  every UI graph; the parser cuts the line at the first `(` and that came back
  as a `SetText` node getting a `Text` argument. A name with parentheses now
  comes out qualified (`TextBlock.SetText`).
- **An async node's `then` came out as `True:`.** The alias exists for Branch,
  and outside it invented a condition that does not exist -- the reader
  understood there was a test, and the next branch became its `False`.

And the rest of what the review asked for: an anchor on both ends of a
reconvergence, an implicit type conversion mark, the name of each orphan node,
`Map of X to Y` and `Set of X`, async actions both ways, `refPath` in the
header, and an erased value coming out as `""` instead of vanishing.

### Replacing the graph, and how far the guard sees

`write_graph(graph, text, replace=True)` erases the graph before writing,
instead of appending. It exists because iterating on a graph in append mode
piles up nodes, and deleting by hand on every pass is the friction that makes
someone stop using the tool.

**The guard:** before erasing, the plugin reads the current graph. If the
reading has any "this does not come back the same" warning, or a data node
nobody consumes, the replacement is refused and **nothing** changes. Erasing
from a text that lost something destroys precisely what nobody has written.

There is no forced mode, on purpose. Whoever really wants to clear selects
everything in the graph and presses Delete: a human gesture, visible, with
Ctrl+Z right there -- or calls `clear_graph`, which hands back what it erased.
The plugin's erase and write sit in the same transaction, so Ctrl+Z also
undoes both at once.

**Where the guard does not see, and that needs to be written down.** It
refuses what the reader *knows* it loses. It does not cover what the reader
does not know it lost:

- **a node's purity** -- a pure `Cast` comes back impure, because the text does
  not say which of the two it is;
- **a disabled node** in the middle of a chain -- comes back enabled;
- **layout** -- position, comment box size, reroutes.

None of those changes what the graph does, except purity. But the list exists
because "the guard passed" is not proof that the text rebuilds the graph -- it
is proof that the reader saw no problem.

The strong version of this would be the fixed point in memory: build the text
read into a transient graph, read it back and compare, which is exactly what
`Tests/run_tests.py` does from outside. It is noted as the next step if
replacing becomes routine.

### What 0.4 changed: the rest

**An automated round-trip test** (`Tests/run_tests.py`), which on its first run
already caught an example in the format doc that did not work:
`Break Vector (In Vec = ...)`. A Break's input pin is named after the struct,
so the right example is `Break Vector ($position)`.

**The catalog is now invalidated when a Blueprint compiles.** It is built once
per session, and `Invalidate()` existed with nobody calling it: creating a
function in a Blueprint and calling it from *another*, in the same session,
gave "no node called X" for something that exists.

**Three things that made the text teach the wrong name:**

- `100.000000` on a pin became `100.0`. The sheet already formatted it this way,
  with the comment explaining why; the graph reader did not -- it was the same
  decision applied to half of the plugin.
- `BP_Golem_C` became `BP_Golem`. The suffix belongs to compilation, shows up
  nowhere in the interface, and the lookup accepts both forms.
- `TargetMap` became `Target Map`, and `bIsChecked` became `Is Checked` -- the
  C++ parameter name gave way to what the screen shows. **With a guard:** a name
  with an acronym or a digit comes out raw, because `NameToDisplayString`
  decides where a space fits by case changes and gets acronyms wrong --
  `JSL4UControllerInfo` came out as `JSL4UController Info`. That only showed up
  rereading a real graph after the change; the fixed-point test passed, because
  the lookup normalises both forms.

### `variable X : Actor` created a struct

The 0.4 round trip ran into this: `Map of Int64 to Actor` came back as
`Map of Int64 to TypedElementActorTag`.

The Engine declares `FTypedElementActorTag` as
`USTRUCT(meta = (DisplayName = "Actor"))`, in
`Elements/Columns/TypedElementCompatibilityColumns.h`.
`ResolvePinTypeFromNameInternal` looks for a struct before a class, and the
struct lookup accepts display names -- so "Actor" matched it first. It predated
0.4, applied to any declaration, and `Actor` is among the most written type
names there are.

The new rule is narrow: **a struct that matched only by display name loses to a
class with an exact name.** `Vector` and `TimerHandle` match by the struct's
internal name and keep winning; the class lookup only knows internal names (and
the Blueprint `_C`), so it does not open a second display-name door.

Checked by regression: `Vector`, `Timer Handle`, `Transform`, `Rotator`,
`Linear Color`, `Array of Vector`, `Map of Name to Vector` and an enum keep
resolving as before; `Actor` and `Pawn` now give the class.

### Where we are

Working today: **graphs** (`read_graph` / `write_graph`), **single-object
sheets** (`read_object`, default and filtered mode) and **Gameplay Tags**
(`read_tags` / `write_tags`).

The active front is **reading an enemy's whole AI** -- the Golem in the
project that drives development is the real case guiding the work. The roadmap
is right below.

One front at a time, and mature before the next. An idea for a new tool that
shows up along the way goes to *Possible future features*, not to the code:
the plugin is only worth it if each piece is reliable, and a piece only
becomes reliable with repeated use.

On every front, **reading first, on its own.** Reading does not break any
asset, so it can be tested freely while the format is still changing its mind
-- and it is where almost all the token savings are. Writing in a format that
will still change is an expensive regret.

### Roadmap: reading the enemy's AI

The real case's assets live in the project that drives development: a
Behavior Tree, its Blackboard, an AI controller, a service and a patrol task,
plus four Gameplay Abilities. The examples below are that project's output,
with its asset names translated.

**An assumption that proved false and saves work:** BT and Blackboard do
**not** depend on components in the sheet. A BT node is a UObject with
properties and nothing more. Components are only missing for the AI
controller and for tuning the Golem's movement, and that is why they moved
down the queue.

#### Stage 0 -- Abilities: done, no new code

`read_object` already delivers a whole `GA_`. A GameplayAbility has no
components, so the sheet's gap does not bite there:

```
sheet GA_RockRain (GameplayAbility)
Rock Class = /Game/MyGame/GAS/Abilities/Golem/RockRain/BP_Rock.BP_Rock_C
Rock Count = 8
Area Radius = 1500.0
Interval Between Rocks = 0.25
~ 22 properties at default
```

#### Stage 1 -- Blackboard keys -- **done**

It came first because a BT decorator references a key **by name**: without
the keys, the tree would come out full of loose, meaningless names.

Before, it failed with `I do not know how to write the value of: Keys`.
`UBlackboardData::Keys` is a `TArray<FBlackboardEntry>`, and each entry keeps a
`KeyType` that is an **instanced subobject** -- that is what the generic
formatter does not open.

```
blackboard BB_Golem
key Player : Object (Actor)
key Attack Distance : Float
key Can Use Laser? : Bool
```

The detail that qualifies the type (an Object's `BaseClass`, an Enum's
`EnumType`) comes out **through reflection**, not through one case per known
subclass: there are ten types in the Engine and any project can write its own,
and a `switch` of casts would answer empty for the key the project itself
created, without saying it was ignoring something. For the same reason,
`Build.cs` gained `AIModule` but only two headers come in by include.

The type's name comes from the `KeyType`'s class, without the
`BlackboardKeyType_` prefix. `UBlackboardData::Parent` becomes a header line
when it exists.

#### Stage 2 -- The BT tree -- **done**

The whole tree in one call. Before, seeing what the AI does required following
pointers node by node, and **`AIModuleToolset` is not even enabled in that
project** -- there was no native alternative.

```
tree BT_Golem  (blackboard BB_Golem)
Selector
  Sequence
    Move To (Blackboard Key = Player)
  Sequence
    Patrol
    Wait (Wait Time = 1.00)
```

Walk: `UBehaviorTree::RootNode` and `RootDecorators`, then
`UBTCompositeNode::Children` -- each `FBTCompositeChild` has its own
`Decorators` --, plus the composite's and the task's `Services`. There is a
guard against cycles, which a BT does not have by construction but a corrupted
asset might.

**Decorators and services come out as lines with a keyword** (`decorator X`,
`service Y`), and not as an indented label ending in `:` as the old draft
planned. A label works for a graph branch, where each branch is a path; here a
node may have several decorators, and nesting each one would create
indentation levels that do not exist in the tree. A decorator comes out
together with the child it guards, which is where the editor shows it.

The parameters come out through the sheet's formatter, compared with the CDO of
the node's class -- that is why the sheet came first. A `Move To` with the
factory radius gets no parameter at all.

Two forms needed their own handling:

- **A blackboard key selector** becomes just the key's name. The canonical form
  carries the list of accepted types along, which fills the line and hides
  which key it is.
- **`FValueOrBBKey_*`** (`Wait Time`, `Acceptable Radius`) has its own
  `ToString()`, which gives the number when the value is fixed and the key's
  name when it is bound to the blackboard. **5.8 retired `FAIDataProviderValue`
  in favour of this family** -- aiming only at the old one compiles, runs and
  returns `(DefaultValue=1.000000)`. That is what happened on the first try.

`GetNodeName()` resolves the Engine's nodes, but on a Blueprint class it only
strips the `_C`: the project's patrol task showed up with its technical
`BTTask_` prefix next to a clean `Move To`. The prefix is removed on reading.

**A BT is the best fit this format has ever had:** a BT is literally a tree and
does not reconverge, so the loss the format doc declares for graphs ("an
execution chain that reconverges loses its way back") does not exist here.

#### Stage 3 -- Components in the sheet -- **done**

```
sheet BP_Golem (Character)
# inherits: Character < Pawn < Actor
variable Faction : GameplayTag = '(TagName="Faction.Enemies")'
Auto Possess AI = PlacedInWorldOrSpawned # default PlacedInWorld
CollisionCylinder : CapsuleComponent
  Capsule Half Height = 98.0 # default 88.0
  Capsule Radius = 80.0      # default 34.0
CharMoveComp : CharacterMovementComponent
  Max Walk Speed = 300.0 # default 600.0
~ 620 properties at default
# [note]: changed, but the value is too long for the overview -- ask for it by
#   name to see it: CollisionCylinder.Body Instance, CharacterMesh0.Body Instance
```

`read_object(BP_Golem, "walk")` now answers `11 of 637`, with
`Max Walk Speed : Float = 300.0 # default 600.0`. Before it answered `0 of 78`.

**Components come from two places and both need reading:** what came from the
C++ constructor lives in the CDO (`AActor::GetComponents()`), and what was
dragged in the editor lives as a template in the `SimpleConstructionScript`.
The lookup walks up the chain of parent Blueprints, because a component the
parent created belongs to the child too. Reading only one of the sources hides
half the components without warning.

**A struct that is too long opens and shows only the member that changed.** It
is the sheet's principle one level down. A capsule's `Body Instance` comes out
flat with the whole collision response table -- ~2,000 characters, plus another
~2,000 of the factory value next to it, on its own bigger than the Golem's
whole sheet. Opened, it is three lines that say the thing:

```
  Body Instance:
    Object Type = ECC_GameTraceChannel2 # default ECC_Pawn
    Collision Profile Name = Body       # default Pawn
```

**It only opens when the flat form goes over 160 characters.** Always opening
would cost readability on small structs: `Relative Location` is worth more as
one line than as three, and a lone `Z` would lose the company of the `X` and
`Y` that say it is a position.

Recursion up to three levels. When opening does not help -- the inner member is
also big and is not a struct --, everything is undone, **including what the
attempt recorded**: without that the note named the inner member and the outer
one, which are the same thing said twice. What is left is named with the whole
path (`CharacterMesh0.Body Instance.Collision Responses`) and comes out whole if
you ask for it by name -- the cut does not apply in filtered mode.

**A cap on the alignment column (64).** Without it, one wide line pushes the
comment of all the others to the same distance -- with that 2,000-character
struct, the neighbours got 2,000 spaces each. Alignment is for reading; beyond
that it gets in the way and still costs tokens.

#### Stage 4 -- Blueprint variables with the `variable` line -- **done**

They come out in a block of their own, with the type, before the inherited
properties. A variable declared by the Blueprint itself **always** shows up,
even at its factory value: it does not exist in the parent class, so its
existence already is the information.

#### Stage 5 -- Writing -- `write_object` **done**

```
Vertical Force = 900
CollisionCylinder:
  Capsule Radius = default
  Body Instance = default
CharMoveComp:
  Max Walk Speed = 420
```

**The text is a list of changes, not the final state.** Nothing is deleted, and
pasting a whole sheet back touches nothing beyond what the lines say.
`= default` returns to the factory value.

An indented block reaches inside a component and inside a struct. **Zero
indentation closes the block**, and that was the first test's defect: without
closing it, a line of the actor itself written after a component kept being
applied to the component. If the component had a property with that name, it
wrote to the wrong place silently -- the "compiles, runs and is wrong" again,
now saved into an asset.

A name that does not resolve becomes a diagnostic **with the similar names**,
and the other lines keep being applied.

Verified by a round trip on a scratch asset: read, write, read, revert with
`default`, read -- the last reading matches the first.

An effect worth knowing: `Collision Profile Name = Body` also touches
`Object Type` and `Collision Responses`. It is `PostEditChangeProperty`
applying the profile, Unreal's own behaviour -- the sheet shows the real
result, not only what the line asked for.

#### Stage 5 -- Writing -- blackboard **done**

The same format as the reader. It creates the key that does not exist, changes
the type of the one that does, and deletes none:

```
blackboard BB_Golem
key Target : Object (Actor)
key Phase : Int
key Health : Float synced
```

The type's class comes from a sweep (`Object` → `UBlackboardKeyType_Object`),
not from a fixed table -- for the same reason as the reader: any project can
write its own key type, and a table would answer "unknown" for what the project
itself created. An unknown type lists the existing ones.

**`synced` became a word, not a comment.** The reader used to emit it as a
`# synced between instances` comment, and the writer discards comments -- a key
that came back unsynced would be a bug that only shows up with two enemies on
screen at the same time.

Changing the type changes the whole subobject. Reusing the old one would leave
the previous key's properties hanging on the new one.

Exercised on a scratch blackboard: four new keys created, one that already
existed was not duplicated, `synced` came back through the reading,
`Object (Pawn)` resolved the class by its short name, and a nonexistent type
was refused listing the twelve that exist.

**Writing a BT is still missing**, and it is the expensive one: the asset keeps
the execution hierarchy *and* an editor graph (`UBehaviorTreeGraph`) that has
to stay in sync. Writing only the runtime side gives an asset that runs and
shows up empty on screen.

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

### Proposing a new tool

It goes to *Possible future features* before becoming code, and the proposal
needs to answer four things -- the same ones the sheet answers:

- **Where does the token leak today?** With numbers, looking at the native
  tool's code, not from memory.
- **What is the compressed format?** One line per item, the name shown on
  screen, readable by people.
- **How much surface does it cost?** How many new MCP tools, and why it cannot
  be fewer.
- **What does it let you turn off?** If the answer is "nothing", it does not
  save -- it only adds.

## Possible future features

### Our own MCP: talking straight to the client

Today NodeScribe's tools reach the client through three of Epic's layers: the
`ModelContextProtocol` plugin (the HTTP server), `ToolsetRegistry` (the
registry) and `AllToolsets`/`EditorToolset` (which expose the three generic
tools). The client sees `list_toolsets`, `describe_toolset` and `call_tool` --
and every `read_graph` travels inside a `call_tool`.

The proposal is for the plugin to speak MCP on its own. **In two stages, and
the first stands on its own** -- the second only changes who hosts it.

#### Stage 1 -- registering the tools directly

`IModelContextProtocolModule::GetChecked().AddTool(...)` accepts any
`IModelContextProtocolTool`: name, description, input JSON Schema and a `Run`.
Those are exactly the four things the tool already has, written in Python
today. Registered directly, `read_graph` becomes a first-class tool in the
client, and the whole Python layer goes away.

#### Stage 2 -- serving the protocol

`HTTPServer` is a module of the Engine's **core** (`Runtime/Online/HTTPServer`),
not a plugin -- it is what Epic's server is built on. It is an
`FTSTickerObjectBase`, so the handlers run on the game thread, which is
precisely where Blueprints can be touched: no marshalling.

The minimum MCP for a tools-only server is `initialize`,
`notifications/initialized`, `tools/list` and `tools/call`, in JSON-RPC 2.0
over POST. **SSE is not mandatory** -- Epic's server only opens
`text/event-stream` when the client asks for progress, and answering plain JSON
conforms to the specification. The project's `.mcp.json` then points at our
port.

#### Where the token leaks today

Measured, not from memory:

- The nine descriptions add up to **4,551 characters (~1,140 tokens)**, plus the
  parameters' schema. It is what `describe_toolset` returns, once per session.
- The `call_tool` envelope costs ~30-40 tokens per call -- the toolset's name
  alone (`nodescribe_toolset.toolsets.graph.NodeScribeTools`) is 46 characters
  repeated in every call.
- `list_toolsets` is ~600 tokens when called.

**Be honest about that math: it is not an order-of-magnitude saving.** The
descriptions that come through `describe_toolset` today would live in the
prompt, so on that axis it is almost a tie -- one discovery call is traded for
a permanent presence. What is left as a clean gain is the envelope (~1,000
tokens in a session of thirty calls) and the three generic descriptions that
stop existing.

This plugin's big saving has already been made, and it was the catalog.
Whoever promises more than that here is selling something.

#### What it lets you turn off

That is the deciding question. With stage 1: `ToolsetRegistry`,
`PythonScriptPlugin` (for us) and the whole `toolset_registry` layer -- the
`__pycache__` that dirties the repository on every run goes along with it. With
stage 2: `ModelContextProtocol` too.

Turning off `AllToolsets`/`EditorToolset` is **a separate decision, and it has
a price**: asset `duplicate`, `move` and `delete` go along, which a project
uses and NodeScribe does not replace. Measured in *Creating assets*: ~4,000
tokens of discovery once per session and ~50 per call.

#### What improves besides tokens

This is where the proposal pays for itself:

- **The wrong-path error becomes ours.** Today `/Game/UI/...` on a plugin asset
  returns `... is not valid Object for property 'target'`, from the Engine's
  `ReferenceConverter`, before any of our code runs. With the parameter
  arriving as a string, the plugin resolves the path and can suggest the
  similar content roots.
- **Optional parameters really work.** The Python registration ignores `= ''`
  and only respects `str | None`; with our own schema, `filter` is optional
  because we say it is.
- **Three fewer experimental plugins between us and the client.** All three
  are `IsExperimentalVersion` and may change shape between Engine versions.

#### What stays the same, and there is no point pretending

This README already records, in *Development*, that a server of our own "is not
worth it" -- that was said about replacing the **command file**, which exists
precisely for when the network goes down. The argument survives here: stage 2
does not remove the risk of having a network protocol up, it only changes its
owner. `Saved/NodeScribe/command.txt` stays the path that depends on nothing,
and it does not go away.

#### Cost and order

| stage | where | cost |
|---|---|---|
| 1 | our own tool interface + 9 adapters + `refPath` resolution | ~400 lines |
| 1 | delete `Content/Python/` and the `ToolsetRegistry` dependency | ~0 |
| 2 | JSON-RPC server over `FHttpServerModule` (`initialize`, `tools/list`, `tools/call`) | ~350 lines |
| 2 | configurable port, `bind` on 127.0.0.1, `Origin` check | ~50 lines |

Stage 1 is a prerequisite for 2 either way: the tools need to exist as objects
with a name, a schema and a `Run` before it matters who serves them. And the
design both share is the same -- **the tools are written against an interface
of ours, and an adapter hands them to whoever is serving**: Epic's server in
stage 1, ours in 2.

Two things to decide before writing code: the **port** (8000 is Epic's;
coexisting requires another) and whether `AllToolsets` stays on for
`duplicate`/`move`/`delete`.

### The sheet: object properties as text

The graph is half the work. The other half is reading and changing properties
-- and there the cost today is worse than it was for graphs.

The Engine's `EditorToolset` solves that with `list_properties`, which returns
the class's whole JSON Schema: type, description and factory value of every
property, all inheritance included. For a `Character` that is ~250 properties,
around **10,000 tokens** -- and that is only the shape, no values. The values
need a second call.

The idea is a **sheet**: one line per property, and only what **differs from
the default**.

```
sheet BP_Golem (Character)
# inherits: BP_BossBase < Character < Pawn < Actor
variable Max Health : Float = 500
Mesh : SkeletalMeshComponent
  Skeletal Mesh = /Game/MyGame/Bosses/SKM_Golem.SKM_Golem
  Relative Location = (X=0.0,Y=0.0,Z=-90.0)   # default (X=0.0,Y=0.0,Z=0.0)
CharacterMovement : CharacterMovementComponent
  Max Walk Speed = 250                        # default 600
~ 431 properties at default
```

Two things look long on purpose. **An asset comes out by full path**, same as
the graph -- `SKM_Golem` would be shorter and ambiguous, and two folders may
have an asset with that name. **A struct comes out in the Engine's canonical
form**, with `X=`, `Y=`, `Z=`: shortening it to `(0, 0, -90)` would save some
five tokens on a minority of lines and open a class of round-trip error that
does not exist today. It is exactly the trade the rest of this document
recommends not making. What **is** clean in the output are the trailing zeros
(`-90.0`, not `-90.000000`), which hid the number in the noise.

It is the same move as the catalog, applied to properties: **do not send what
can be resolved on the engine's side**. `Gravity Scale = 1.0` carries no
information at all -- it is the factory value, and the plugin knows that
locally. In a typical CDO, 95% of the properties are at their default.

The `# default X` comment only shows up where there was a change. It costs
nothing on unchanged lines, and puts the deviation -- the only thing worth
checking by eye -- in the spotlight. The count at the end exists so the
silence is explicit: without it, "did not show up" is ambiguous between *it is
at the default* and *the plugin cannot read it*.

**There is no separate step for listing the schema.** Looking for a property,
the filter goes in the same call and the answer already comes with the values:

```
sheet BP_Golem  ~ "walk" (4 of 218)
Max Walk Speed : Float = 250             # default 600
Max Walk Speed Crouched : Float = 300
Walkable Floor Angle : Float = 44.765
Ignore Base Rotation on Base : Boolean = false
```

One turn instead of two, four lines instead of eight hundred.

The way back is the same text without the comments, with the usual
discipline: a line that does not resolve is not guessed.

**Implementation:** `NodeScribeObjectReader` and `NodeScribeObjectWriter` as
mirrors, with a shared `NodeScribePropertyText` -- where the mirror cannot bend.
`TFieldIterator` walks; `Identical` against `GetArchetype()` detects the
deviation (deliberately: it is the same reference as the *Reset to Default*
arrow, so what the sheet calls changed is what shows up changed on screen);
`ExportTextItem` / `ImportText` cover structs, enums, arrays and references
without per-type code. The parser and the catalog's `Normalize` serve without
changes.

**Visibility filter -- decided:** a property goes in if it shows up in the
Details panel (`CPF_Edit`) **or** has a Get node in the Blueprint
(`CPF_BlueprintVisible`). Transient ones (`CPF_Transient`), which are not even
saved to disk, are left out.

Most have both flags, but not all, and the exceptions are precisely the ones
that matter. `ACharacter::bIsCrouched` is `BlueprintReadOnly` without `Edit`:
it does not show up in the panel -- it makes no sense to *type* whether the
character is crouching -- and it is read in the graph all the time. A
Details-only filter would answer "not found" for it.

Transient ones leave for the opposite reason: `APawn::LastHitBy` is
`BlueprintReadOnly, transient`, runtime state that never even gets saved to the
file. In a sheet it would be noise that changes by itself between two readings.

**Surface:** two tools, not six. `read_object` and `write_object`; the new doc
becomes a parameter-free part of `get_format_docs`, not one more tool. It is
not about the cost in the prompt -- with lazy discovery that cost barely
exists (see *The rest of the map*) -- it is so the model does not have to choose
between six similar doors, which is where it goes wrong and spends a turn
finding out it chose wrong.

### Behavior Tree and Blackboard

It left this list: it became the active front. The full plan -- format,
implementation walk and stage order -- is in **Roadmap: reading the enemy's
AI**, further up.

Only the note that did not fit there stays, about the native toolset. The
Engine's `AIModuleToolset` exists (7 tools) but **was not enabled in the
project that drives development**, and even enabled it would be read-only -- it
edits nothing. And its reading is among the worst: `list_nodes` returns UObject
references and `get_node_depths` returns a parallel array of integers the model
has to match in its head, and it still takes N `get_properties` calls to know
what each node is.

### The rest of the map

`EditorToolset` exposes **224 tools**, and the temptation is to think they
cost a fixed tax per message. **They do not.** This Engine's MCP server uses
lazy discovery: what stays in the prompt is three tools (`list_toolsets`,
`describe_toolset`, `call_tool`), and the 224 descriptions only show up when
someone asks for a specific toolset's.

That changes the strategy, for the better:

- **Turning off `EditorToolset` saves little by itself** -- the whole
  `list_toolsets` is ~600 tokens, and only when called. It is not a goal worth
  chasing on its own.
- **Each replaced tool pays off right away.** There is no need to cover 224
  before seeing a benefit. A sheet that avoids a 10,000-token dump saved 10,000
  tokens, with the other 223 in place.

The waste is in **use**, not in presence: big dumps, two-turn flows, and N
calls per object patterns. That is where the saving comes from, and it is what
the map below goes after.

| block | tools | destination |
|---|---|---|
| `object` | 6 | the sheet, above |
| `scene` + `actor` | 37 | world query with field projection |
| `asset` | 21 | same, same tool |
| `blueprint` | 53 | already this plugin's territory, plus variable and function CRUD |
| material, meshes, tables, texture | 107 | ← |

Those 107 of the long tail are per-asset-type editing -- and a Material
Instance parameter is a property, a Data Table row is a property, a mesh build
setting is a property. If the sheet is truly generic -- and it is, because
`TFieldIterator` does not know what a material is --, a good part of them go
away with no new tool at all. That is why the sheet came first: the 6 tools it
replaces are the mechanism that makes another ~107 unnecessary.

The token numbers above are estimates from the property count and the format
`StructToJsonSchema` produces, not measurements.

### Creating assets: measured, and **not worth it** for tokens

The question was whether creating assets should come here to save tokens. We
measured.

| | cost |
|---|---|
| `describe_toolset` of `AssetTools` (21 tools) | ~4,000 tokens, **once per session** |
| the call itself (`duplicate`, `move`, `save_assets`...) | ~50 tokens |

The cost is **discovery**, not use -- and bringing the operation here would only
avoid that discovery if we never needed anything else from that toolset. But
`find_assets`, `get_referencers`, `get_dependencies`, `move` and `delete` all
live there.

And above all: **that toolset has no fat payload to compress.**
`find_assets` returns a list of paths, `get_dependencies` too, `duplicate`
returns a boolean. It is the well-solved case -- no dump, no two-turn flow, no N
calls per object. By the question this README says to ask before building a new
tool -- *what does it let you turn off?* --, the answer is "nothing".

**There was a gap, but of capability, not tokens:** the native toolset has
`duplicate`, `move` and `delete`, and **no `create_asset`**. Creating a
BlackboardData or a BehaviorTree from scratch was not possible; only by
duplicating an existing one. That is why `create_asset` was built (item 2 in
*What is missing so clicks are not needed*, below) -- to be able to do
something new, and this README does not pretend it saves anything.

### `Target` came out of the reader and did not go into the builder -- **fixed**

Worse than the Cast, because it broke the round trip on a common node.
`Get Blackboard` was read as `Get Blackboard (Target = $controller)` -- and
writing exactly that failed, with a diagnostic that contradicted itself:

```
The node has no pin `Target`. Input pins: Target
```

The pin existed and the list showed it. `Get Blackboard` is a static function
whose parameter is literally called `Target`, and the builder only tried
`Target` as an alias for the self pin. It now tries the name as written first,
and the alias second. The test suite has a case for it
(`parameter_called_target`).

### Cast with a continuation did not come back the same

Found using the plugin for real. In an ability's `Gameplay Ability Graph`, the
chain after a `Cast to Character` came out **without a label and without a
warning**. The format doc says a node with more than one execution output stops
the chain on purpose and waits for a label -- but there the reader stopped and
did not say so.

The text read, pasted back, would leave the `Launch Character` unlinked: a graph
that compiles and does nothing. It is exactly the failure mode this project
exists to avoid, and it slipped through because reader and writer were
exercised on graphs without a Cast in the middle of execution.

The test suite now has a case for it (`cast_with_continuation`): an impure Cast
with both outputs linked, written, read, and pasted back. **It passes** -- the
reader writes the `then:` and `Cast Failed:` labels and the text pastes back the
same. The original report came from a real ability graph, so it is not closed
until that graph is read again; if it still comes out without a label, the
difference from the test case is the thing to look for.

### Listing a Blueprint's graphs -- **done**

`read_object` on a Blueprint now brings `# graphs: ...`. Without it the graph's
name was guesswork: `EventGraph` works almost always and fails without saying
why. One ability used `Gameplay Ability Graph`, with spaces, and another used
`EventGraph` -- two assets of the same type, created along different paths.
Without the listing, the only option was trying names until one worked.

### Where we are on that list

| item | status | what it blocks while missing |
|---|---|---|
| 1 -- Blueprint variables | **done** | -- |
| 2 -- Creating assets | **done** | -- |
| 3 -- Writing Behavior Trees | missing | every change to the tree |
| 4 -- Gameplay Effect components | missing | every new cooldown |
| 5 -- Creating Gameplay Tags | **done** | -- |
| 6 -- Reading fixes | missing | `Default Starting Data` |

With 1, 2 and 5 done, what is left for the Golem's AI to close without clicks
is 3 and 4. 3 is the biggest and the only one where a mistake saves an asset
that looks right and is empty -- that is why it goes last.

### What is missing so clicks are not needed

Gathered by reviewing a whole session of real work, where building a GAS
mechanic required twelve "click here" requests. The list is what was missing.

**First: not everything that was asked for needed asking.** `Cooldown Gameplay
Effect Class` and `Activation Owned Tags` are properties -- `write_object`
already handles them, including `GameplayTagContainer`, which accepts the
canonical form `(GameplayTags=((TagName="X")))`. Those were clicks asked for
because it had not been tested. Worth checking whether the sheet already covers
it, before proposing a tool.

In order of what blocked the most:

**1. Blueprint variables -- create, delete, mark Instance Editable.**
**Done.**

```
variable Range : Float editable = 800
variable Ability Class : GameplayAbility Class editable
variable Tag List : Array of Name
delete variable Internal Counter
```

It fitted in the syntax that already existed: `variable X : Type` was read and
ignored on writing, and now creates it when it does not exist. `editable`
closes the declaration, like `synced` on the blackboard, and **the reader emits
it back** -- without that a round trip would silently erase the flag, and the
variable vanished from the panel of whoever uses the Blueprint.

**Deleting has a word of its own.** This writer deletes nothing on principle,
and deleting a variable breaks every node that used it. It cannot happen
through a formatting slip.

**An existing type is not changed.** Changing the type of a variable in use
breaks the nodes that consume it; that calls for a decision, not a side effect.

Two things only the real test showed:

- **Creating a variable requires recompiling the Blueprint.**
  `AddMemberVariable` touches the list, but the property only exists in the
  class after `FKismetEditorUtilities::CompileBlueprint`. Without that the
  freshly created variable showed up neither in the sheet nor in the panel, and
  the caller would have to ask for a click on Compile. Creating something that
  cannot be seen is worse than not creating it.
- **`X Class` was not a type.** `ResolvePinTypeFromName` only produced object
  references, so `GameplayAbility Class` -- exactly what a BTTask needs to point
  at an ability -- failed. The suffix now becomes `PC_Class`, and
  `DescribePinType` emits it back. It was a hole in the mirror nobody had run
  into because no tested graph declared a class variable.

**2. Creating assets.** **Done.**

```
create_asset("/Game/MyGame/AI/Golem/Behavior/BTTask_UseAbility",
             "BTTask_BlueprintBase")
```

**It is capability, not savings** -- the native toolset has no `create_asset`
either, only `duplicate`. It was built to be able to do something nobody could,
and this README does not pretend it saves tokens.

Two paths, decided by `FKismetEditorUtilities::CanCreateBlueprintOfClass`:
what is Blueprintable -- `GameplayEffect`, `BTTask_BlueprintBase`,
`BTService_BlueprintBase` -- becomes a Blueprint with that parent; the rest --
`BlackboardData`, `BehaviorTree` -- goes through the type's factory. The factory
comes from a sweep, not a table, for the same reason as the blackboard key
types: plugins and projects may bring their own.

Two guards, and the second almost did not exist:

- **Only inside `/Game/`.** Writing into `/Engine` from a line of text is the
  kind of accident that cannot be undone.
- **Never overwrites** -- and the first version did overwrite.
  `DoesPackageExist` only looks at the disk, and a freshly created asset that
  was not saved yet lives only in memory, which is exactly the state it stays
  in. Calling twice in a row went straight past the guard and recreated it on
  top. Now it checks `FindPackage` too.

The asset stays dirty, unsaved, like any freshly created one in the editor --
whoever created it by mistake closes without saving.

**3. Writing Behavior Trees.** It is stage 5 of the roadmap, and the one that
weighs the most: without it, every change to the tree is a click. The asset
keeps the execution hierarchy *and* a `UBehaviorTreeGraph` that has to stay in
sync -- writing only the runtime side gives an asset that runs and shows up
empty on screen.

**4. Gameplay Effect components.** `Grant Tags to Target Actor` is an
instanced subobject inside `UGameplayEffect::GEComponents`, and `write_object`
does not create subobjects inside arrays. Without that, every new cooldown is a
click. It works as a general case: **arrays of instanced subobjects** show up in
many places in the Engine.

**5. Creating Gameplay Tags.** **Done.**

```
read_tags("Cooldown")        →  tags  ~ "Cooldown" (1 of 31)
                                tag Cooldown.Golem.Jump

write_tags("tag Cooldown.Golem.Laser", "MyGame.ini")
                             →  1 tag(s) created in MyGame.ini.
```

`Cooldown.Golem.Jump` had to exist before being used, and a tag is neither an
asset nor a property -- it lives in an ini --, so neither `create_asset` nor the
sheet reached it. What creates it is
`IGameplayTagsEditorModule::AddNewGameplayTagToINI`, from the editor module;
not `UGameplayTagsManager`, where it is not.

It accepts `tag X` or just `X`, and **skips the header the reader emits** --
without that the round trip failed on a line written by the plugin itself. It
neither deletes nor renames: both break every asset that uses the tag, and that
calls for a decision, not a formatting side effect.

Three things only the real test showed:

- **"Already exists" has to be `IsDictionaryTag`, not `RequestGameplayTag`.**
  The question is whether the tag was *declared*, not whether it resolves.
  `Cooldown.Golem` resolves because `Cooldown.Golem.Jump` exists, but it is not
  declared in any ini -- and the reader, which only lists declared ones, would
  never show it. The first version skipped it because it resolved: you asked for
  the tag, heard "already existed", and it did not show up in the listing. The
  Engine itself makes that distinction, and for the same reason.
- **The target ini is not the same as the others.** With an empty source the
  Engine writes to `DefaultGameplayTags.ini`, while a project may keep its tags
  in `Config/Tags/MyGame.ini`. The `source` parameter chooses, and the return
  **says which file it landed in** -- the destination going unsaid would spread
  the tags over two places without anyone noticing. A source that does not
  exist is refused with the list of the ones that do, instead of becoming a new
  ini through a typo.
- **Do not guess why the Engine refused a name.** The first version said
  "accepts letters, digits, dot and underscore" -- and the project that drives
  development has accented tags such as `Facção.Inimigos`. Accents pass; what
  counts is the project's `InvalidTagCharacters`. `IsValidGameplayTagString`
  returns the Engine's reason **and a corrected name to suggest**, and that is
  what comes out: ``Test,Comma (Tag may not contain the following characters: ,
  try `Test_Comma`)``.

**6. A reading defect**, which is not a feature but a fix: an
AbilitySystemComponent's `Default Starting Data` is not readable by the sheet
-- and it is precisely where you find out which abilities a character has.

### What is not worth bringing here

The native toolset already solves it well, and duplicating only adds surface:

| | why it is already good |
|---|---|
| moving, duplicating, deleting, finding assets | the payload is a list of paths; measured in *Creating assets* |
| saving, source control state, dirty | a boolean per call |
| console variables, PIE control, viewport camera | thin, one call and done |
| reading the output log | text that is filtered at the source; nothing to compress |
| placing an actor in the level | returns a reference, no dump |
| material, mesh, texture, data asset properties | **the sheet already covers it** -- they are properties, and `read_object`/`write_object` are generic |

That last line is the one that paid off the most: most of `EditorToolset`'s
~107 per-asset-type editing tools are properties -- and the sheet reaches them
without a line of per-type code, because `TFieldIterator` does not know what a
material is.

**Still open, and worth it:** world queries with field projection --
`find_actors` returns references and every attribute is another call, so fifteen
actors become fifty calls. It is the only item of the original map that is
still worth doing and was never done.

### Sending only what changed

Today, editing one node in a 40-node graph costs the whole graph in each
direction: read everything, return everything, rewrite everything.

The idea is for `read_graph` to return a state identifier together with the
text, and for `write_graph` to accept only the changed lines plus that
identifier. If the graph changed along the way, the write is refused instead of
overwriting blind.

**Why it is worth it:** it is the only compression that scales without costing
readability. The others -- shortening names, using numeric identifiers -- save
~1% of the total cost (the text is already ~2% of it; the rest was discovery,
and that has already been eliminated) and destroy the ability to check what the
AI wrote. Sending fewer lines saves orders of magnitude, and the few that
travel stay readable text.

The gain shows up on big graphs and small edits -- which is exactly the common
case once the graph exists.

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
