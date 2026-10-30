# NodeScribe format

One line = one node. Nothing else.

Everything that is **not** written here is the plugin's job: node positions,
execution links, implicit casts between types, internal function names. Why
each rule is the way it is: [design.md](design.md) -- not needed to write text.

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
- [Comments](#comments)
- [What the parser ignores by itself](#what-the-parser-ignores-by-itself)
- [The way back](#the-way-back)
- [Inserting into a chain that exists](#inserting-into-a-chain-that-exists)
- [When it cannot be done](#when-it-cannot-be-done)
- [Keyword reference](#keyword-reference)

## Topics

This file is the core: read it whole before writing or interpreting any
text. A topic is read only when the task touches it -- `get_format_docs(topic)`
returns one, `get_format_docs()` returns this core.

| topic | file | read it when |
|---|---|---|
| `animgraph` | [format/animgraph.md](format/animgraph.md) | the graph is an Animation Blueprint's AnimGraph, a state machine or a transition rule |
| `behavior-tree` | [format/behavior-tree.md](format/behavior-tree.md) | reading or writing a Behavior Tree with `read_object` / `write_object` |
| `designer` | [format/designer.md](format/designer.md) | the `designer` section of a Widget Blueprint |
| `input-mapping` | [format/input-mapping.md](format/input-mapping.md) | the `mappings` section of an Input Mapping Context |
| `sheet` | [format/sheet.md](format/sheet.md) | adding or removing a component, or an array of instanced objects (a Gameplay Effect's `Components`) in `read_object` / `write_object` |
| `reading` | [format/reading.md](format/reading.md) | a reading has `# anchor`, `(A -> B)` conversions or warnings, or before `replace` |

## The basics

```
Get Player Controller
Print String (In String = "hello")
```

Two lines → two nodes, already linked in the order they appear.

## Keeping a result

`name =` names a node's output; `$name` reuses it:

```
pc = Get Player Controller
Enable Input (Target = $pc)
```

`$name` also reads a Blueprint variable directly, without declaring anything
-- the Get node is created by itself:

```
Print String (In String = $PlayerName)
```

### When the node has more than one output

A bare `$name` takes the main output. For another one -- an event's
parameter, a struct Break's field, a function's *out param* -- write
`$name.Pin`:

```
key = event OnKeySelected
Map Player Key (New Key = $key.Selected Key Key)
```

A pin that does not exist is answered with the outputs the node has. `$name.Pin`
also reaches **part of a struct**: when the node only has the whole `Selected
Key` pin, `$key.Selected Key Key` splits it (*Split Struct Pin*).

## Declaring variables

```
variable Total Jumps : Integer = 3
variable Jump Duration : Float = 1.0
variable Jump Timer : Timer Handle
variable Inputs : Array of Name
variable Mirrors : Map of Int64 to BP_Mirror
variable Already Seen : Set of Name
```

The variable is created before the nodes that use it; one that already exists
is left alone, so pasting twice does no harm.

The type is the name shown in the interface: `Float`, `Integer`, `Boolean`,
`Name`, `Text`, `String`, any struct (`Timer Handle`, `Vector`), any enum
(`EPlayerMappableKeySlot`) and any class (`BP_Golem`). `BP_Rock Class` is the
class itself, not an instance. `Array of X`, `Set of X`, `Map of X to Y`; a
map's key and value cannot be collections -- that line is refused.

**Designer widgets are not declared**: a `variable` line with a widget's name is
refused. The widget comes from `write_object`'s
[Designer section](format/designer.md).

## Arguments

Between parentheses, separated by commas, by pin name or by position:

```
Print String (In String = "hi", Duration = 5.0)
Print String ("hi", 5.0)
```

Names are compared tolerantly -- `In String`, `instring` and `In_String` are
the same, and accents are ignored (`$Duracao` finds `Duração`) for every name
in the format. `Target` is the self pin.

## When two nodes share a name

When a name exists in two classes, the plugin does not choose: write
`Class.Function`.

```
EnhancedInputUserSettings.ApplySettings (Target = $settings)
```

Parentheses do **not** work for this: they are the argument list.

## Branches (Branch, loops)

Indent, under a label ending in `:`.

```
Branch (Condition = $bIsAlive)
  true:
    Print String (In String = "alive")
  false:
    Print String (In String = "dead")
```

Labels: `true`/`then`, `false`/`else`, `loop body`/`loop`, `completed`. Any
other name is compared with the output pin's name. A node with several
execution outputs and no label stops the chain there.

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

A name the parent class or an interface the Blueprint implements has becomes
that event; any other becomes a Custom Event with that name, and the plugin
says so. A Custom Event's parameters go in parentheses, as `Name : Type` --
the types of [Declaring variables](#declaring-variables):

```
hit = event TakeHit (Damage : Float, Source : Actor)
Print String (In String = $hit.Damage)
```

An event of the class, of an interface or of a dispatcher takes its parameters
from where it is declared: they are not written, and a list that differs gets
a yellow warning. A name that is an Engine
event but not this class's -- `BeginPlay` in a Widget Blueprint, which has
`Construct` -- gets a yellow warning listing the events the class offers.

An event already in the graph with **nothing linked** (the greyed-out
BeginPlay, Tick and ActorBeginOverlap of a new Blueprint) is reused. One that
already has a wire is a duplicate -- see
[When it cannot be done](#when-it-cannot-be-done).

The event of **another object's dispatcher** -- right-clicking a child widget
and choosing its dispatcher:

```
key = event OnKeySelected of SelectInputKey
Print String (In String = $key.Selected Key Key)
```

`of` separates the dispatcher from the variable that exposes it. A variable or
dispatcher that does not exist is answered with the ones that do.

## Input Action events

```
EnhancedInputAction /Game/MyGame/Player/Inputs/IA_Attack.IA_Attack
  Started:
    Print String (In String = "attacked")
```

The labels are the triggers: `Started`, `Triggered`, `Completed`, `Canceled`,
`Ongoing`. The short name (`IA_Attack`) works once the asset is loaded; the
full path always does.

## Dispatchers

```
Call OnHealthChanged (New Health = $health)
Bind OnHealthChanged
Unbind OnHealthChanged
Clear OnHealthChanged
```

Without `Target`, the dispatcher is this Blueprint's; with `Target = $obj`, it
is that object's. The event bound with `Event = $done` may be declared further
down (`done = event Done`). For a dispatcher's **event**, see
`event X of Variable`.

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

Only a variable the Blueprint has is treated as one: `Get Player Controller`
stays the function.

### A variable of another object

Pass `Target`, and the variable is found in its class, by the name on screen
too (`Show Mouse Cursor` finds `bShowMouseCursor`):

```
pc = Get Player Controller
Set Show Mouse Cursor (Target = $pc, Show Mouse Cursor = true)
```

### A variable that does not exist yet

A `$Something` used as an argument that does not exist still goes in: a Get
node with the type of the pin it feeds, a yellow warning, and the Blueprint
does not compile until the variable is created (right-clicking the node offers
it). A loose `Get X` line with `X` nonexistent is an error.

## Structs

```
args = Make MapPlayerKeyArgs (Mapping Name = $Name, Slot = First)
Break Vector ($position)
```

In a `Make`, each pin has the field's name. A `Break` has a single input pin,
so the positional form always works. The struct by its internal name
(`MapPlayerKeyArgs`) or display name (`Map Player Key Args`); it is only a
struct node if the struct exists -- `Make Literal Int` stays the function.

`Vector`, `Rotator`, `Transform` and `Color` use their own make and break
functions instead of the generic node; there the input pin is the function's
parameter (`In Vec`, not `Vector`), and the positional form works on both.

## Subsystems

```
settings = Get EnhancedInputLocalPlayerSubsystem (PlayerController = $pc)
```

LocalPlayer subsystems ask for a PlayerController; Engine ones ask for nothing.

## Creating widgets and spawning actors

```
row = Create Widget (Class = WBP_RemapRow, Owning Player = $pc, Input Name = $name)
Spawn Actor from Class (Class = BP_Boss, Spawn Transform = $t)
```

Also `Construct Object from Class`. The *Expose on Spawn* pins only exist once
the class is set; the plugin applies `Class` first, so the arguments go in any
order. Without `Class`, the node goes in without those pins, with a warning.

## Standard macros

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

An **async action** (`UBlueprintAsyncActionBase`: one execution pin per event)
goes in by the name shown in the graph, each output an indented label; the pin
that continues right away is `then`:

```
Wait For Any Controller Changes
  On Connected:
    Print String (In String = "connected")
  On Disconnected:
    Print String (In String = "dropped")
```

## Holes: what the plugin does **not** decide for you

`?` on a pin whose value is your choice:

```
Spawn Sound 2D (Sound = ?)
```

The node goes in with the pin empty. Some empty pins block compiling, others
fail only at runtime: **check the pins with `?` before pressing Play.**

An asset goes by full path:

```
Spawn Sound 2D (Sound = /Game/MyGame/Audio/SFX_Hit.SFX_Hit)
```

A bare name that is not a path is **not** accepted: a warning, and the pin
stays empty.

## Comments

`#` or `//` up to the end of the line. A comment box in the graph:

```
Comment Rebind logic starts here
```

## What the parser ignores by itself

```` ``` ```` fences, `-` and `*` bullets, and step numbering in any common form
-- `1.`, `1)`, `[2]`, `3.1`.

---

## The way back

**Copy Selected** and **Copy Whole Graph** produce text in this same format.
The header says where it came from and declares the variables:

```
# WBP_RemapRow -> EventGraph (partial selection)
# refPath: /Game/UI/WBP_RemapRow.WBP_RemapRow:EventGraph
# from the Designer (create them with write_object's `designer` section, as `Name : Class variable`):
#   AbilityNameText : Text Object Reference
variable Input Name : Name
variable KeySlot : EPlayerMappableKeySlot
```

`refPath` is the graph's exact path: its name (`EventGraph` in one asset,
`Gameplay Ability Graph` in another) and its root (`/PluginName/` for a
plugin's asset) cannot be guessed. Pasted into an empty Blueprint, the text
recreates the variables with the nodes; only what the Blueprint itself
declares comes out. Designer widgets and components come out as comments -- not
`variable` lines, which would make plain variables: widgets are created in
`write_object`'s [Designer section](format/designer.md), components with its
`component` line ([sheet](format/sheet.md)).

An omitted pin is at its **factory value** -- never "nothing linked". A value
erased on purpose comes out explicitly: `""`. Anchors, side inputs,
conversions, and what does not come back the same: the
[reading](format/reading.md) topic.

## Inserting into a chain that exists

`after`, a line copied from `read_graph` and `:` open a block that goes into
that node's chain, right after it. What used to follow the node now follows the
block's last node:

```
after Set Text (Target = $BatteryVoltsAnswer, In Text = $volts):
  Set Text (Target = $BatteryCurrentAnswer, In Text = $sensors.Battery Current)
```

The `$names` of that reading work inside the block (`$sensors` above is a node
already in the graph). The line may leave arguments out, as long as only one node
still matches. On a node with several outputs, a label says which one, and the
block goes at the head of that branch:

```
after Branch (Condition = $bOn):
  false:
    Print String (In String = "first")
```

`after` is not indented. Without `replace`: it inserts into the graph that is
there. If an `after` matches no node, or more than one, the whole write is
refused and nothing changes. If the block ends in a node with several outputs,
whatever came next is left unlinked, with an error.

---

## When it cannot be done

Nothing fails silently:

| Situation | What happens |
|---|---|
| Node name not found | **red** comment in the graph with the original line |
| Event that already exists in the graph, with a wire | red comment; nothing is created or changed |
| `after` that matches no node, or several | error; the whole write is refused |
| Ambiguous name | red comment listing the candidates, none chosen |
| Nonexistent pin | error in the panel listing the pins the node really has |
| `$something` that does not exist | error in the panel |
| Incompatible types | node goes in, link does not; warning in the panel |
| Asset not decided | empty pin → the Blueprint does not compile |

`Ctrl+Z` undoes the whole paste at once.

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
| `event Name` | line | overridden event, interface event or Custom Event |
| `event Name (P : Type, ...)` | line | Custom Event with parameters |
| `event Dispatcher of Variable` | line | event bound to another object's dispatcher |
| `Call`, `Bind`, `Unbind`, `Clear` | line prefix | dispatcher nodes |
| `Get X`, `Set X` | line prefix | variable nodes |
| `Cast to Class` | line | dynamic cast |
| `Make Struct`, `Break Struct` | line | struct nodes |
| `Switch on X` | line | enum, `Int`, `String` or `Name` switch |
| `EnhancedInputAction Path` | line | Input Action event |
| `Comment Text` | line | comment box |
| `after Line:` | top level | inserts the block under it after the node that reads as `Line` |
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
| `component Name : Class [in Parent]` | write_object | adds a component to an actor Blueprint |
| `delete component Name` | write_object | removes a component the Blueprint added |
| `# components: A, B in A` | read_object header | every component and its parent; ignored by write_object |
| `Name = default` | write_object | resets the property to its factory value |
| `key Name : Type [(Detail)] [synced]` | blackboard sheet | a blackboard key |
| `tree Name (blackboard X)` | Behavior Tree header | ignored by write_object |
| `decorator X`, `service X` | Behavior Tree | a decorator guards the node on the next line at its indentation; a service belongs to the node above it |
| `Name = Class` | Behavior Tree | a node renamed in the editor: `CombatState = Selector` |
| `designer` | Widget Blueprint | opens the Designer section; above it is the sheet |
| `Name : Class [variable] [like Other]` | Designer | a widget; `variable` ticks Is Variable, `like` copies another widget's look |
| `slot Property = value` | Designer | a property of the slot the parent panel gives the widget |
| `Is Variable = false` | Designer | unticks Is Variable on an existing widget |
| `rename Old -> New` | Designer | renames a widget |
| `delete widget Name` | Designer | removes a widget and everything under it |
| `named slot Name` | Designer | the place a named slot's one widget goes |
| `mappings` | Input Mapping Context | opens the mappings section; above it is the sheet |
| `Key -> Action` | mappings | a key mapping |
| `trigger X`, `modifier X` | mappings | a mapping's trigger or modifier, under it |
| `delete mapping Key -> Action` | mappings | removes a mapping |
| `axis X : Name = min .. max` | write_blendspace | a BlendSpace axis |
| `tag Name` | read_tags / write_tags | a Gameplay Tag |
