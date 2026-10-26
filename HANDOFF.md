# Handoff

Updated: 2026-10-26

## State
- R-001 (Widget Designer as text) approved by the user on 2026-10-26 and closed.
- Token work: `Docs/FORMAT.md` is now the core (~19 KB, ~4.7k tokens, was 36 KB) and `Docs/format/` holds the topics `animgraph`, `behavior-tree`, `designer`, `reading`; `get_format_docs(topic)` returns one (D-017). The suite has `token_budget`, with ceilings in `BUDGET` at the top of `Tests/run_tests.py`. New kinds of asset ride on `read_object` / `write_object`, not new tools (D-016). R-008 and R-012 are in test.
- README (R-013, approved): `README.pt-BR.md` is the main one, opening with a running paragraph; `README.md` is its English translation (D-009). History went to `Docs/history.md`, proposals to `Docs/ideas.md`, and everything for working on the plugin to `Docs/development.md`, English only (D-018).
- Built (unity); suite 25/25; `get_format_docs(topic)` answers through MCP in the editor.
- Branch `main`, committed, not pushed. Trail is set up (`.trail/`, `.claude/settings.json`, `.ignore`).
- External: BossRush's own `CLAUDE.md` now points to the core and the topics, and mentions the `designer` section. It sits uncommitted in BossRush together with the user's own pending changes there.

## Next step
R-012 waits for the user's check (the last line of `tests.txt`); R-008's last step happens in the next graph conversation in BossRush. Then R-009 (lean prose in FORMAT.md, measured by a fresh agent writing graphs from the core alone, as proposed to the user) or R-002 (read the AbilitySystem's `Default Starting Data` on BossRush's `BP_Golem`).

## Notes for the next session
- Draft assets: `/Game/BossRush/Testes/WBP_TesteDesigner` has Battery and Temperature rows and an `NSVisTest` event from R-001's test; it may be unsaved. It is scratch.
- `save_all_and_quit` saves every dirty asset, the user's too. Ask the user before closing the editor, and never let `/JoyShockLibrary4Unreal/LV_JoyShockDemo` be saved: it is slated to be reverted with git.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- The suite command (`Docs/development.md`, *Testing*): pass `-script=...` as one quoted argument; `--%` swallows anything after it on the line, `> $null` included.
- Current budget numbers: core 18,842 chars, animgraph 9,271 (ceiling 10,000, the closest), descriptions 9,846, tools 11 of 11, worst reading ratio 1.86.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph or Designer text, and the topic the task touches.
- `Docs/development.md` only when building; DECISIONS.md when a choice comes up.
- Not worth reading: Docs/history.md and Docs/ideas.md (history and proposals, not plans).
