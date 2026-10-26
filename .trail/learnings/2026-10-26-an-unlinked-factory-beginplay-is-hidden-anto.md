---
title: An unlinked factory BeginPlay is hidden by read_graph but blocks event BeginPlay in write_graph
author: Antônio
date: 2026-10-26
branch: main
cost: { minutes: 3, tokens: 16597 }
id: l-a4967036
scope: repo
status: active
src: agent
touches:
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeReader.cpp
  - Docs/FORMAT.md
---

## Watch out
- A new Blueprint's EventGraph carries factory event stubs (Event BeginPlay and friends) with nothing linked. The reader leaves an unlinked stub out of `read_graph`, but the builder still finds it and refuses `event BeginPlay` with "already exists in this graph". So a graph whose reading shows no BeginPlay cannot take one, and the text gives no way to see why. Hit in `/Game/BossRush/Testes/BP_TesteGolem`: a fresh agent had to delete the stub (`K2Node_Event_0`) with the native `BlueprintTools.delete_node`, paying the native toolset's discovery to do it.
- Until the builder adopts an unlinked existing event instead of refusing, the way through is `clear_graph` on a scratch graph, or deleting the stub by hand.

## Decided
- A change to the format docs is checked by a fresh subagent given the task the way a new conversation would ask it, without being told it is a test, and asked to report every file and `get_format_docs` call it made. It read the core, then `reading` only because the graph carried warnings, which is what the core's Topics table says.
