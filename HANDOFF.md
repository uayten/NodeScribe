# Handoff

Updated: 2026-10-26

## State
- In test (the user's screen checks, steps in ROADMAP): R-003 components by text, R-004 Input Mapping Context, R-005 Designer moves and reorders, R-006 named slots, R-007 Gameplay Effect components. All of them were exercised through the MCP in the editor and are covered by suite cases (`components_by_text`, `input_mapping_section`, `designer_moves_and_slots`, `effect_components_block`).
- Closed by the agent's own checks, to go in the same commit: R-002 (`Default Starting Data` reads now; it only initialises attribute sets, and no Golem Blueprint calls Give Ability -- F-005), R-009 (core 20,707 → 16,528 characters; two fresh agents wrote the same ten graphs, 8 of 10 pasted clean with both versions -- D-023), R-010 (`project_graphs_round_trip`, D-024), R-011 (state machine with transition rules read, cleared, pasted back, read the same and compiled with warnings as errors, in `/Game/BossRush/Testes/ABP_TesteSM` -- in BossRush, not Metahuman, whose NodeScribe is an older separate clone).
- Also fixed on the way: Spawn Actor from Class crashed `write_graph` (pins before PostPlacedNewNode, F-006, case `spawn_actor`); struct-member writes now report the struct as the member property; component-pointer variables no longer print `= None` in the sheet.
- Suite: 31 of 32. The one failure is `project_graphs_round_trip`, on purpose: 24 graphs of the project do not come back the same and say nothing. Diffs in `Saved/NodeScribe/project_graphs.txt`.
- Nothing committed this session: the user's rule is to commit when the tests are confirmed. `main` has all of it uncommitted, plus `Docs/design.md`, three new source pairs and two new topics.
- External, uncommitted in BossRush: its `CLAUDE.md` now says the Golem's abilities are not granted, Behavior Trees are written by `write_object`, and components are created with `component`. Scratch assets saved in `/Game/BossRush/Testes/`: `GE_TesteCooldown`, `IMC_Teste`, `WBP_TesteSlotHost`, `WBP_TesteSlotChild`, `ABP_TesteSM`, plus changes to `BP_TesteGolem` and `WBP_TesteDesigner`.

## Next step
The user's checks of R-003 to R-007. Then, if the user accepts it as an item, the 24 silent differences R-010 found, grouped: custom and interface events lose their parameters (the format has no way to declare them); split struct pins on Spawn Actor and SetColorAndOpacity; names starting with a digit (`3 Corações`) and numeric Switch labels (`0:`); an interface function that also has an implementation reads unqualified and comes back ambiguous; Expose on Spawn with the class from a variable; an interface message losing its Target; a comment losing its parenthesised part. The R-009 agents hit two of these too (`Switch on Int` labels, ambiguous `Get Display Name`).

## Open questions
- None waiting on the user beyond the tests and whether the R-010 findings become an item.

## Notes for the next session
- `save_all_and_quit` saves every dirty asset. When the user has used the editor in the session, ask before closing, and never let `/JoyShockLibrary4Unreal/LV_JoyShockDemo` be saved: it is slated to be reverted with git.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph or Designer text, and the topic the task touches.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up; `Docs/design.md` before changing a rule of the format.
- Not worth reading: Docs/history.md and Docs/ideas.md.
