# Handoff

Updated: 2026-10-26

## State
- R-001 (Widget Designer as text) approved by the user on 2026-10-26 and closed.
- Token work, done and in test (R-008, R-012, R-013): `Docs/FORMAT.md` is now the core (~19 KB, ~4.7k tokens, was 36 KB) and `Docs/format/` holds the topics `animgraph`, `behavior-tree`, `designer`, `reading`; `get_format_docs(topic)` returns one (D-017). The suite has `token_budget`, with ceilings in `BUDGET` at the top of `Tests/run_tests.py`. The README's history went to `Docs/history.md`, its proposals to `Docs/ideas.md` (D-018). New kinds of asset ride on `read_object` / `write_object`, not new tools (D-016).
- Built (unity); suite 25/25; `get_format_docs(topic)` answers through MCP in the editor.
- Branch `main`, committed, not pushed. Trail is set up (`.trail/`, `.claude/settings.json`, `.ignore`).
- External: BossRush's own `CLAUDE.md` now points to the core and the topics, and mentions the `designer` section. It sits uncommitted in BossRush together with the user's own pending changes there.

## Next step
The user runs the test steps of R-008, R-012 and R-013. On approval, delete each item; then R-002 (read the AbilitySystem's `Default Starting Data` on BossRush's `BP_Golem`).

## Notes for the next session
- Draft assets: `/Game/BossRush/Testes/WBP_TesteDesigner` has Battery and Temperature rows and an `NSVisTest` event from R-001's test; it may be unsaved. It is scratch.
- `save_all_and_quit` saves every dirty asset, the user's too. Ask the user before closing the editor, and never let `/JoyShockLibrary4Unreal/LV_JoyShockDemo` be saved: it is slated to be reverted with git.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- The suite command (README, *Testing*): pass `-script=...` as one quoted argument; `--%` swallows anything after it on the line, `> $null` included.
- Current budget numbers: core 18,842 chars, animgraph 9,271 (ceiling 10,000, the closest), descriptions 9,846, tools 11 of 11, worst reading ratio 1.86.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph or Designer text, and the topic the task touches.
- README *Status* and *Development* only when building; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md (history and proposals, not plans).
