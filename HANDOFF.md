# Handoff

Updated: 2026-10-30

## State
- R-016 to R-026 are done: the user read R-017 back and approved R-023 in the editor; R-020 and R-021 were confirmed through `read_graph` of `NS_Validacao` before it was deleted. R-026 is not committed.
- R-024 makes `project_graphs_round_trip` compile each original and its copy, always (`Docs/development.md` > Testing): 10 Blueprints stop compiling when recreated (16 before R-025 and R-026), all with a warning in the reading of one of their graphs; 2 already had errors in the original. A warning covers the Blueprint, not each error, so R-027 checks the ones it may not explain. The Pose Driver errors are the declared limit, not an item.
- Suite: 47 of 47, with `layout_leaves_no_overlap` measuring positions through `ReadNodePositions` (library only, not a tool). It failed before the fix. `project_graphs_round_trip`: 213 graphs, 0 silent differences, 69 declared by a warning. The commandlet exits 1 from an Engine ensure when deleting the copy of `BossRushGameMode` (already in the logs before this session): judge by `Saved/NodeScribe/tests.txt`.
- External, in BossRush (not committed by this session): 13 assets of `Content/BossRush/Testes` deleted at the user's request, leaving `BP_TesteGolem` and `BB_Teste`, the sandbox the project's CLAUDE.md names. The deletions are not committed there. `BP_TesteGolem` may hold the pasted test nodes, and was saved earlier with the AbilitySystem component re-created (the saved asset had lost it; cause not found). The other changes there (`Zoo`, the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's.

## Next step
R-027: for each Blueprint listed there, whether the error comes from what the reading warned or is a silent defect. `Saved/NodeScribe/project_graphs.txt` has the errors.

## Open questions
None.

## Notes for the next session
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
