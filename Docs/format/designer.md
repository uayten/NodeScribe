# NodeScribe format: Widget Designer

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

`read_object` on a Widget Blueprint gives its sheet and then, after a
`designer` line, the Designer's hierarchy. `write_object` takes the same text
back:

```
designer
Root : VerticalBox
  IsStillRow : HorizontalBox
    IsStillLabel : TextBlock
      Text = "Is still?"
      Font:
        Size = 18
    IsStillAnswer : TextBlock variable
      Text = "-"
      slot Padding = (Left=8.0,Top=0.0,Right=0.0,Bottom=0.0)
      # Text is bound to `GetIsStillText`
  GravityVector : WBP_VectorInspector variable
```

A widget is written like a component in the sheet, `Name : Class`, and
`variable` at the end means *Is Variable* is ticked. Under it, one level in,
come its properties -- only the ones that differ from the class's factory
value, as in the sheet -- and its children. `slot X = ...` is a property of the
slot the parent panel gives it (padding, alignment, size); `slot Padding:`
opens a struct block the same way `Font:` does. A text bound to a function
comes out as a comment, since the property itself only holds the Designer's
placeholder.

The class is the name the Palette shows: `TextBlock`, `HorizontalBox`, or a
Widget Blueprint's name, found even if nobody opened it this session. Two
Widget Blueprints with the same name are refused, listing both paths; write
the path instead (`/Game/UI/WBP_Row.WBP_Row`).

**Writing is a list of changes, like the sheet.** A widget the text does not
mention stays as it is, and pasting a whole reading back changes nothing. A
name that does not exist yet creates a widget:

```
designer
Root : VerticalBox
  IsStillRow : HorizontalBox
  BatteryRow : HorizontalBox like IsStillRow
    BatteryLabel : TextBlock like IsStillLabel
      Text = "Battery"
    BatteryAnswer : TextBlock variable like IsStillAnswer
```

- **Where it goes is where it is written.** A new widget goes right after the
  sibling written above it, or right before the existing sibling written below
  it; with neither, at the end of its parent. `Root` and `IsStillRow` above are
  anchors -- existing widgets with nothing under them -- and say only where.
- **`like Other`** copies another widget's look onto this one: every property
  that the Details panel edits, and the slot's settings when both sit in the
  same kind of panel. Both must be of the same class. Lines under the widget
  are applied afterwards, so `Text = "Battery"` wins over the copied text. It
  works on an existing widget too, to copy a style onto it.
- **Is Variable.** On a new widget, the word `variable` is the whole answer:
  without it, the widget is not a variable. On an existing one the missing word
  changes nothing -- an anchor must not untick it and break the graph's
  references --, so unticking is said out loud: `Is Variable = false`.
- **`rename Old -> New`** renames, and the graph's references follow, as on
  screen. **`delete widget Name`** removes a widget and everything under it.
  They are the only ways to do either: a name written differently is a new
  widget, never a rename.

A widget line at the top of the text is an anchor and may name any widget,
however deep it really is. Indented, it states its parent, and a different
parent is a move -- refused, since moving is not written yet. So are
reordering existing siblings and changing a widget's class.

The structure is all or nothing: a class that does not resolve, a name already
taken, a second root, a panel that holds a single child given two, and nothing
in the Designer changes. Property lines then go through the sheet's writer one
by one, as on any object: a property that does not exist becomes an error with
the similar names, and the other lines are applied.

Filtering `read_object` by a word gives the widgets whose name or class
contains it, with their properties whole, under their ancestors written bare --
which still pastes back as anchors. Not read yet, and named in a warning:
content placed in a child Widget Blueprint's named slot.
