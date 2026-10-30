---
title: "A round trip that closes says nothing about compiling, and a delegate wire checks the skeleton's signature"
author: Antônio
date: 2026-10-30
branch: main
cost: { minutes: 42, tokens: 561324 }
id: l-3d9c7723
scope: repo
status: active
src: agent
touches:
  - ROADMAP.md
  - Source/NodeScribeEditor/Private/NodeScribeCatalog.cpp
  - Tests/run_tests.py
  - DECISIONS.md
  - Docs/FORMAT.md
  - Docs/design.md
  - Docs/format/reading.md
  - HANDOFF.md
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeParser.cpp
---

## Watch out
- T1 == T2 holds for a line lost or wrong on the first write, because both readings carry the same loss. `dispatcher_on_variable` and `event_bound_before_its_line` passed for a long time with a Bind whose event wire was never made, and only failed once `write_graph` started compiling (D-028). `project_graphs_round_trip` still compares readings only, so its 0 silent differences does not mean the copies compile. For what a fixed point cannot see, use `expects_in_reading` or compile.
- `UEdGraphSchema_K2` compares a delegate pin with the event's function in the skeleton class (`EdGraphSchema_K2.cpp`, `IsSignatureCompatibleWith`), so adding user pins to a Custom Event is not enough: the wire is refused until the skeleton is regenerated, because the function there still has the old signature. Regenerate, then find the pins again by name, since regenerating may rebuild them.

## Tried and ruled out
- Creating the event's user pins from the dispatcher's signature and retrying the connection right away: it failed with the same "The link was not made" warning, because of the stale skeleton above.
