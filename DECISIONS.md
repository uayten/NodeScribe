# Decisions

<!-- project-docs: only what holds now. Next ID: D-019 · Next fact: F-005 -->

## Decisions

### D-001 The Designer text is a list of changes, not the whole tree
Decision: in `write_object`'s `designer` section, an existing widget's line is an anchor and changes nothing by itself; a new name creates a widget; a widget the text omits stays.
Why: a Designer has dozens of widgets. Whole-tree text would make "add a row" cost the whole screen, and every forgotten widget would be deleted.
Origin: agent's choice, passed the other session's test
Not chosen: whole tree, as the Behavior Tree does - a BT is small and only exists as a shape; a Designer is neither.
Affects: Docs/FORMAT.md (Widget Designer), NodeScribeWidgetTree.cpp

### D-002 Is Variable never changes by omission
Decision: on a new widget, the word `variable` is the whole answer; on an existing one, a missing word changes nothing, and unticking is `Is Variable = false`.
Why: an anchor written without the word would untick it and break every graph node that reads the widget.
Origin: agent's choice, passed the other session's test

### D-003 Designer structure is all or nothing; properties go line by line
Decision: every widget line is resolved before the tree is touched, and one structural error changes nothing; property lines then go through the sheet's writer (`WriteProperties`) one by one.
Why: half a row is a row in the wrong place; a font size that did not parse is one line to fix.
Origin: agent's choice

### D-004 The Designer is edited through the editor's own operations
Decision: create, add, rename, delete and Is Variable go through `FWidgetBlueprintOperationUtils`, the same as the Hierarchy panel.
Why: a rename then takes the graph's references along, and a widget is created as the Palette creates it (`TextBlock` gets "Text Block").
Origin: agent's choice

### D-005 Moving, reordering and changing a widget's class are refused
Decision: an indented existing widget under a different parent, existing siblings out of order, or a different class are errors until R-005.
Why: the anchor rule means a line can name a widget anywhere; without the refusal, a slip would move it silently.
Origin: agent's choice
Affects: R-005

### D-006 A typo is answered, never resolved
Decision: near misses (`TextBlok`, `Txet`) go into the error's "Similar:" list; they never resolve by themselves.
Why: a typo that resolved itself would be the plugin choosing - the guessed plausible node this project refuses.
Origin: agent's choice, asked for by the other session's test

### D-007 The arguments and the Target decide what a name means
Decision: `Get X` / `Set X` becomes a variable node only when every named argument is a pin that node has; otherwise it is a function. When a line has a `Target`, its class breaks catalog ties, and the Blueprint's own class only gets its bonus when the Target is one of its kind.
Why: `Set Visibility (Target = $Row, In Visibility = Visible)` is the reader's line for `UWidget::SetVisibility`; taken as a Widget Blueprint's own `Visibility` property, or left as a tie between four classes, it did not paste back.
Origin: agent's choice, from the other session's test

### D-008 `Show Pin for Properties` is presentation, not a node setting
Decision: the reader and builder skip `ShowPinForProperties` on every node class.
Why: every struct Break fills it, so it always "changed"; as a setting it made `replace` refuse a graph NodeScribe had just written.
Origin: agent's choice, from the other session's test

### D-009 Code and docs in English; ROADMAP.md in Portuguese
Decision: identifiers, comments, messages, README, FORMAT and these files are English; the ROADMAP items are Portuguese.
Why: the repository is public and English; the roadmap's test steps are what the user reads to test.
Origin: user

### D-010 Flow, not execution
Decision: the builder's walk uses `IsFlowPin` / `FindFlowInput` / `GetFlowOutputs`, which answer an execution pin in the EventGraph and a pose pin in the AnimGraph.
Why: one walk for both kinds of graph.
Origin: user, AnimGraph round
Affects: NodeScribeBuilder.cpp

### D-011 In the AnimGraph, indentation opens a pose input
Decision: each node of an indented block links over the previous one on the parent's pose pin, so the last one remains.
Why: a pose flows into its consumer; an output-shaped block would read upside down.
Origin: user, AnimGraph round

### D-012 Each graph uses its own schema
Decision: the builder asks the graph for its schema instead of using the generic `UEdGraphSchema_K2`.
Why: AnimGraph pose pins only exist in the animation schema. It changed every graph, so it is where to look if the EventGraph regresses.
Origin: user, AnimGraph round

