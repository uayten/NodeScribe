# NodeScribe: possible future features

Proposals and measurements, not plans. An idea becomes work only when it is
accepted into [ROADMAP.md](../ROADMAP.md), with its proposal answering the
four questions in *Proposing a new tool*, in [development.md](development.md).

## Our own MCP: talking straight to the client

Today NodeScribe's tools reach the client through three of Epic's layers: the
`ModelContextProtocol` plugin (the HTTP server), `ToolsetRegistry` (the
registry) and `AllToolsets`/`EditorToolset` (which expose the three generic
tools). The client sees `list_toolsets`, `describe_toolset` and `call_tool` --
and every `read_graph` travels inside a `call_tool`.

The proposal is for the plugin to speak MCP on its own. **In two stages, and
the first stands on its own** -- the second only changes who hosts it.

### Stage 1 -- registering the tools directly

`IModelContextProtocolModule::GetChecked().AddTool(...)` accepts any
`IModelContextProtocolTool`: name, description, input JSON Schema and a `Run`.
Those are exactly the four things the tool already has, written in Python
today. Registered directly, `read_graph` becomes a first-class tool in the
client, and the whole Python layer goes away.

### Stage 2 -- serving the protocol

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

### Where the token leaks today

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

### What it lets you turn off

That is the deciding question. With stage 1: `ToolsetRegistry`,
`PythonScriptPlugin` (for us) and the whole `toolset_registry` layer -- the
`__pycache__` that dirties the repository on every run goes along with it. With
stage 2: `ModelContextProtocol` too.

Turning off `AllToolsets`/`EditorToolset` is **a separate decision, and it has
a price**: asset `duplicate`, `move` and `delete` go along, which a project
uses and NodeScribe does not replace. Measured in *Creating assets*: ~4,000
tokens of discovery once per session and ~50 per call.

### What improves besides tokens

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

### What stays the same, and there is no point pretending

[development.md](development.md) already records that a server of our own "is not
worth it" -- that was said about replacing the **command file**, which exists
precisely for when the network goes down. The argument survives here: stage 2
does not remove the risk of having a network protocol up, it only changes its
owner. `Saved/NodeScribe/command.txt` stays the path that depends on nothing,
and it does not go away.

### Cost and order

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

## The sheet: object properties as text

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

## Behavior Tree and Blackboard

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

## The rest of the map

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

## Creating assets: measured, and **not worth it** for tokens

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

## `Target` came out of the reader and did not go into the builder -- **fixed**

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

## Cast with a continuation did not come back the same

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

## Listing a Blueprint's graphs -- **done**

`read_object` on a Blueprint now brings `# graphs: ...`. Without it the graph's
name was guesswork: `EventGraph` works almost always and fails without saying
why. One ability used `Gameplay Ability Graph`, with spaces, and another used
`EventGraph` -- two assets of the same type, created along different paths.
Without the listing, the only option was trying names until one worked.

## Where we are on that list

| item | status | what it blocks while missing |
|---|---|---|
| 1 -- Blueprint variables | **done** | -- |
| 2 -- Creating assets | **done** | -- |
| 3 -- Writing Behavior Trees | **done** | -- |
| 4 -- Gameplay Effect components | missing | every new cooldown |
| 5 -- Creating Gameplay Tags | **done** | -- |
| 6 -- Reading fixes | missing | `Default Starting Data` |

With 1, 2, 3 and 5 done, what is left for the Golem's AI to close without
clicks is 4.

## What is missing so clicks are not needed

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

## What is not worth bringing here

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

## Sending only what changed

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
