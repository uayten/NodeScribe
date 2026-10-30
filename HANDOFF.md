# Handoff

Updated: 2026-10-30

## State
- R-016 approved and committed (`0a47180`). R-017 to R-021 are in test, not committed: implemented together, with two fixes outside the groups (a self pin beside a parameter called `Target`; an emptied graph that read with no header).
- Suite: 42 of 42. `project_graphs_round_trip` passes: 213 graphs, 0 silent differences. The commandlet still exits 1 from an Engine ensure when deleting the copy of `BossRushGameMode` (already in the logs before this session).
- External, in BossRush (not committed by this session): `Content/BossRush/Testes/BP_TesteGolem` saved with the AbilitySystem component re-created (the saved asset had lost it; cause not found). The other changes there (`Zoo`, the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's. `BP_TesteInsercao` was never saved.

## Next step
The user approves R-017 to R-021 (commit them together); then the ROADMAP is empty: ask for the next front.

## Notes for the next session
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
