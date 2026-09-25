# NodeScribe

Unreal editor plugin that transcribes a list of nodes written as text into
real nodes in a Blueprint graph, and does the way back.

**The format's specification is [`Docs/FORMAT.md`](Docs/FORMAT.md). Read that
file before writing or interpreting NodeScribe text** -- the parts that cannot
be guessed (`$name.Pin`, `Class.Function`, `event X of Y`, indented labels,
`?` as a declared hole) are there. Its last section lists every keyword.

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

## Structure

| file | role |
|---|---|
| `NodeScribeParser` | text → statements. Knows nothing about Unreal. |
| `NodeScribeCatalog` | index of callable UFunctions, built at runtime. |
| `NodeScribeBuilder` | statements → real nodes, linked and positioned. |
| `NodeScribeReader` | the way back: nodes → text. |
| `NodeScribeGraphActions` | the three buttons on the editor toolbar. |
| `Tests/run_tests.py` | automated round trip, without an interface. Run it before and after touching the reader or the builder; the command is in *Testing*, in the README. |
| `Saved/NodeScribe/vocabulary.txt` | the names nobody managed to resolve, one per line. `sort \| uniq -c \| sort -rn` gives the list of aliases missing from the catalog, by frequency. It is not an error log -- see *The missing vocabulary*, in the README. |

Reader and builder are mirrors: when teaching a new kind of node, both change.

## Before touching the code

Read **Development** and **Status** in the [`README.md`](README.md). They hold:
where the plugin is and what the current front is, the cycle of closing the
editor → compiling → reopening → testing (with the commands ready), how to test
by round trip, the four invariants that must not break, and what a proposal
for a new tool needs to answer before becoming code.

Current front: **reading an enemy's whole AI**. The sheet (`read_object` /
`write_object`), the blackboard, the BT tree, `create_asset` and Gameplay Tags
(`read_tags` / `write_tags`) are already up; **writing Behavior Trees** and
**Gameplay Effect components** are missing. The roadmap with each stage's state
is in *Where we are on that list*, in the README. An idea for a new tool that
shows up along the way goes to *Possible future features*, not to the code.
