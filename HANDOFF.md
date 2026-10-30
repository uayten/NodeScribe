# Handoff

Updated: 2026-10-30

## State
- Branch `main`, everything committed; R-031 and this handoff are not pushed. R-032 and R-033 are the pending items.
- Suite: 49 of 49. `project_graphs_round_trip`: 213 graphs, 0 silent differences, 53 declared by a warning. 5 Blueprints stop compiling when recreated, 2 already had errors in the original. The commandlet exits 1 from an Engine ensure when deleting the copy of `BossRushGameMode`: judge by `Saved/NodeScribe/tests.txt`, and check its time, since a run that never started leaves the old one.
- The 5: `ABP_Manny_PostProcess` and `ABP_Quinn_PostProcess` (Pose Driver, declared), `PáginaInput` (`Get Player Input Mappings Static`, which the reading says it cannot name), `BP_Pedra` (a loose chain in the original using two variables it no longer has). `BP_Skeleton` (`$inputTouch`) is inferred to be the delegate-bound input limit, not confirmed on that node.
- Measured on 83 graphs of 53 Blueprints, before R-031: of the warnings left, 22 are reconvergence or side pins (R-033) and 15 are nodes with logic of their own, such as `Play Montage` and the ability tasks (R-032).
- External, in BossRush (not committed by this session): 13 assets of `Content/BossRush/Testes` deleted at the user's request, leaving `BP_TesteGolem` and `BB_Teste`, the sandbox the project's CLAUDE.md names. `BP_TesteGolem` may hold pasted test nodes, and was saved earlier with the AbilitySystem component re-created (cause not found). `save_all_and_quit` also saved `Zoo` and the assets of `TesteDeImportação`, which had unsaved changes. The other changes there (the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's. `Plugins/JoyShockLibrary4Unreal/HANDOFF.md` still says `BP_TesteSensoresPro2` awaits deletion; it is deleted.

## Next step
R-032: Play Montage and the ability tasks. First move part of the core spec to the topics: it is at 19,928 of 20,000 characters.

## Notes for the next session
- `trail check` flags `ReadNodePositions` and `ReadCompileErrors` against the learning on `read_object`, and D-029 against the one on event pins: false positives, already said in the commit messages. Read each conflict before committing; a new one may be real.
- `ProgrammaticToolset` fails on an empty list from `get_referencers`, and a script reading every graph takes the editor open; the core spec cannot take another row without the move in R-032.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
