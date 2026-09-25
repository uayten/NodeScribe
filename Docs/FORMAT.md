# NodeScribe format

One line = one node. Nothing else.

Everything that is **not** written here is the plugin's job: node positions,
execution links, implicit casts between types, internal function names.

---

## Contents

- [The basics](#the-basics)
- [Keeping a result](#keeping-a-result)
- [Declaring variables](#declaring-variables)
- [Arguments](#arguments)
- [When two nodes share a name](#when-two-nodes-share-a-name)
- [Branches (Branch, loops)](#branches-branch-loops)
- [Switch](#switch)
- [Events](#events)
- [Input Action events](#input-action-events)
- [Dispatchers](#dispatchers)
- [Select](#select)
- [Cast](#cast)
- [Variables](#variables)
- [Structs](#structs)
- [Subsystems](#subsystems)
- [Creating widgets and spawning actors](#creating-widgets-and-spawning-actors)
- [Standard macros](#standard-macros)
- [Other names the plugin knows](#other-names-the-plugin-knows)
- [Holes: what the plugin does **not** decide for you](#holes-what-the-plugin-does-not-decide-for-you)
- [AnimGraph](#animgraph)
- [Comments](#comments)
- [What the parser ignores by itself](#what-the-parser-ignores-by-itself)
- [The way back](#the-way-back-1)
- [When it cannot be done](#when-it-cannot-be-done)
- [Keyword reference](#keyword-reference)

---

## The basics

```
Get Player Controller
Print String (In String = "hello")
```

Two lines → two nodes, already linked in the order they appear.

## Keeping a result

Use `name =` to name a node's output and reuse it with `$name`:

```
pc = Get Player Controller
Enable Input (Target = $pc)
```

`$name` also works directly with the Blueprint's variables, without declaring
anything -- the plugin creates the Get node automatically:

```
Print String (In String = $PlayerName)
```

### When the node has more than one output

A bare `$name` takes the main output. If the node has several -- an event with
parameters, a struct Break, a function with *out params* -- say which one with
`$name.Pin`:

```
key = event OnKeySelected
Map Player Key (New Key = $key.Selected Key Key)
```

If the pin does not exist, the plugin lists the outputs the node really has.

This also reaches **part of a struct**. If you ask for `$key.Selected Key Key`
and the node only has the whole `Selected Key` pin, the plugin splits the
struct (the same as *Split Struct Pin*) to find the requested part.

## Declaring variables

```
variable Total Jumps : Integer = 3
variable Jump Duration : Float = 1.0
variable Jump Timer : Timer Handle
variable Inputs : Array of Name
variable Mirrors : Map of Int64 to BP_Mirror
variable Already Seen : Set of Name
```

The variable is created before the nodes that use it. If it already exists,
the line is ignored -- pasting the same text twice does no harm.

The type is the name shown in the interface: `Float`, `Integer`, `Boolean`,
`Name`, `Text`, `String`, any struct (`Timer Handle`, `Vector`), any enum
(`EPlayerMappableKeySlot`) and any class (`BP_Golem`). `BP_Rock Class` is a
reference to the class itself, not to an instance.

Collections: `Array of X` for a list, `Set of X` for a set, `Map of X to Y` for
a map. A map's key and value cannot be collections -- Unreal has no
`TMap<int, TArray<X>>` --, and a line asking for that is refused instead of
becoming something else.

**Designer widgets are not declared.** A `ScrollBox` on the screen becomes a
variable when it is placed in the Designer with *Is Variable* ticked --
declaring one with the same name would give a variable that compiles and never
points at the widget. The plugin refuses and says so.

## Arguments

Between parentheses, separated by commas. Accepts the pin name or the position:

```
Print String (In String = "hi", Duration = 5.0)
Print String ("hi", 5.0)
```

Names are compared tolerantly -- `In String`, `instring` and `In_String` are
the same. `Target` points at the self pin.

The comparison also ignores accents, and that holds for every name in the
format: pin names and variable names (`$Duracao` finds `Duração`, and the
other way round). Names come from the project, and the project may not be
written in English.

## When two nodes share a name

`Apply Settings` exists in `GameUserSettings` and in `EnhancedInputUserSettings`.
In that case the plugin does not choose -- you say which with `Class.Function`:

```
EnhancedInputUserSettings.ApplySettings (Target = $settings)
```

Parentheses do **not** work for this: they already are the argument list.

## Branches (Branch, loops)

Indent and use a label ending in `:`.

```
Branch (Condition = $bIsAlive)
  true:
    Print String (In String = "alive")
  false:
    Print String (In String = "dead")
```

Accepted labels: `true`/`then`, `false`/`else`, `loop body`/`loop`,
`completed`. Any other name is compared directly with the output pin's name.

When a node has more than one execution output and you do **not** open a
label, the chain stops there on purpose -- choosing a branch for you would be
guessing.

## Switch

```
Switch on EJSL4UBatteryLevel (Selection = $level)
  Empty:
    Print String (In String = "no battery")
  Full:
    Print String (In String = "full")
```

The labels are the enum's values. Also `Switch on Int`, `Switch on String` and
`Switch on Name`.

## Events

```
event BeginPlay
event MyAbilityActivated
```

If the name exists in the parent class, it becomes the override event. If it
does not, it becomes a Custom Event with that name (and the plugin says it did
that).

For the event of **another object's dispatcher** -- the one you create by
right-clicking a child widget and choosing its dispatcher:

```
key = event OnKeySelected of SelectInputKey
Print String (In String = $key.Selected Key Key)
```

`of` separates the dispatcher's name from the variable that exposes it. If the
variable does not exist, or does not have that dispatcher, the plugin lists the
ones it has instead of creating a loose event.

If the name is an event **that exists in the Engine, but not in this class** --
the classic case is `BeginPlay` in a Widget Blueprint, which only has
`Construct` -- the warning goes up to **yellow** and lists the events the parent
class really offers. A Custom Event called `BeginPlay` compiles, looks right in
the graph, and never fires; it is the kind of mistake that only shows up in
playtest.

## Input Action events

```
EnhancedInputAction /Game/MyGame/Player/Inputs/IA_Attack.IA_Attack
  Started:
    Print String (In String = "attacked")
```

The labels are the triggers: `Started`, `Triggered`, `Completed`, `Canceled`,
`Ongoing`.

It accepts the short name (`IA_Attack`) if the asset is already loaded in the
editor, but the full path is what always works.

## Dispatchers

```
Call OnHealthChanged (New Health = $health)
Bind OnHealthChanged
Unbind OnHealthChanged
Clear OnHealthChanged
```

Without `Target`, the dispatcher belongs to this Blueprint. With
`Target = $obj`, it belongs to the object pointed at. For a dispatcher's
**event**, see `event X of $Variable`.

## Select

```
Select (Index = $bIsAlive, Option 0 = "dead", Option 1 = "alive")
```

## Cast

```
boss = Cast to BP_Boss (Object = $actor)
```

## Variables

```
Set Health (Health = 100)
health = Get Health
```

It is only treated as a variable if it exists in the Blueprint. That is why
`Get Player Controller` stays the function, not a variable called
"Player Controller".

### A variable of another object

Pass `Target`, and the plugin finds the variable in its class:

```
pc = Get Player Controller
Set Show Mouse Cursor (Target = $pc, Show Mouse Cursor = true)
```

The name can be the one shown on screen: `Show Mouse Cursor` finds
`bShowMouseCursor`.

### A variable that does not exist yet

A `$Something` that does not exist **does not lose the link**: the Get node
goes in anyway, with the type of the pin that was going to consume it. The
Blueprint flags the error and right-clicking the node offers to create the
variable -- the same behaviour as pasting nodes between two different
Blueprints.

A yellow warning comes out. The Blueprint still does not compile until you
create the variable; what changes is that the pending item stays in the graph,
one click away from being solved, instead of becoming a lost chain.

This holds for `$name` used as an argument. A loose `Get X` line, with `X`
nonexistent, is still an error -- there is no consuming pin there to take the
type from.

## Structs

```
args = Make MapPlayerKeyArgs (Mapping Name = $Name, Slot = First)
Break Vector ($position)
```

In a `Make`, each pin has the field's name. A `Break` has a single input pin --
that is why the unnamed, positional form is the one that always works.

**Some structs bring their own make and break function**, and for them the
plugin uses that function instead of the generic node: `Vector`, `Rotator`,
`Transform` and `Color` are in that case. The generic node there compiles with
an Engine warning -- *"the structure cannot be broken using generic 'break'
node"* --, and that warning could not be avoided by whoever writes the text,
since the format has no way to choose between the two nodes.

Only the input pin's name changes, becoming the function parameter's (`In Vec`,
not `Vector`). The positional form works across both.

It accepts the internal name (`MapPlayerKeyArgs`) or the display name
(`Map Player Key Args`). It only becomes a struct node if the struct exists --
so `Make Literal Int` stays the function it always was.

## Subsystems

```
settings = Get EnhancedInputLocalPlayerSubsystem (PlayerController = $pc)
```

The plugin picks the right node depending on where the subsystem lives:
LocalPlayer ones ask for a PlayerController, Engine ones ask for nothing.

## Creating widgets and spawning actors

```
row = Create Widget (Class = WBP_RemapRow, Owning Player = $pc, Input Name = $name)
Spawn Actor from Class (Class = BP_Boss, Spawn Transform = $t)
```

Also `Construct Object from Class`.

In these nodes the `Class` pin comes first **by necessity**: the *Expose on
Spawn* pins only exist after the class is chosen. The plugin takes care of
that by itself -- you can write the arguments in any order. Without `Class`,
the node goes in without those pins and a warning comes out.

## Standard macros

Names from the Engine's library work directly:

```
For Each Loop (Array = $Enemies)
  loop body:
    Print String (In String = "an enemy")
```

Also: `Do Once`, `Flip Flop`, `Gate`, `While Loop`, `Multi Gate`, `Is Valid`,
`Switch Has Authority` (in Actor and Actor Component Blueprints).

## Other names the plugin knows

`Return` is a function graph's return node. `To Text` converts any value into
Text. `Self` is the reference to this Blueprint.

An **async action** -- the nodes the Engine and plugins expose through
`UBlueprintAsyncActionBase`, with one execution pin per event -- goes in by the
name shown in the graph, and each output is an indented label:

```
Wait For Any Controller Changes
  On Connected:
    Print String (In String = "connected")
  On Disconnected:
    Print String (In String = "dropped")
```

The pin that continues right away is called `then`, and it is a label like any
other.

## Holes: what the plugin does **not** decide for you

Write `?` on a pin whose value is your choice:

```
Spawn Sound 2D (Sound = ?)
```

The node goes in and the pin stays empty. Some pins block compilation; others
compile with a null value and only fail at runtime. In both cases the pending
item is yours and visible -- better than a guessed asset, which goes unnoticed
and becomes a playtest bug.

**Check the pins with `?` before pressing Play.** Not every empty pin shouts.

Assets can also be passed by full path, if you know it:

```
Spawn Sound 2D (Sound = /Game/MyGame/Audio/SFX_Hit.SFX_Hit)
```

A bare name that is not a path is **not** accepted -- the plugin warns and
leaves the pin empty instead of guessing which asset it was.

## AnimGraph

In an animation graph the same rules apply, with one difference: the flow is
not execution, it is **pose** -- and it does not continue, it *feeds*. The
chain ends at the **Output Pose**, which already exists in the graph and is
never created.

```
Idle_Standing
```

A single line. `Idle_Standing` is the name of an AnimSequence in the project,
and it becomes the Sequence Player with the asset already filled in. The link
into the Output Pose is the plugin's -- like every link the format does not
write.

**A bare asset name is accepted here, and only here.** Outside the AnimGraph
the plugin refuses it, because there is no obvious node to wrap the asset. Here
there is exactly one, and it is the same one dragging the asset into the graph
produces: AnimSequence becomes Sequence Player, BlendSpace becomes BlendSpace
Player, and so on. Two assets with the same short name do not become a choice
-- the list of paths comes out.

An anim node goes in by the name shown in the graph's menu:

```
BS_Locomotion (Speed = $Speed)
Apply Additive
```

Two lines, two poses: the BlendSpace feeds the Apply Additive, which feeds the
Output Pose. Consecutive, they link in the order they appear, same as the
EventGraph.

### What is not a pin

Not everything that changes what an anim node does is a pin. An asset player's
`Loop Animation` and `Play Rate` live in the details panel, and go in as
arguments all the same:

```
MM_Jump (Loop Animation = false, Play Rate = 1.5)
```

It is worth writing `Loop Animation` whenever the animation is not a loop: it
**is born on**, so an `MM_Jump` that should play once keeps repeating with
nothing in the text saying so. The reading writes back every option that
differs from a freshly created node.

An option does not accept a `$reference`: it is a fixed value, because there is
no wire to link.

### Indentation, here, opens an input

It is the reverse of the EventGraph. There the indented label opens an
**output** -- what happens next. Here it opens a pose **input** -- what feeds
the node:

```
Blend Poses by bool (Active Value = $bStanding)
  True Pose:
    Idle_Standing
  False Pose:
    BS_Locomotion (Speed = $Speed)
```

The label is the pin's name. A block may have several lines: they chain among
themselves, and the block's result -- the last line -- is what goes into the pin.

```
  False Pose:
    BS_Locomotion (Speed = $Speed)
    Apply Additive
```

Here `Apply Additive` is what feeds `False Pose`.

### Output Pose

No need to write it: the end of the chain links into it by itself. Writing it
works and is useful when you want to make explicit where the chain ends:

```
Idle_Standing
Output Pose
```

If the graph has no Output Pose, the plugin warns and **does not create
another** -- it is born with the AnimGraph, and if it is gone the graph itself
is wrong.

An empty pose input comes out as a warning. It breaks nothing: it compiles,
runs, and the character stays in the reference pose, arms spread. It is the
silent hole of this kind of graph.

### State machine

```
Locomotion = State Machine
  state Idle:
    Idle_Standing
  state Running:
    BS_Locomotion (Speed = $Speed)
  Idle -> Running:
    KismetMathLibrary.Greater_DoubleDouble (A = $Speed, B = 10.0)
  Running -> Idle:
    KismetMathLibrary.LessEqual_DoubleDouble (A = $Speed, B = 10.0)
```

The comparisons are written qualified on purpose. `Greater` exists for every
numeric type (`Greater_DoubleDouble`, `Greater_IntInt`, `Greater_ByteByte`...),
so the bare name is ambiguous and the plugin refuses it with the list of
candidates; `Class.Function` picks one. It is also what the reading writes.

The `name =` names the machine -- a state machine's name is its sub-graph's,
and without it every machine would be born "New State Machine".

`state Name:` opens a state, and the block is the AnimGraph inside it, with the
same rules as everything above. The `state` prefix is mandatory: without it,
the label would be indistinguishable from a pose input.

`From -> To:` opens a transition, and the block is its rule -- a data graph
that ends in a bool. The block's last line is what goes into
`Can Enter Transition`; linking that is the plugin's job. A `transition` prefix
in front (`transition From -> To:`) is accepted and changes nothing.

**The first state declared is where the machine starts.** It is the only
possible reading without inventing syntax: in the graph the Entry points at a
single state.

States are created **before** any transition, so the order in the text does not
matter -- the transitions can be written first. In exchange, a transition that
mentions a state that does not exist is an error, with the list of the ones
that do, instead of an empty state created by mistake.

In a rule, a variable alone is enough. The three forms below give the same node:

```
    $Is In Air
    Get Is In Air
    Is In Air
```

### State and transition options

What lives in the details panel goes between parentheses, **before** the colon
-- the same syntax as `MM_Jump (Loop Animation = false)`:

```
  Landing -> Ground (Automatic Rule Based on Sequence Player in State = true):
```

That is the one that matters most. With it on, the transition fires by itself
when the source state's animation is ending -- and that is why it **is born
with no rule at all**. It is how Epic writes `ABP_Unarmed`'s
`Land -> Locomotion`.

Without it, a transition with an empty block is a transition that compiles and
never fires, and a warning says so. The two look identical on screen; the
difference lives in the option, and that is why it needs to fit in the text.

All the panel's options go in here: `Duration` (the blend time),
`Priority Order`, `Blend Mode`, `Min Time Before Re-entry`. The reading writes
what differs from a freshly created transition. A state takes its own the same
way, and so does a conduit.

An option does not accept a `$reference`: it is a fixed value, because there is
no wire to link.

### Conduit

A conduit is a crossing: **a single rule**, which several transitions go
through, instead of each repeating the same condition.

```
  conduit To Air:
    Get Is In Air
  Ground -> To Air:
  To Air -> Jump:
    KismetMathLibrary.Greater_DoubleDouble (A = $Velocity Z, B = 100.0)
  To Air -> Fall:
    KismetMathLibrary.LessEqual_DoubleDouble (A = $Velocity Z, B = 100.0)
```

A conduit's block is a **rule**, not a pose. That is the difference that calls
for a word of its own: `conduit X:` and `state X:` would be indistinguishable in
the text, and the plugin would create the wrong node -- a state without a pose,
which is the reference pose.

In transitions it is an end like any other.

### Alias

An alias is a nickname for several states at once: a transition leaving it
leaves all of them, without repeating the rule on each one. The block is the
list of states, one per line.

```
  alias In The Air:
    Jump
    Fall
  In The Air -> Landing:
    NOT Boolean (A = $Is In Air)
```

It is what Epic uses in `ABP_Unarmed`: `To Falling` and `To Land` are aliases,
not states.

An alias only points at a `state`. A conduit or another alias in the list is
refused -- the Engine sweeps the graph for states when rebuilding the
references, and whatever is not a state **vanishes on the next save**, with no
error and no warning, taking along the transition that left from there.

`alias Name (Global Alias = true):` covers every state of the machine, and then
the block stays empty.

### The state machine getters

Inside a transition rule there is a vocabulary that only exists there:

```
  Jump -> Fall:
    t = Get Relevant Anim Time Remaining
    KismetMathLibrary.Less_DoubleDouble (A = $t, B = 0.1)
```

`Get Relevant Anim Time Remaining`, `Get Relevant Anim Time Remaining Fraction`,
`Get Transition Time Elapsed` and the other getters are not function calls,
even though they show up as one in the editor's menu. What makes them work is
not on any pin: it is the **transition's source state**, which the plugin fills
in by itself, because the transition knows where it leaves from and the text
would have no way to say it.

That is why they only exist inside a rule. Outside it the name falls into the
function catalog and finds the function of the same name in
`UAnimationStateMachineLibrary` -- which exists, is public, goes into the graph,
and asks for two pins a transition rule has nothing to feed from. It was a
plausible node that does not compile, which is exactly what the plugin promises
not to do.

### The way back

**Copy Whole Graph** on an AnimGraph writes in this same format. Two things do
not come back the same, and both come out with a warning:

| Situation | What happens |
|---|---|
| The same pose feeding two places | anchor on both ends: the format is a tree, and the second link is lost on pasting |
| An anim node that has an asset but does not come back through it (a Sequence *Evaluator*) | the node's title comes out + a warning: the asset is not in the text |

The Output Pose and a transition's result do not become lines in the reading --
they already exist in the target graph, and the link into them is the plugin's.

---

## Comments

`#` or `//` up to the end of the line. To create a comment box in the graph:

```
Comment Rebind logic starts here
```

## What the parser ignores by itself

When pasting a chat answer, this goes away without getting in the way:
```` ``` ```` fences, `-` and `*` bullets, and step numbering in any common form
-- `1.`, `1)`, `[2]`, `3.1`.

---

## The way back

**Copy Selected** and **Copy Whole Graph** produce text in this same format,
ready to paste into a chat and edit.

The header says where the text came from and declares the variables:

```
# WBP_RemapRow -> EventGraph (partial selection)
# refPath: /Game/UI/WBP_RemapRow.WBP_RemapRow:EventGraph
# from the Designer (create them there, ticking Is Variable):
#   AbilityNameText : Text Object Reference
variable Input Name : Name
variable KeySlot : EPlayerMappableKeySlot
```

The `refPath` is that graph's exact path, to ask for it back without guessing.
It is two things that cannot be deduced: the graph's name (`EventGraph` in one
asset, `Gameplay Ability Graph` in another) and the content root -- a plugin
asset lives in `/PluginName/`, not in `/Game/`.

Pasting into an empty Blueprint recreates the variables together with the
nodes. The Designer widgets come out as comments, because it is not the text
that creates them -- and so do the components added in the Components panel,
under `# components (add them in the Components panel):`. A `variable` line for
either would create a plain variable of that type, which compiles and never
points at the widget or the component.

Only what the Blueprint itself declares -- the inherited ones would be hundreds
of Engine lines.

### Four things the reading writes and the parser discards

**Reconvergence anchor.** Two chains landing on the same node do not fit in a
tree. Instead of the second branch coming out empty -- just like a branch
nobody linked --, the target node gets a `# anchor N` at the end of its line,
and the point of return says where it goes:

```
Branch (Condition = $bOn)
  true:
    Print String (In String = "turned on")
    Refresh Screen  # anchor 1
  false:
    # -> back to anchor 1 (`Refresh Screen`)
```

The way back is still lost on pasting -- what changed is that you can see it
exists, and where.

**Side input.** An execution wire does not always land on the main input of
the node on the other side. The `Reset` of a Do Once, the `Stop` of a Timeline,
the `Close` of a Gate: those do not continue the chain, they send a command to
a node that lives elsewhere in the graph. It comes out with the same anchor,
saying through which pin it enters:

```
Branch (Condition = $Rotation Mode?)
  true:
    Do Once  # anchor 2
      completed:
        Set Actor Location (New Location = $Home Location)
        # -> enters `Do Once` through pin `Reset` (anchor 3)
  false:
    Do Once  # anchor 3
```

Before, this was followed as if it were a continuation, and the reading wrote a
chain that does not exist -- two Do Onces that reset each other came out
stacked, one under the other, as if one called the other. The link itself is
still lost on pasting; what changed is that it shows up, and on the right pin.

**Type conversion.** Linking an `Integer` into a `String` pin makes Unreal
insert a conversion node. It does not become a line (the plugin recreates it by
itself when redoing the link), but it is marked:

```
Append (A = "n = ", B = $Counter (Integer -> String))
```

Without this, a `Device Id` promoted to `int64` and a `Connection Id` that
already was `int64` write exactly the same line -- and one of the two makes
every controller collide on the same map key.

**Unnamed pin.** When the plugin cannot name the output a value comes from, it
writes `$x.<unknown pin>` instead of `$x`, which would look like the main
output and link to another pin on the way back.

### A pin that does not appear on the line

An omitted pin is at its **factory value** -- it never means "nothing linked".
`Set Is Enabled (Target = $X)` is `bInIsEnabled` at its default, which is
`false`. A value erased on purpose, which differs from the default, comes out
explicitly: `""`.

### The rest of what is not perfect

| Situation | What happens |
|---|---|
| Execution chain that reconverges | anchor on both ends + warning: the way back is lost on pasting |
| Wire entering through a side pin (`Reset`, `Stop`, `Close`) | anchor + the pin's name, and a warning: the link is lost on pasting |
| Pin fed by a node outside the selection | warning: the pin comes out without a value |
| Node the plugin cannot name back | the node's title comes out + a warning that it may not come back the same |
| Value with both kinds of quotes | warning: there is no escape, copy it by hand |
| Data node that feeds nobody | note, with each one's name |
| Event bound to a dispatcher/delegate | warning on reading; red comment on pasting |
| Function entry (`Function Entry`) | warning with the signature: create the function and paste inside it |

Reroutes (the little dots for arranging wires) vanish on the way back -- they
are layout, not logic. A Get of the Blueprint's own variable becomes `$Name`
directly, without a line of its own.

---

## When it cannot be done

None of this fails silently:

| Situation | What happens |
|---|---|
| Node name not found | **red** comment in the graph with the original line |
| Event that already exists in the graph | red comment; nothing is created or changed |
| Ambiguous name | red comment listing the candidates, none chosen |
| Nonexistent pin | error in the panel listing the pins the node really has |
| `$something` that does not exist | error in the panel |
| Incompatible types | node goes in, link does not; warning in the panel |
| Asset not decided | empty pin → the Blueprint does not compile |

If something went in wrong, `Ctrl+Z` undoes everything at once.

---

## Keyword reference

Every word the format reserves, in one place.

| Keyword | Where | Meaning |
|---|---|---|
| `name = ...` | any node line | names the node's main output for `$name` |
| `$name`, `$name.Pin` | argument, or a line of its own | reference to a named output or a Blueprint variable |
| `?` | argument value | declared hole: the pin stays empty on purpose |
| `variable Name : Type [= value]` | top level | declares a Blueprint variable |
| `Array of X`, `Set of X`, `Map of X to Y`, `X Class` | variable type | containers and class references |
| `event Name` | line | overridden event or Custom Event |
| `event Dispatcher of Variable` | line | event bound to another object's dispatcher |
| `Call`, `Bind`, `Unbind`, `Clear` | line prefix | dispatcher nodes |
| `Get X`, `Set X` | line prefix | variable nodes |
| `Cast to Class` | line | dynamic cast |
| `Make Struct`, `Break Struct` | line | struct nodes |
| `Switch on X` | line | enum, `Int`, `String` or `Name` switch |
| `EnhancedInputAction Path` | line | Input Action event |
| `Comment Text` | line | comment box |
| `true:`, `then:`, `false:`, `else:`, `loop body:`, `loop:`, `completed:` | label | execution output aliases |
| `state Name:`, `conduit Name:`, `alias Name:` | label under a state machine | state machine nodes |
| `From -> To:` (optionally `transition From -> To:`) | label under a state machine | transition |
| `Target` | argument name | the self pin |

The sheet (`read_object` / `write_object`) and the other tools add their own:

| Keyword | Tool | Meaning |
|---|---|---|
| `sheet Name (Class)` | read_object header | ignored by write_object |
| `variable Name : Type [editable] [= value]` | write_object | creates or updates a Blueprint variable |
| `delete variable Name` | write_object | removes a Blueprint variable |
| `Name = default` | write_object | resets the property to its factory value |
| `key Name : Type [(Detail)] [synced]` | blackboard sheet | a blackboard key |
| `tree`, `decorator`, `service` | Behavior Tree reading | read only |
| `axis X : Name = min .. max` | write_blendspace | a BlendSpace axis |
| `tag Name` | read_tags / write_tags | a Gameplay Tag |
