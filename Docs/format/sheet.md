# NodeScribe format: the sheet's structure

A topic of [the format](../FORMAT.md). Every rule of the core still
applies; this file only adds what is particular to it.

`read_object` gives one line per changed property, and `write_object` takes
the same lines back as a list of changes. Besides plain values, the sheet has
two kinds of structure: an actor Blueprint's components, and arrays of
instanced objects.

## Components

The header of an actor Blueprint names every component, changed or not, with
its parent:

```
sheet BP_Golem (Character)
# components: CollisionCylinder, CharMoveComp, CharacterMesh0 in CollisionCylinder, AbilitySystem
CharMoveComp : CharacterMovementComponent
  Max Walk Speed = 300.0 # default 600.0
```

Only a changed component opens a block (`Name : Class`, its lines indented
under it); the header line is how to know the others exist.

`component Name : Class [in Parent]` adds one, as the Components panel's
**Add** does, and the lines indented under it configure it:

```
component Hitbox : Box Collision in CharacterMesh0
  Box Extent = (X=40.0,Y=40.0,Z=60.0)
  Collision Profile Name = OverlapAllDynamic
```

- The class is the name the panel shows (`Box Collision`), the class name
  (`BoxComponent`) or a path. Only classes the panel offers are accepted: a
  CharacterMovement added to a Character would compile and fight the one it
  already has.
- Without `in`, a scene component goes under the root and any other one onto
  the actor. `in Parent` takes a scene component under a scene component.
- A name that already exists with that class is an anchor: pasting the same
  text twice creates nothing twice. With another class it is refused --
  changing a component's class is not done by text.
- `delete component Name` removes one this Blueprint added. Inherited and
  native components are refused.

## Arrays of instanced objects

An array whose elements are objects owned by the asset -- a Gameplay Effect's
`Components`, for example -- comes out as a block: one line per element, the
class as the Details panel names it, and the element's changed properties
under it.

```
Duration Policy = HasDuration
Duration Magnitude:
  Scalable Float Magnitude = (Value=10.000000,Curve=(),RegistryType="None")
Components:
  Grant Tags to Target Actor
    Add Tags:
      Added = (GameplayTags=((TagName="Cooldown.Golem.Salto")))
```

**The block is the array's whole value**, like any `Name = value` line: the
elements come out in the text's order; an existing element of the same class is
kept, and the lines under it change it; an element the text does not list
leaves, and the return names it. A sheet that does not mention the property
leaves the array alone. `None` is an empty element.

The class is the panel's name, the class name, the class name without the
base's (`TargetTags` for `TargetTagsGameplayEffectComponent`) or a path. A
name that matches two classes is refused, listing both. All or nothing on the
classes: one that does not resolve and the array stays as it was.

A whole cooldown, from an empty Gameplay Effect:

```
Duration Policy = HasDuration
Duration Magnitude:
  Scalable Float Magnitude = (Value=10.0)
Components:
  Grant Tags to Target Actor
    Add Tags:
      Added = (GameplayTags=((TagName="Cooldown.Golem.Salto")))
```
