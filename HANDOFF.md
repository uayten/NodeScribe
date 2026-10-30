# Handoff

Updated: 2026-10-30

## State
- Nothing in test. R-003 to R-007 and R-015 were approved by the user on screen. R-015 is `after <a line of the reading>:` (D-025), committed in `7fe313c` with the suite case `insert_into_existing_chain`.
- Suite: 32 of 33. The one failure is `project_graphs_round_trip`, on purpose: 24 graphs of the project come back different without a warning. Diffs in `Saved/NodeScribe/project_graphs.txt`. Its groups are now R-016 to R-021, and each closes when its group leaves the suite's list of silent differences.
- External, in BossRush (not committed by this session): `Content/BossRush/Testes/BP_TesteGolem` saved with the AbilitySystem component re-created (the saved asset had lost it; cause not found). The other changes there (`Zoo`, the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's. `BP_TesteInsercao` was never saved.

## Next step
R-016: parameters of custom and interface events. Start from the diffs of that group in `Saved/NodeScribe/project_graphs.txt`, then propose the syntax for declaring an event's parameters to the user before coding: it is a format rule, so it goes to FORMAT.md's core, `Docs/design.md` and both reader and builder.

## Notes for the next session
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