### D-013 `create_asset` options go into the factory, before creating
Decision: `options` is a sheet written into the factory; a factory option that does not apply aborts the creation.
Why: on the finished asset the skeleton of an AnimBlueprint or BlendSpace is read-only.
Origin: user, AnimGraph round

### D-014 `replace` refuses what the text cannot say; `clear_graph` is not a forced mode
Decision: `write_graph` with `replace` refuses a graph whose reading loses something; `clear_graph` returns the transcribed graph with the reading's warnings.
Why: replacing a graph from a lossy reading deletes what the text never mentioned.
Origin: user, AnimGraph round

### D-015 `save_all_and_quit` queues CLOSE_SLATE_MAINFRAME, never QUIT_EDITOR
Decision: closing goes through `IMainFrameModule::RequestCloseEditor()`.
Why: `QUIT_EDITOR` skips the Slate shutdown, leaves asset editors alive past the main window, and crashed in `AnimationBlueprintEditor`. The engine itself warns against it in `EditorServer.cpp`.
Origin: user, AnimGraph round

### D-016 A new kind of asset is a shape of read_object / write_object, not a tool
Decision: Blackboard, Behavior Tree and Designer already ride on the sheet's two tools; the next kinds (Input Mapping Context, Gameplay Effect components) do the same. `write_blendspace` stays as it is. The suite's `token_budget` caps the number of tools.
Why: every tool's description is read whenever the toolset is looked up, and similar doors make the assistant choose wrong (README, *What must not break*, 4).
Origin: user asked for the plugin to stay token efficient as it grows; agent's proposal
Affects: R-004, R-007, Tests/run_tests.py

### D-017 The format is a core plus topics
Decision: `Docs/FORMAT.md` holds the rules every graph uses and every keyword; AnimGraph, Behavior Tree, Designer and the details of a reading live in `Docs/format/<topic>.md`. `get_format_docs(topic)` returns one; empty returns the core, and an unknown name answers with the list.
Why: the whole file was ~9k tokens for every task, and every new kind of asset made it longer for tasks that never touch it. The core is now ~4.7k.
Origin: R-008
Affects: Docs/FORMAT.md, Docs/format/, NodeScribeLibrary.cpp, graph.py

### D-018 The README holds the current state; history and ideas go to Docs/
Decision: what each version changed goes to `Docs/history.md`, proposals to `Docs/ideas.md`; the README's *Status* is the present only.
Why: an agent reads *Status* and *Development* before touching the code. *Status* had grown to 27 KB of changelog, paid again in every development session.
Origin: user asked for the plugin to stay token efficient as it grows; agent's proposal
Affects: README.md, Docs/history.md, Docs/ideas.md

## Verified facts

### F-001 A plain Blueprint factory makes no Designer
Fact: `UBlueprintFactory` accepts `UserWidget` as a parent and gives a plain Blueprint with no WidgetTree; a Widget Blueprint needs `UWidgetBlueprintFactory`.
Source: `create_asset` before and after the fix; `designer_writes_rows` in the suite
Verified: 2026-10-25

### F-002 Widget Blueprint templates take the Blueprint's asset data, not the class's
Fact: `FWidgetTemplateBlueprintClass::Supports` requires the asset class to be `UWidgetBlueprint`; `FAssetData` of the generated class falls through as unsupported.
Source: Engine/Source/Editor/UMGEditor/Private/Templates/WidgetTemplateBlueprintClass.cpp:121
Verified: 2026-10-25

### F-003 Append and the math operators are their own node class
Fact: functions with `CommutativeAssociativeBinaryOperator` metadata are placed as `UK2Node_CommutativeAssociativeBinaryOperator` (the "Add pin" button); as a plain `UK2Node_CallFunction` they keep two inputs.
Source: `append_grows_pins` failing and then passing in the suite
Verified: 2026-10-26

### F-004 The transition rule's schema is not an animation schema
Fact: `UAnimationTransitionSchema` derives from `UEdGraphSchema_K2`, not from `UAnimationGraphSchema`.
Source: Engine header; the AnimGraph round's empty-transition bug
Verified: AnimGraph round
