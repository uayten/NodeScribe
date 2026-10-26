# NodeScribe format: AnimGraph

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

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

## What is not a pin

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

## Indentation, here, opens an input

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

## Output Pose

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

## State machine

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

## State and transition options

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

## Conduit

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

## Alias

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

## The state machine getters

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

## The way back

**Copy Whole Graph** on an AnimGraph writes in this same format. Two things do
not come back the same, and both come out with a warning:

| Situation | What happens |
|---|---|
| The same pose feeding two places | anchor on both ends: the format is a tree, and the second link is lost on pasting |
| An anim node that has an asset but does not come back through it (a Sequence *Evaluator*) | the node's title comes out + a warning: the asset is not in the text |

The Output Pose and a transition's result do not become lines in the reading --
they already exist in the target graph, and the link into them is the plugin's.
