---
title: "A warning on every function graph was hiding 20 real differences, one of them an override calling itself"
author: Antônio
date: 2026-10-30
branch: main
cost: { minutes: 22, tokens: 78222 }
id: l-ff0fb876
scope: repo
status: active
src: agent
touches:
  - DECISIONS.md
  - Docs/FORMAT.md
  - Docs/format/reading.md
  - HANDOFF.md
  - ROADMAP.md
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeLibrary.cpp
  - Source/NodeScribeEditor/Private/NodeScribeReader.cpp
  - Tests/run_tests.py
---

## Decided
The function entry is a note, not a warning. A blanket warning on every function made `project_graphs_round_trip` count each difference in a function as declared, and made `replace` refuse every function.

## Watch out
A warning that fires on a whole class of graphs hides real defects in them: removing this one exposed 20 silent differences, among them a call to the parent read as `Class.Function`, which came back as the override calling itself, compiling and never returning. Before trusting the declared count, ask what each warning is covering.
A kept Return still has its wires: the Entry can lead straight to it, so `RemoveDeletableNodes` breaks them. An interface function's Return reports it cannot be deleted, so that check has to come after.
