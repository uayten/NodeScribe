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
- [Comments](#comments)
- [What the parser ignores by itself](#what-the-parser-ignores-by-itself)
- [The way back](#the-way-back)
- [When it cannot be done](#when-it-cannot-be-done)
- [Keyword reference](#keyword-reference)

Topics, in files of their own: see [Topics](#topics).

## Topics

This file is the core: read it whole before writing or interpreting any
text. A topic is a file of its own, read only when the task touches it --
`get_format_docs(topic)` returns one, `get_format_docs()` returns this core.

| topic | file | read it when |
|---|---|---|
| `animgraph` | [format/animgraph.md](format/animgraph.md) | the graph is an Animation Blueprint's AnimGraph, a state machine or a transition rule |
| `behavior-tree` | [format/behavior-tree.md](format/behavior-tree.md) | reading or writing a Behavior Tree with `read_object` / `write_object` |
| `designer` | [format/designer.md](format/designer.md) | the `designer` section of a Widget Blueprint |
| `reading` | [format/reading.md](format/reading.md) | a reading has `# anchor`, `(A -> B)` conversions or warnings, or before `replace` |

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
points at the widget. The plugin refuses and says so. The widget itself is
created through `write_object`, in its [Designer section](format/designer.md).

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

The event bound with `Event = $done` may be declared further down
(`done = event Done`): a Custom Event's name is known before the first line
runs.

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
# from the Designer (create them with write_object's `designer` section, as `Name : Class variable`):
#   AbilityNameText : Text Object Reference
variable Input Name : Name
variable KeySlot : EPlayerMappableKeySlot
```

The `refPath` is that graph's exact path, to ask for it back without guessing.
It is two things that cannot be deduced: the graph's name (`EventGraph` in one
asset, `Gameplay Ability Graph` in another) and the content root -- a plugin
asset lives in `/PluginName/`, not in `/Game/`.

Pasting into an empty Blueprint recreates the variables together with the
nodes. The Designer widgets come out as comments, because it is not the graph
text that creates them -- `write_object`'s [Designer section](format/designer.md)
does -- and so do the components added in the Components panel,
under `# components (add them in the Components panel):`. A `variable` line for
either would create a plain variable of that type, which compiles and never
points at the widget or the component.

Only what the Blueprint itself declares -- the inherited ones would be hundreds
of Engine lines.

### A pin that does not appear on the line

An omitted pin is at its **factory value** -- it never means "nothing linked".
`Set Is Enabled (Target = $X)` is `bInIsEnabled` at its default, which is
`false`. A value erased on purpose, which differs from the default, comes out
explicitly: `""`.

What else the reading marks (reconvergence anchors, side inputs, type
conversions, unnamed pins) and the table of what does not come back the same
are in the [reading](format/reading.md) topic.

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
| `tree Name (blackboard X)` | Behavior Tree header | ignored by write_object |
| `decorator X`, `service X` | Behavior Tree | a decorator guards the node on the next line at its indentation; a service belongs to the node above it |
| `Name = Class` | Behavior Tree | a node renamed in the editor: `CombatState = Selector` |
| `designer` | Widget Blueprint | opens the Designer section; above it is the sheet |
| `Name : Class [variable] [like Other]` | Designer | a widget; `variable` ticks Is Variable, `like` copies another widget's look |
| `slot Property = value` | Designer | a property of the slot the parent panel gives the widget |
| `Is Variable = false` | Designer | unticks Is Variable on an existing widget |
| `rename Old -> New` | Designer | renames a widget |
| `delete widget Name` | Designer | removes a widget and everything under it |
| `axis X : Name = min .. max` | write_blendspace | a BlendSpace axis |
| `tag Name` | read_tags / write_tags | a Gameplay Tag |
