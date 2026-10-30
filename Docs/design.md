# NodeScribe format: why the rules are what they are

The rules live in [FORMAT.md](FORMAT.md) and its topics; this file only says
why, section by section, for whoever is going to change one. Writing text
never needs it.

The thread through all of them: when the plugin cannot decide safely, it does
not decide. A guessed plausible node compiles, runs and is wrong; a red
comment, an empty pin or a refusal costs one look.

## Declaring variables

- **A collection inside a map is refused** instead of becoming something
  else: Unreal has no `TMap<int, TArray<X>>`, and the nearest thing that
  compiles is not what was asked for.
- **A Designer widget is not a `variable` line.** A `ScrollBox` on screen is a
  variable only because the Designer ticks *Is Variable*; a declared variable
  with the same name compiles and never points at the widget.

## Arguments

- **Accents are ignored** because names come from the project, and the project
  may not be written in English (`$Duracao` and `Duração`).

## When two nodes share a name

- **`Class.Function` and not parentheses**: parentheses already are the
  argument list. And the plugin does not pick between `GameUserSettings` and
  `EnhancedInputUserSettings` -- both compile, one is wrong.
- **An interface implementation is not a candidate**: it is the same call as
  the interface's, and counting it made every call to a function three classes
  implement ambiguous. The interface's is created as a message, the editor
  menu's form, because its Target takes any object.

## Arguments of split structs

- **A split field is found by prefix only** (`Spawn Transform Location`,
  `B Z`): the reader always writes the parent's name. A bare `Z` would have
  to choose between struct pins, and on a node with an Add pin button it was
  taken for a pin to add. A split that finds nothing is undone.

## Branches

- **No label, the chain stops.** Choosing one of several execution outputs for
  the writer would be guessing.

## Events

- **The yellow warning for an Engine event the class lacks**: a Custom Event
  called `BeginPlay` in a Widget Blueprint compiles, looks right in the graph,
  and never fires -- a mistake that only shows up in playtest.
- **An unlinked event is reused** because a new Blueprint is born with greyed
  out BeginPlay, Tick and ActorBeginOverlap, the reading leaves them out, and
  refusing `event BeginPlay` there gave no way to see why (D-019).
- **A dispatcher event that does not exist lists the real ones** instead of
  creating a loose event, which would compile and never be bound.
- **A Custom Event's parameters are `(Name : Type)` on its own line**: an
  event has no inputs, so its parentheses were free, and `:` is how
  `variable` already pairs a name with a type. Without them the event came
  back with no outputs and every `$x.Pin` on it broke (D-026).
- **A bound event that declared nothing takes the dispatcher's signature**, as
  the editor's "create matching event" does: no parameters is not the
  dispatcher's signature, the schema refused the wire, and the Blueprint did
  not compile. One that declared parameters keeps them, and a mismatch is said.
- **Interface events are looked up before Custom Events**: missed, `event X`
  of an interface became a Custom Event called X, which compiles, looks the
  same and is never called through the interface. Two interfaces with the same
  event are not chosen between.
- **A declared signature on an event of the class, an interface or a
  dispatcher only warns**: the signature is not the line's to choose, and the
  event is right either way. On an existing Custom Event a different one is an
  error: changing it would break every call to it elsewhere.

## Dispatchers

- **An event bound before its line** works because a Custom Event's name is
  known before the first line runs; forcing the order would make the text read
  backwards.

## Variables

- **Only an existing variable is a variable**, so `Get Player Controller` stays
  the function and not a variable called "Player Controller".
- **A `$Something` that does not exist keeps its link.** It is what pasting
  nodes between two Blueprints does: the pending item stays in the graph, one
  click from solved, instead of a lost chain. A loose `Get X` has no consuming
  pin to take the type from, so there it is an error.

## Structs

- **Vector, Rotator, Transform and Color use their own make/break functions**
  because the generic node compiles there with an Engine warning -- *"the
  structure cannot be broken using generic 'break' node"* -- that whoever writes
  the text could not avoid: the format has no way to choose between the two.
- **A struct node only if the struct exists**, so `Make Literal Int` stays the
  function it always was.

## Creating widgets and spawning actors

- **`Class` goes in first by necessity**: the *Expose on Spawn* pins only exist
  after the class is chosen; applied in the text's order, the other arguments
  would arrive before their pins and fail with an error that explains nothing.

## Holes

- **`?` instead of a guessed asset.** An empty pin is a visible pending item;
  a guessed asset goes unnoticed and becomes a playtest bug. Not every empty
  pin blocks compiling, which is why the core says to check them.
- **A bare asset name is refused** for the same reason: two folders may have
  `SKM_Golem`, and choosing the wrong one is the mistake this plugin refuses.

## The way back

- **`refPath`** exists because the graph's name and the content root cannot be
  deduced: people kept trying names until one worked.
- **Widgets and components as comments**: a `variable` line for either creates
  a plain variable that compiles and never points at the widget or component.
- **Only what the Blueprint declares**: the inherited variables would be
  hundreds of Engine lines.
- **An omitted pin is its factory value**, never "nothing linked": that is what
  keeps a reading short, and a value erased on purpose has to come out as `""`
  to survive.

## Inserting into a chain that exists

- **The node is named by its reading line**, not by a GUID or a path through
  the chain: the line is what the assistant has in front of it, and a reading
  is deterministic, so the plugin re-reads the graph and compares. The reading's
  `$names` count in the block for the same reason.
- **Several matches are refused, never chosen between.** Leaving arguments out
  only widens the match, so a short `after` costs a refusal, never a wrong node.
- **All of them or none.** An `after` that does not resolve stops the write
  before its first node: half an insertion is a graph nobody wrote.
- **The colon is required** so that no line that worked before changes meaning:
  a node line never ends in `:`, so a function called "After X" stays a function.
- **Insertion and not `replace` for graphs that reconverge.** The format only
  describes trees, so a graph where two chains land on the same node can never
  be rewritten whole. An insertion leaves those wires alone.
