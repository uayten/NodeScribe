---
title: "token_budget measures graph lines, and FORMAT.md fences fool naive splitters"
author: Antônio
date: 2026-10-26
branch: main
cost: { minutes: 1, tokens: 1221 }
id: l-3fac1894
scope: repo
status: active
src: agent
touches:
  - "Tests/run_tests.py#token_budget"
  - Docs/FORMAT.md
---

## Decided
- `token_budget`'s reading ratio compares only the graph lines (`body()`) of the written text and of the reading. Measured on the whole reading it fails on small cases like `declared_hole`: on a three-line case the header and the expected warning are the whole ratio, so the number says nothing about how the reader grows.

## Watch out
- `Docs/FORMAT.md` has a prose line that starts with backticks (```` ``` ```` fences, in *What the parser ignores by itself*). A script that toggles "inside a code fence" on any line starting with ``` gets inverted from there on and misses every later `## ` heading. Toggle only on a line that is nothing but the fence.
- Computer use cannot find the Unreal editor by name ("Unreal Editor", "UnrealEditor" and the window title all fail in `request_access`), so the editor cannot be looked at through screenshots here; ask the user what they see.
