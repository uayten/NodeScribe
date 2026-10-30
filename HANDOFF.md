# Handoff

Updated: 2026-10-30

## State
- R-016, R-018, R-019 and R-022 are done. R-017, R-020 and R-021 are committed and in test, each with its remaining step in ROADMAP > Em teste. The user pasted `Saved/teste_r016_r021.txt` into `BP_TesteGolem`: all fine except R-023 (overlap).
- Suite: 44 of 44. `project_graphs_round_trip`: 213 graphs, 0 silent differences. It compares readings only: whether each copy compiles is not checked. The commandlet exits 1 from an Engine ensure when deleting the copy of `BossRushGameMode` (already in the logs before this session): judge by `Saved/NodeScribe/tests.txt`.
- External, in BossRush (not committed by this session): `Content/BossRush/Testes/NS_Validacao` is a saved scratch asset, to delete after the tests; `BP_TesteGolem` may hold the pasted test nodes. `BP_TesteGolem` was also saved earlier with the AbilitySystem component re-created (the saved asset had lost it; cause not found). The other changes there (`Zoo`, the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's.

## Next step
R-023: reproduce the overlap, starting from the frame layout in `FNodeScribeBuildContext::Run` (`BaseY`, `BranchesOpened`): an event after a chain with branches gets a row the branches already use. Then ask the user for the remaining steps in test.

## Open questions
- Should `project_graphs_round_trip` also check that each copy compiles? Proposed to the user, not answered.

## Notes for the next session
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
