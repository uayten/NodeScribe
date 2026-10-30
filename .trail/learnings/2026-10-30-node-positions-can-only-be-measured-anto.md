---
title: "Node positions can only be measured through ReadNodePositions, and get_referencers fails inside a tool script on an empty result"
author: Antônio
date: 2026-10-30
branch: main
cost: { minutes: 23, tokens: 135317 }
id: l-204b7d3c
scope: repo
status: active
src: agent
touches:
  - HANDOFF.md
  - ROADMAP.md
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeLibrary.cpp
  - Source/NodeScribeEditor/Public/NodeScribeLibrary.h
  - Tests/run_tests.py
  - Docs/development.md
---

## Decided
Layout is tested by `layout_leaves_no_overlap` through `UNodeScribeLibrary::ReadNodePositions`, a library function kept out of the toolset. Positions stay out of the format, so a round trip cannot see a collision: every node in one spot reads the same.

## Tried and ruled out
`read_object` on a graph or on a node path (`...:EventGraph.K2Node_CustomEvent_0`) with filter `pos`/`node` returns nothing: `NodePosX` and `Graph->Nodes` are not shown.

## Watch out
`AssetTools.get_referencers` called inside `ProgrammaticToolset.execute_tool_script` raises `'NoneType' object is not iterable` for an asset with no referencers, and the script's result is lost. That error does not prove anything by itself: call the tool directly, where the same case returns `[]`.
The test's box (300 x 150) is smaller than a tall node, so a data stack touching a tall `Spawn Actor` above it passes.
