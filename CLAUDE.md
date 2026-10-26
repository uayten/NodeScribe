# NodeScribe

Unreal editor plugin that transcribes a list of nodes written as text into
real nodes in a Blueprint graph, and does the way back.

This project follows the project-docs convention (ROADMAP.md, DECISIONS.md, HANDOFF.md).
When the user asks to resume ("retoma", "vamos continuar o projeto", "vamos continuar o desenvolvimento do roadmap"), before acting: read HANDOFF.md; from ROADMAP.md, only the titles (`grep "^### R-" ROADMAP.md`) and the item for the next step or the request. Confirm the state and the next step in 3 lines and wait for the go-ahead.

**The format's specification is [`Docs/FORMAT.md`](Docs/FORMAT.md), the core.
Read that file before writing or interpreting NodeScribe text** -- the parts
that cannot be guessed (`$name.Pin`, `Class.Function`, `event X of Y`, indented
labels, `?` as a declared hole) are there. Its last section lists every
keyword. A topic in `Docs/format/` (AnimGraph, Behavior Tree, Designer, the
details of a reading) only when the task touches it; the core's *Topics* table
says when.

When someone pastes text in that format asking for help with a graph, it is
the output of the **Copy Whole Graph** button. The answer should come back in
the same format, to be pasted back with **Paste**.

## Why it exists

To spend as few tokens as possible in the conversation between the AI and
Unreal. The saving comes from **eliminating discovery** -- the catalog resolves
names locally --, not from shortening the text. When touching things here, do
not trade readability for bytes: the text being checkable by a person is what
makes the rest trustworthy.

## Principle that guides the code

When it cannot decide safely, **it does not decide**. An ambiguous node becomes
a red comment in the graph; an unspecified asset becomes an empty pin that
blocks compiling; a node the format cannot recreate is not created.

A guessed plausible node is the worst possible result: it compiles, runs, and
is wrong. When touching things here, keep that -- when in doubt, fail out loud.

## Language

Everything is in English: identifiers, comments, log and error messages, UI
text, documentation and the format's keywords. The keywords are English only;
there are no aliases in other languages. Names that come from a project
(variables, pins, assets) may be in any language, and the lookup ignores
accents for them.

Two exceptions, both read by the user (D-009): the main README is
`README.pt-BR.md`, in Portuguese, and `README.md` is its English translation --
change the first and translate into the second in the same commit; the
ROADMAP items are Portuguese.

## Structure

| file | role |
|---|---|
| `NodeScribeParser` | text → statements. Knows nothing about Unreal. |
| `NodeScribeCatalog` | index of callable UFunctions, built at runtime. |
| `NodeScribeBuilder` | statements → real nodes, linked and positioned. |
| `NodeScribeReader` | the way back: nodes → text. |
| `NodeScribeGraphActions` | the three buttons on the editor toolbar. |
| `NodeScribeWidgetTree` | a Widget Blueprint's Designer as text, both ways: the `designer` section of `read_object` / `write_object`. |
| `Tests/run_tests.py` | automated round trip, without an interface, plus `token_budget`: ceilings on what the docs, the tools and a reading cost. Run it before and after touching the reader, the builder or the docs; the command is in *Testing*, in `Docs/development.md`. |
| `Saved/NodeScribe/vocabulary.txt` | the names nobody managed to resolve, one per line. `sort \| uniq -c \| sort -rn` gives the list of aliases missing from the catalog, by frequency. It is not an error log -- see *The missing vocabulary*, in `Docs/development.md`. |

Reader and builder are mirrors: when teaching a new kind of node, both change.
A new family of nodes gets its own pair of files, as `NodeScribeAnimGraph`
did; a new kind of asset gets a shape of `read_object` / `write_object` and a
topic in `Docs/format/`, not a new tool (D-016).

## Before touching the code

Read [`Docs/development.md`](Docs/development.md), and *Status* in the README
for where the plugin is. The first holds the cycle of closing the editor →
compiling → reopening → testing (with the commands ready), how to test by
round trip, the invariants that must not break, how to grow without getting heavier, and what a proposal
for a new tool needs to answer before becoming code. `Docs/history.md` and
`Docs/ideas.md` are not read by default.

Current front: the next step in HANDOFF.md, and the pending work in order in
ROADMAP.md (its items are in Portuguese, D-009). An idea for a new tool goes to
the ROADMAP only once the user accepts it, and its proposal answers the four
questions in *Proposing a new tool*, in `Docs/development.md`, first.

## Lessons that cost a build

- **Helpers in an anonymous namespace need names unique across the module.**
  UBT compiles the module as one unity blob, so two files' `CleanClassName` or
  `FDelete` collide. A green unity build also hides a missing include: to
  check, put `bUseUnity = false;` next to `PCHUsage` in the Build.cs, build,
  and take it out again.
- **Git:** commit straight to `main`, no branches; never push without an
  explicit request.
