# NodeScribe: development

For whoever -- person or AI agent -- is going to touch the plugin without
having followed the conversation in which it was designed. Read
[`CLAUDE.md`](../CLAUDE.md) first for the file structure, and
[`FORMAT.md`](FORMAT.md) for the text format.

## Contents

- [The cycle: close, compile, reopen, test](#the-cycle-close-compile-reopen-test)
- [Testing](#testing)
- [The missing vocabulary](#the-missing-vocabulary)
- [Traps that already bit](#traps-that-already-bit)
- [What must not break](#what-must-not-break)
- [Growing without getting heavier](#growing-without-getting-heavier)
- [Proposing a new tool](#proposing-a-new-tool)
- [How the MCP setup works](#how-the-mcp-setup-works)

## The cycle: close, compile, reopen, test

Unreal holds the plugin's binaries while it is open, so recompiling requires
closing the editor. `save_all_and_quit` exists for that: with it an agent
closes, compiles, reopens and tests without anybody clicking.

1. Edit the C++.
2. `save_all_and_quit` through the MCP. It refuses while Play In Editor runs,
   and the MCP connection drops right after -- that is expected.
3. Compile:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BossRushEditor Win64 Development -Project="C:\Unreal Projects\BossRush\BossRush.uproject" -WaitMutex -NoUBA
```

4. Reopen:

```powershell
Start-Process "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Unreal Projects\BossRush\BossRush.uproject"'
```

5. Wait for the editor to come up; the MCP only answers after that.
6. Test through the MCP, read the Message Log's *NodeScribe* channel, repeat.

**If the MCP is down**, step 2 has a path that does not depend on it:

```powershell
New-Item -ItemType Directory -Force "C:\Unreal Projects\BossRush\Saved\NodeScribe" | Out-Null
Set-Content "C:\Unreal Projects\BossRush\Saved\NodeScribe\command.txt" "quit" -NoNewline
```

The plugin checks that file every half second, deletes it on reading and calls
the same `SaveAllAndQuit`, with the same refusals. The answer goes to
`response.txt` next to it. It exists so the cycle does not stall when the MCP
drops with the editor open; killing the process instead loses unsaved work and
skips the PIE check. A server of our own inside the plugin is not worth it: it
would still be a network protocol that has to be up, only maintained by us.

Things that cost time when forgotten:

- **The paths above belong to one machine** -- Engine on `E:`, project on `C:`.
- **`-NoUBA` is on purpose.** There the Unreal Build Accelerator's DLL
  injection is blocked and the build dies with error 740/9006.
- **The quotes inside `-ArgumentList` matter.** Without them the space in
  "Unreal Projects" makes the editor open `C:\Unreal` and show *"Missing
  Modules"*, which looks like a broken build and is not.
- **Check the build's output, not only the exit code.** `Build.bat` exits with
  0 in cases where it compiled nothing; look for `Result: Succeeded`.
- **Live Coding blocks the build.** With the editor open, `Build.bat` stops
  with `Unable to build while Live Coding is active` and shows no compile
  error. A new `UFUNCTION` or `UCLASS` needs a restart anyway. Close first.

## Testing

The test that catches the most defects is the **round trip**: read a graph,
paste the text into an empty Blueprint, read again, compare. Reader and writer
are mirrors, so when the text does not close, one of them is lying and the diff
says which.

`Tests/run_tests.py` does it without an interface, in ~20 seconds:

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" --% "C:/Unreal Projects/BossRush/BossRush.uproject" -run=pythonscript -script="C:/Unreal Projects/BossRush/Plugins/NodeScribe/Tests/run_tests.py" -unattended -nopause -nosplash -NullRHI
```

The report goes to `Saved/NodeScribe/tests.txt`; a failure exits non-zero.
Two traps, both from the space in "Unreal Projects": `--%` makes PowerShell
pass the rest literally (it also swallows anything after it, `> $null`
included), and the paths use **forward slashes**, since the Engine eats a
backslash inside quotes (`Tests\run` becomes `Testsun`).

Each case is written into an empty Blueprint, read (T1), T1 is written into a
second one and read again (T2); a mirror gives T1 == T2. No hand-kept expected
answer, so nothing goes stale when the format changes. It does not cover
readability -- `True:` on a node that is not a Branch closes the fixed point
just the same --, so someone still has to read a real graph now and then.

To move into the Engine's suite one day, the template is
`ToolsetRegistry/Source/ToolsetRegistry/Private/Tests/ToolsetLibraryTest.cpp`
(`BEGIN_DEFINE_SPEC`).

## The missing vocabulary

`Saved/NodeScribe/vocabulary.txt` gets one line per **written name that
matched nothing**: node not found, ambiguous node, nonexistent input pin,
nonexistent output in a `$x.Pin`.

```
node       Make Coffee
ambiguous  Apply Settings
pin        In Vec  in  Break Vector
output     Cup  in  $pc
```

It is not an error log: the error already comes back in the call's return, in
the Message Log and as a red comment in the graph. The file answers what those
cannot -- **which aliases the catalog is missing**, by frequency:

```bash
sort vocabulary.txt | uniq -c | sort -rn
```

So a line has no date (a repeat must come out identical to be counted), and
what has a known cause -- incompatible type, empty pin, duplicate event --
stays out.

## Traps that already bit

They compile and fail at runtime.

- **`UEdGraphSchema_K2()` on the stack brings the editor down.** A UObject's
  constructor calls `FObjectInitializer::Get()`, valid only inside a UObject
  constructor. Use `GetDefault<UEdGraphSchema_K2>()`. It happened twice (in
  `cac90d8`, and writing `NodeScribePropertyText`); check it wherever
  `ConvertPropertyToPinType` shows up.

## What must not break

A change that contradicts these is wrong even if it works:

1. **When it cannot decide safely, it does not decide.** Ambiguous becomes a
   red comment; an unspecified asset becomes an empty pin. A guessed plausible
   node compiles, runs and is wrong.
2. **Reader and writer are mirrors.** Teaching a new type changes both, in the
   same commit.
3. **Readability is not traded for bytes.** The text is ~2% of the cost;
   shortening it saves ~1% and removes the only defence against the AI having
   written something else.
4. **A new tool costs a choice.** Discovery is lazy, so the cost is not the
   prompt; it is that similar doors make the assistant pick wrong and spend a
   turn finding out. Prefer a new parameter on an existing tool.

## Growing without getting heavier

One front at a time, and mature before the next; on every front, reading
first, on its own -- reading breaks no asset, so it can be tested freely while
the format still changes its mind, and it is where almost all the savings are.

What an assistant pays grows with the plugin unless each new piece lands where
only the tasks that need it read it:

- **A new kind of asset** is a new shape of `read_object` / `write_object`,
  not a new tool (D-016), and its text gets a topic in `Docs/format/`, named
  in the core's *Topics* table. The core only takes rules every graph uses.
- **A new family of nodes** is born in its own reader/builder pair of files,
  as `NodeScribeAnimGraph` did.
- **History and proposals stay out of the README and of this file:** what a
  version changed goes to [history.md](history.md), an idea to
  [ideas.md](ideas.md).

The suite's `token_budget` case holds the line: it fails when the core, a
topic, the tools' descriptions, the number of tools, or a reading against the
text that made it passes its ceiling (`BUDGET`, at the top of
`Tests/run_tests.py`), and prints the numbers when it passes. Raising a ceiling
is allowed; doing it without saying why in the commit is not.

## Proposing a new tool

It goes to [ideas.md](ideas.md) before becoming code, and the proposal answers
four things:

- **Where does the token leak today?** With numbers, from the native tool's
  code, not from memory.
- **What is the compressed format?** One line per item, the name shown on
  screen, readable by people.
- **How much surface does it cost?** How many new MCP tools, and why not fewer.
- **What does it let you turn off?** If nothing, it does not save -- it adds.

## How the MCP setup works

`NodeScribeMcpSetup` runs one second after launch and fixes the two links
that fail silently: the Engine's MCP server starts off (`bAutoStartServer`),
and without `.mcp.json` the assistant never tries to connect. Nothing links
against Epic's plugin -- the settings are read by reflection and the server
starts through a console command --, so without ModelContextProtocol both
steps fail quietly, as `Optional` promises.

`.mcp.json` is treated as someone else's file: read, changed and rewritten
keeping what is there. Another server's entry is untouched, and an entry
already pointing at the right address counts, whatever its name. Invalid JSON,
or an entry called `unreal-mcp` pointing elsewhere (a tunnel, another editor),
make it back off and explain in the Output Log; **Tools → NodeScribe →
Configure project MCP** overrides.

A plugin ini cannot do this: Unreal injects `<Plugin>/Config` into the
project's hierarchy only by matching a branch it already knows, and the result
does not reach `EditorPerProjectUserSettings`. Measured with both possible
names.
