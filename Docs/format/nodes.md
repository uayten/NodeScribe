# NodeScribe format: Nodes with a form of their own

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

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
down (`done = event Done`); declaring no parameters, it takes the dispatcher's.
For a dispatcher's **event**, see `event X of Variable` in the core.

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
order. `Class = $Rock Class` takes them from the variable's class. Without
`Class`, the node goes in without those pins, with a warning.

## Nodes that wait: async actions, tasks, Play Montage

A node that waits has one execution output per thing it waits for, each an
indented label; the pin that continues right away is `then`. They only exist in
an event graph, never in a function. Three kinds, all by the name shown in the
graph:

**An async action** (`UBlueprintAsyncActionBase`):

```
Wait For Any Controller Changes
  On Connected:
    Print String (In String = "connected")
  On Disconnected:
    Print String (In String = "dropped")
```

**A gameplay task or an ability task.** An ability task only goes in a Gameplay
Ability's graph; anywhere else the line is a red comment. Its name counts whole:
`Play Montage` is never taken for `PlayMontageAndWait`.

```
event ActivateAbility
Wait Input Release (Test Already Released = true)
  On Release:
    End Ability
```

**A node of its own**, such as `Play Montage` or `AI MoveTo`:

```
Play Montage (In Skeletal Mesh Component = $Mesh, Montage to Play = /Game/Anim/AM_Swing.AM_Swing)
  On Completed:
    Print String (In String = "done")
```

A function shown under that same name wins, and the reading warns about the
node. A task with a node of its own the plugin does not know is a red comment.
