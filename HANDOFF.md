# Handoff

Updated: 2026-10-26

## State
- Nothing in test. Approved and closed this session: R-001 (Widget Designer as text), R-008 (FORMAT.md as a core plus topics), R-012 (`token_budget` in the suite), R-013 (README), R-014 (an empty existing event is reused, D-019).
- The format: `Docs/FORMAT.md` is the core, `Docs/format/` holds `animgraph`, `behavior-tree`, `designer`, `reading`; `get_format_docs(topic)` returns one (D-017).
- Docs: `README.pt-BR.md` is the main README, `README.md` its translation (D-009); building and testing live in `Docs/development.md` (D-018).
- Built (unity); suite 26/26. `token_budget`: core 19,201 of 20,000 (the closest ceiling), animgraph 9,271 of 10,000, descriptions 9,846, tools 11 of 11, worst reading ratio 1.86.
- Branch `main`, all committed, not pushed. Trail is set up; its learnings are in `.trail/learnings/`.
- External: BossRush's own `CLAUDE.md` points at the core and the topics; it sits uncommitted in BossRush with the user's own pending changes. Its *Limites conhecidos* still says writing a Behavior Tree does not exist, which is stale.
- External: the `roadmap` and `project-docs` skills in `~/.claude` were changed this session (test steps the agent checks itself; README shape), uncommitted there.

## Next step
R-002, first in the ROADMAP: read the AbilitySystem's `Default Starting Data` on BossRush's `BP_Golem`. Start from `read_object` on `BP_Golem` with the filter `Default Starting Data`, and see why the sheet cannot reach it.

## Open questions
- R-009 (lean prose in FORMAT.md) was proposed with a way to measure it: a fresh agent writes ~10 typical BossRush graphs from the core alone, the suite counts how many paste cleanly, then the prose is trimmed and the count must not drop. The user has not said yes; ask before doing it, and before R-002 if the core's ceiling gets in the way.

## Notes for the next session
- Drafts in `/Game/BossRush/Testes/`: `BP_TesteEvento` (R-014), `WBP_TesteDesigner` (R-001, Temperature row, `NSVisTest`), `BP_TesteGolem` (two old red comments and a `Pular` line that does not resolve). All scratch.
- `save_all_and_quit` saves every dirty asset. When the user has used the editor in the session, ask before closing, and never let `/JoyShockLibrary4Unreal/LV_JoyShockDemo` be saved: it is slated to be reverted with git.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- Computer use cannot find the Unreal editor window by name; ask the user what the screen shows.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph or Designer text, and the topic the task touches.
- `Docs/development.md` when building or testing; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md.
