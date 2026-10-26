# NodeScribe format: Input Mapping Context

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

`read_object` on an Input Mapping Context gives its sheet and then, after a
`mappings` line, one line per mapping. `write_object` takes the same text
back:

```
mappings
SpaceBar -> IA_Jump
  trigger Pressed
Gamepad_FaceButton_Bottom -> IA_Jump
Gamepad_Left2D -> IA_Move
  modifier Dead Zone
    Lower Threshold = 0.25
  modifier Swizzle Input Axis Values
```

- **`Key -> Action`.** The key by its name (`SpaceBar`, `Gamepad_Left2D`) or
  the name on screen (`Space Bar`). The action by its asset name when no other
  Input Action has it, the path otherwise -- the reading writes it the same
  way.
- **`trigger X` and `modifier X`**, one level in, in order, with the class as
  the Details panel names it (`Hold`, `Dead Zone`, `Negate`). The changed
  properties go one level further in, as sheet lines.

**Writing is a list of changes, like the sheet.** A `Key -> Action` line
that exists is an anchor; one that does not is added at the end. With
`trigger` / `modifier` lines under it, those are the mapping's whole set --
an element the text does not list leaves, and the return names it --; with
nothing under it, its triggers and modifiers stay as they are. Two mappings
with the same key and action line up with the lines in order.
`delete mapping Key -> Action` removes one.

A mapping's player-mappable settings are not written; when one differs from
the default, the reading names it in a warning instead of leaving it out.
