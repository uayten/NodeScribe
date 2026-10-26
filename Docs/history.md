# NodeScribe: how it got here

What each version changed, and the measurements behind the choices. Moved
out of the README so it stops growing with every release; the README keeps
only the current state. Decisions that still hold are in
[DECISIONS.md](../DECISIONS.md).

## The format is English-only

The format's keywords used to accept Portuguese and English side by side
(`evento`/`event`, `verdadeiro`/`true`, `variavel`/`variable`). They are now
English only: `event`, `true:`, `false:`, `variable`, `state`, `conduit`,
`key`, `sheet`, `delete variable`, `Map of X to Y`, and so on -- the full list
is at the end of [`Docs/FORMAT.md`](FORMAT.md#keyword-reference). **Texts
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

## What 0.4 changed: the reader stopped lying

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

## Replacing the graph, and how far the guard sees

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

## What 0.4 changed: the rest

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

## `variable X : Actor` created a struct

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

## Where we are

Working today: **graphs** (`read_graph` / `write_graph`), **single-object
sheets** (`read_object`, default and filtered mode), the **Widget Designer**
(the `designer` section of the same two tools) and **Gameplay Tags**
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

## Roadmap: reading the enemy's AI

The real case's assets live in the project that drives development: a
Behavior Tree, its Blackboard, an AI controller, a service and a patrol task,
plus four Gameplay Abilities. The examples below are that project's output,
with its asset names translated.

**An assumption that proved false and saves work:** BT and Blackboard do
**not** depend on components in the sheet. A BT node is a UObject with
properties and nothing more. Components are only missing for the AI
controller and for tuning the Golem's movement, and that is why they moved
down the queue.

### Stage 0 -- Abilities: done, no new code

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

### Stage 1 -- Blackboard keys -- **done**

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

### Stage 2 -- The BT tree -- **done**

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

### Stage 3 -- Components in the sheet -- **done**

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

### Stage 4 -- Blueprint variables with the `variable` line -- **done**

They come out in a block of their own, with the type, before the inherited
properties. A variable declared by the Blueprint itself **always** shows up,
even at its factory value: it does not exist in the parent class, so its
existence already is the information.

### Stage 5 -- Writing -- `write_object` **done**

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

### Stage 5 -- Writing -- blackboard **done**

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

### Stage 5 -- Writing -- Behavior Tree **done**

`write_object` on a Behavior Tree takes the same text the reading gives. The
asset keeps the execution hierarchy *and* an editor graph
(`UBehaviorTreeGraph`); writing only the runtime side gives an asset that runs
and shows up empty on screen. So the writer edits the **graph** -- the same
steps as the editor's "add node" -- and the runtime tree is rebuilt from it by
`UpdateAsset`.

**The text is the whole tree**, unlike the sheet and the blackboard: a shape
only exists as a whole. A node the text does not mention leaves, and the
return names it. A loose branch, which the reading shows commented, is hooked
to the root by writing it uncommented in its place -- the case that motivated
this stage.

Decisions that are not obvious:

- **All or nothing.** Every line is resolved on scratch instances before the
  graph is touched. Half a tree runs, and it is the wrong tree.
- **A node that already reads exactly as its line is kept**, instance and all,
  with whatever the text cannot say. A parameter that does not parse back
  (an object default the reader writes by short name) is forgiven only then.
- **Same class with other parameters keeps the graph node and swaps the
  instance.** With two of the same class, the one already under the same
  parent wins -- taking the first in the graph reported the wrong one as
  removed.
- **The reading had to change first.** A node renamed in the editor came out
  as its name only -- `CombatState` -- and a name does not say it is a
  Selector. It now comes out `CombatState = Selector`, the shape that names a
  state machine.
- **Key checks.** A key selector or a `Key: Name` value naming a key the
  blackboard lacks, or of a type the value cannot read, is refused. The first
  version let `Wait Time = Key: AttackTarget` (an Object key) through; the
  Engine dropped the key on the next load, silently.
- **Layout.** When shape and order are unchanged the hand layout stays.
  Otherwise the tree is laid out left to right in the text's order, which is
  also the execution order. The editor's Auto Arrange is not used: it
  measures node widgets, which only exist with the tree open on screen.
- **Refused, not guessed:** Simple Parallel (the text cannot say which child is
  the main task), composite decorators and decorators injected by a subtree.

Exercised on a scratch tree: created from nothing (14 nodes), read back
identical, written again with zero changes, reordered with a parameter
changed and two nodes removed, and four kinds of error refused with nothing
changed. Then on the real case: the enemy's patrol branch hooked to the root.

### Off the AI front -- the Widget Designer, read and write

Not on the roadmap: it came from a real session that had to add two rows to a
Widget Blueprint. NodeScribe wrote the graph, and the rows had to go through
the native `UMGToolSet` -- a ~62 KB schema, plus `ObjectTools.list_properties`
per widget to see a text or a font, estimated at 15-25k tokens for two rows.

`read_object` on a Widget Blueprint now ends with a `designer` section, and
`write_object` takes it back. The format is in
[`Docs/FORMAT.md`](format/designer.md); the decisions:

- **A widget is a component, one level down.** `Name : Class`, its changed
  properties under it, children too -- the sheet's own vocabulary, so there
  is no second format to learn. The sheet's `X = None` line per widget
  variable is gone: the section says what each one is and where.
- **A list of changes, not the whole tree** -- unlike the Behavior Tree. A
  Designer has dozens of widgets; making the whole tree the text would make
  "add a row" cost the whole screen, and every widget the text forgot would
  be deleted. Existing widgets are anchors; a new name creates one, placed by
  its neighbours in the text.
- **`like Other`** is what makes "a row like that one" short: the look of an
  existing widget, slot included, onto the new one. Instanced subobjects are
  not copied -- two widgets would share one.
- **Is Variable never changes by omission.** An anchor written without the
  word must not untick it -- that would break every graph node that reads the
  widget. Unticking is `Is Variable = false`.
- **The structure is all or nothing; properties are line by line**, through
  the sheet's own writer (`WriteProperties`). A row half created is a row in
  the wrong place; a font size that did not parse is one line to fix.
- **The editor's operations, not hand-made ones.** Create, add, rename and
  delete go through `FWidgetBlueprintOperationUtils`, the same as the
  Hierarchy panel: a rename takes the graph's references along, and a widget
  is created as the Palette creates it.
- **Refused for now, out loud:** moving a widget to another parent,
  reordering existing siblings, changing a class, named slot content.

Exercised on a real Widget Blueprint of another plugin (read only): 37
widgets, the whole Designer in ~4 KB. On a scratch Widget Blueprint: a tree created from nothing, a
Widget Blueprint child found by name without being open, a row added `like`
another one in a 300-character text and landing between its neighbours, the
whole reading pasted back with nothing changed, and a move plus an unknown
class refused together with nothing changed. `designer_writes_rows` in the
suite does the same by itself.

Also found on the way: a struct that has no text form of its own came out as
"I do not know how to write the value of" -- every TextBlock's `Font`. It is
now opened member by member, the same as a struct that is too long.

Found on the way: `create_asset` with parent `UserWidget` used to give a plain
Blueprint with no Designer -- the generic factory accepts the parent and
produces something nobody can place a widget on. It now uses the Widget
Blueprint factory.
