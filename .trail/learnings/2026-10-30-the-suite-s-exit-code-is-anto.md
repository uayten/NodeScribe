---
title: "The suite's exit code is 1 even with 0 failures; read tests.txt, and run it with Start-Process -Wait"
author: Antônio
date: 2026-10-30
branch: main
cost: { minutes: 1, tokens: 1470 }
id: l-cc7275df
scope: repo
status: active
src: agent
touches:
  - Docs/development.md
---

## Watch out
- `UnrealEditor-Cmd ... run_tests.py` exits 1 even when `Saved/NodeScribe/tests.txt` says `0 failure(s)`, because an Engine ensure in `ObjectTools.cpp` ("Failed to unload all packages during ForceDeleteObjects", `/Game/NodeScribeTests/Project/BossRushGameMode_0`) fires while `project_graphs_round_trip` deletes its copies. It is in logs from before R-016 (2026-10-30 05:44), so it is not a plugin regression. Judge the run by tests.txt, not by the exit code.
- With `--%` (Docs/development.md), PowerShell passes everything after it to the engine, including `; Get-Content ...` and `> $null`, so the follow-up command never runs; the call also came back while the commandlet was still running, and the tests.txt read then was the previous run's. Check its timestamp.

## Decided
- Run the suite with `Start-Process ... -ArgumentList '<same args>' -NoNewWindow -Wait -PassThru -RedirectStandardOutput <file>`, then read tests.txt, because it waits for the commandlet to end and lets a follow-up command run, which `--%` does not.
