# Handoff

Updated: 2026-10-31

## State
- Branch `main`, everything committed. R-032 and R-033 are closed: the user confirmed both with Paste in the editor. Their commit is not pushed. The roadmap is empty.
- Suite: 55 of 55. `project_graphs_round_trip`: 213 graphs, 0 silent differences, 39 declared by a warning. 4 Blueprints stop compiling when recreated, all with a warning, and 2 already had errors in the original. The commandlet exits 1 from an Engine ensure when deleting the copy of `BossRushGameMode`: judge by `Saved/NodeScribe/tests.txt`, and check its time, since a run that never started leaves the old one.
- The 4: `ABP_Manny_PostProcess` and `ABP_Quinn_PostProcess` (Pose Driver: the text does not carry the pose asset), `PáginaInput` (`Get Player Input Mappings Static` and `Set Player Input Preset by Tag`, not in the Enhanced Input headers or the project's C++; where they come from was not found), `BP_Skeleton` (`InputTouch` and `InputAction Run`, the legacy input events). No item in the roadmap yet: the user decides.
- Warnings left, in 39 graphs: 39 names it cannot write, 28 Pose Driver assets, 11 interface calls, 10 delegate-bound events, 6 component templates, and 7 others. None is a reconvergence, a side pin or a task any more.
- External, in BossRush (not committed by this session): 13 assets of `Content/BossRush/Testes` deleted at the user's request, leaving `BP_TesteGolem` and `BB_Teste`, the sandbox the project's CLAUDE.md names. `BP_TesteGolem` may hold pasted test nodes, and was saved earlier with the AbilitySystem component re-created (cause not found). `save_all_and_quit` also saved `Zoo` and the assets of `TesteDeImportação`, which had unsaved changes. The other changes there (the Tigre animations, `Grant_BlendSpace`, the JoyShock pointer) are the user's. `Plugins/JoyShockLibrary4Unreal/HANDOFF.md` lost its line about `BP_TesteSensoresPro2`, uncommitted, next to a `ROADMAP.md` change there that is the user's.

## Next step
None in the roadmap. The user chooses whether the 4 Blueprints above become items.

## Notes for the next session
- `trail check` flags `ReadNodePositions` and `ReadCompileErrors` against the learning on `read_object`, and D-029 against the one on event pins: false positives, already said in the commit messages. Read each conflict before committing; a new one may be real.
- `trail check` also flags `NewObject`-style lines in `NodeScribeBuilder.cpp` and `run_tests.py` against the same learning on `read_object`: lexical, false positives.
- `ProgrammaticToolset` fails on an empty list from `get_referencers`, and a script reading every graph takes the editor open.
- `UEdGraph::Nodes` is protected in Python; `read_graph` is the way to look at a graph from a commandlet script.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- In a Bash heredoc on this machine, `\\n` inside a Python string did not survive as backslash-n; multi-line replacement scripts went through a file written with the Write tool instead.
- The reading writes exec labels capitalised (`True:`, `False:`): compare labels case-insensitively in tests.
- The Metahuman project has its own older NodeScribe clone; testing there means updating that clone first.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph text, and `Docs/design.md` before changing a rule of the format.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
