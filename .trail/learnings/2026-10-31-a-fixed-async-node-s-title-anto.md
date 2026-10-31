---
title: "A fixed async node's title needs a Blueprint, and a promotable operator's pin hides the parameter type"
author: Antônio
date: 2026-10-31
branch: main
cost: { minutes: 33, tokens: 352100 }
id: l-5f6a5518
scope: repo
status: active
src: agent
touches:
  - Source/NodeScribeEditor/NodeScribeEditor.Build.cs
  - Source/NodeScribeEditor/Private/NodeScribeCatalog.h
  - Source/NodeScribeEditor/Private/NodeScribeCatalog.cpp
  - Source/NodeScribeEditor/Private/NodeScribeTypes.h
  - Source/NodeScribeEditor/Private/NodeScribeParser.cpp
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeReader.cpp
  - Tests/run_tests.py
  - Docs/format/nodes.md
  - Docs/FORMAT.md
---

## Decided
- Nodes with a fixed factory (`Play Montage`, `AI MoveTo`) are named by a throwaway node inside a throwaway `UBlueprint`, in `FNodeScribeCatalog::Build`. A fixed node loses to a function only when that function is *shown* under the name (`IsShownAs`): the task `AIMoveTo` has that raw name but is shown as `Move To Location or Actor`.
- A promotable operator's conversion note names the function's parameter, since the line comes back as a plain call: the pin on screen shows the wire's type, the copy shows the parameter's, and the texts differed. Creating real `UK2Node_PromotableOperator`s was not done: its wildcard pins take neither a literal nor a split field until wired, and `Add_VectorVector (A = $loc, B Z = 3.0)` depends on both.

## Tried and ruled out
- `GetNodeTitle` on a class default or a node in the transient package: `UK2Node_BaseAsyncTask::GetNodeTitle` resolves the factory through the node's Blueprint, and with none the editor dies reading address 0x30. `GetFactoryFunction` on a class default logs an error per class fixing none, so the field is read by reflection.

## Watch out
- Removing the reconvergence warnings uncovered 4 silent differences they had been covering (D-031's pattern again): the promotable note, a wire of incompatible types (`Vector -> Float`) kept by an old asset, and `Switch on Int` being born without its cases.
