# NodeScribe format: The way back, in detail

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

## Four things the reading writes and the parser discards

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

## The rest of what is not perfect

| Situation | What happens |
|---|---|
| Execution chain that reconverges | anchor on both ends + warning: the way back is lost on pasting |
| Wire entering through a side pin (`Reset`, `Stop`, `Close`) | anchor + the pin's name, and a warning: the link is lost on pasting |
| Pin fed by a node outside the selection | warning: the pin comes out without a value |
| Node the plugin cannot name back | the node's title comes out + a warning that it may not come back the same |
| Value with both kinds of quotes | warning: there is no escape, copy it by hand |
| Data node that feeds nobody | note, with each one's name |
| Event bound to a dispatcher/delegate | warning on reading; red comment on pasting |
| Interface function called on an interface reference | warning: it comes back as the interface's message |
| Function entry (`Function Entry`) | warning with the signature: create the function and paste inside it |

Reroutes (the little dots for arranging wires) vanish on the way back -- they
are layout, not logic. A Get of the Blueprint's own variable becomes `$Name`
directly, without a line of its own.
