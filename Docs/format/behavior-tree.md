# NodeScribe format: Behavior Tree

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

`read_object` on a Behavior Tree gives the tree, and `write_object` takes the
same text back:

```
tree BT_Golem  (blackboard BB_Golem)
Selector
  CombatState = Selector
    decorator Is Wielding Axe (Inverse Condition = true)
    Sequence
      Move To (Blackboard Key = Player)
      Wait (Wait Time = 1.00)
  decorator Has Patrol Route
  Patrol = Sequence
    service Update Target
    BTT_MoveAlongPatrolRoute
```

Indentation is the hierarchy, and the order of the lines is the order the
children run in. `decorator X` guards the node on the **next** line at the
same indentation; `service X` belongs to the node **above** it, one level in.
A node renamed in the editor is written `Name = Class`, because the name alone
does not say what the node is.

**Writing takes the whole tree, not a list of changes** -- unlike the sheet.
A tree's shape only exists as a whole. A node the text does not mention leaves
the graph, and the return names it (Ctrl+Z brings it back). A branch the
reading shows commented, as not connected to the root, is hooked up by writing
it uncommented where it belongs.

It is all or nothing: a name that does not resolve, an ambiguous one, a key
the blackboard lacks or a parameter that does not parse, and nothing changes.
A node whose line already reads exactly the same is kept as it is, with
whatever the text cannot say. When the shape and order do not change, the
layout someone arranged by hand stays; otherwise the tree is laid out again,
left to right in the text's order -- which is also what decides the execution
order.

Not written yet, and refused out loud: Simple Parallel, composite decorators
and decorators injected by a subtree.
