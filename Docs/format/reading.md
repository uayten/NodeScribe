# NodeScribe format: The way back, in detail

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

## What the reading adds

**Links.** Chains that land on the same node, a chain coming back to one of its
own, and a wire entering through a side pin (the `Reset` of a Do Once, the `Stop`
of a Timeline, the `Close` of a Gate) come out as `# anchor N` and `-> anchor N`,
as in the core's *Chains that meet*, with the node's title after them as a
comment. They come back linked on pasting:

```
Branch (Condition = $Rotation Mode?)
  true:
    Do Once  # anchor 2
      completed:
        Set Actor Location (New Location = $Home Location)
        -> anchor 3 through Reset  # `Do Once`
  false:
    Do Once  # anchor 3
```

**Type conversion.** Linking an `Integer` into a `String` pin makes Unreal
insert a conversion node. It does not become a line (the plugin recreates it by
itself when redoing the link), but it is marked:

```
Append (A = "n = ", B = $Counter (Integer -> String))
```

Without this, a `Device Id` promoted to `int64` and a `Connection Id` that
already was `int64` write exactly the same line -- and one of the two makes
every controller collide on the same map key. A promotable math operator comes
back as a plain call, so its mark names the function's parameter: `(Integer ->
Float)`, even with the pin on screen showing Integer.

**Unnamed pin.** When the plugin cannot name the output a value comes from, it
writes `$x.<unknown pin>` instead of `$x`, which would look like the main
output and link to another pin on the way back.

## The rest of what is not perfect

| Situation | What happens |
|---|---|
| Wire of incompatible types kept by the asset, which the editor would refuse today | warning: on pasting the link does not go in |
| Pin fed by a node outside the selection | warning: the pin comes out without a value |
| Node the plugin cannot name back | the node's title comes out + a warning that it may not come back the same |
| Value with both kinds of quotes | warning: there is no escape, copy it by hand |
| Data node that feeds nobody | note, with each one's name |
| Event bound to a dispatcher/delegate | warning on reading; red comment on pasting |
| Interface function called on an interface reference | warning: it comes back as the interface's message |
| Function entry (`Function Entry`) | note with the signature and local variables, when there are any: pasted elsewhere, the function must have them; the line finds the entry |

Reroutes (the little dots for arranging wires) vanish on the way back -- they
are layout, not logic. A Get of the Blueprint's own variable becomes `$Name`
directly, without a line of its own.
