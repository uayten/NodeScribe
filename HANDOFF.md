# Handoff

Updated: 2026-10-26

## State
- The Widget Designer reads and writes as text (`designer` section of `read_object` / `write_object`), plus the six fixes from the other session's test (Break under `replace`, `Set Visibility` with a Target, Append growing pins, split-struct field names, typo suggestions, `read_graph` header). Built, unity and non-unity; suite 24/24; exercised in the editor. R-001 waits for the user's test.
- Branch `main`, everything in the handoff commit. Not pushed.
- External: BossRush's own CLAUDE.md still tells agents to read FORMAT.md whole and does not mention the `designer` section (R-008 touches the first).

## Next step
The user runs R-001's test steps. On approval, delete the item; then R-002 (read the AbilitySystem's `Default Starting Data` on BossRush's `BP_Golem`).

## Notes for the next session
- Draft assets: `/Game/BossRush/Testes/WBP_TesteDesigner` (saved, has a Battery row). The editor may hold unsaved test events in it; discard them.
- `save_all_and_quit` saves every dirty asset, the user's too. Ask the user before closing the editor, and never let `/JoyShockLibrary4Unreal/LV_JoyShockDemo` be saved: it is slated to be reverted with git.
- `/JoyShockLibrary4Unreal/...` assets belong to a public plugin: read, never write.
- The suite command (README, *Testing*): pass `-script=...` as one quoted argument; `--%` swallows anything after it on the line.

## Read first
- CLAUDE.md, then Docs/FORMAT.md for any graph or Designer text.
- README *Development* only when building; DECISIONS.md when a choice comes up.
- Not worth reading: README *Possible future features* (proposals, not plans).
