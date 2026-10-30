---
title: "A Get inside a function may be a local variable or a parameter, and array functions need their own node class"
author: Antônio
date: 2026-10-30
branch: main
cost: { minutes: 15, tokens: 113820 }
id: l-a1d8e27f
scope: repo
status: active
src: agent
touches:
  - Docs/format/reading.md
  - HANDOFF.md
  - ROADMAP.md
  - Source/NodeScribeEditor/Private/NodeScribeBuilder.cpp
  - Source/NodeScribeEditor/Private/NodeScribeReader.cpp
  - Tests/run_tests.py
  - Docs/FORMAT.md
---

## Decided
A Get in a function can read a local variable or a parameter of that function; the builder checks both before the catalog (`FindLocalVariable`, `IsFunctionParameter`), because otherwise `Get SkeletalMesh` becomes an unrelated editor function of the same name. Array library functions must be `UK2Node_CallArrayFunction`: a plain `UK2Node_CallFunction` leaves their wildcards untyped, so the link goes in and only the compiler complains.

## Watch out
Fixing one compile error can reveal another Blueprint: `ExecCalculationOndaDeChoque` only entered the stopped-compiling list after its Return stopped being deleted, so count again after each fix. `SetDesiredReturnType` and `IsSetToReturnRef` of `UK2Node_GetArrayItem` are unusable from a plugin: not exported and private; use the `bReturnByRefDesired` property and the result pin. By-ref array items do not apply to every type: on an Array of Actor the node stayed `Get (a copy)`, on an Array of Vector it worked.
