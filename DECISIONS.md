# Decisions

<!-- project-docs: only what holds now. Next ID: D-028 · Next fact: F-007 -->

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

### D-005 Moves and reorders follow the text; a class change is refused
Decision: an existing widget written under another parent moves there, at the place its neighbours in the text give it; existing siblings written in another order take, in that order, the places they had among themselves, and a sibling the text does not name keeps its own. Every move and reorder is named in the return. The root does not move, nothing moves into its own children, moving into or out of a named slot is refused, and so is changing a widget's class.
Why: the text is where it is written, as for a new widget; a reading edited and pasted back should do what the edit says. The refusal existed so a slip could not move a widget silently -- the note in the return is what keeps it from being silent.
Origin: R-005
Not chosen: a `move X under Y` keyword -- the same intent written twice, and a pasted reading would still be refused.
Affects: NodeScribeWidgetTree.cpp, Docs/format/designer.md

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

### D-009 Code and docs in English; the main README and ROADMAP.md in Portuguese
Decision: identifiers, comments, messages, FORMAT and these files are English. The main README is `README.pt-BR.md`, in Portuguese, and `README.md` is its English translation, kept with the same content; the ROADMAP items are Portuguese.
Why: the repository is public and English, but the user reads the README and the roadmap's test steps to test, in Portuguese. A change to the README is made in `README.pt-BR.md` and translated into `README.md` in the same commit.
Origin: user (README in Portuguese asked for on 2026-10-26)

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
Why: every tool's description is read whenever the toolset is looked up, and similar doors make the assistant choose wrong (`Docs/development.md`, *What must not break*, 4).
Origin: user asked for the plugin to stay token efficient as it grows; agent's proposal
Affects: R-004, R-007, Tests/run_tests.py

### D-017 The format is a core plus topics
Decision: `Docs/FORMAT.md` holds the rules every graph uses and every keyword; AnimGraph, Behavior Tree, Designer and the details of a reading live in `Docs/format/<topic>.md`. `get_format_docs(topic)` returns one; empty returns the core, and an unknown name answers with the list.
Why: the whole file was ~9k tokens for every task, and every new kind of asset made it longer for tasks that never touch it. The core is now ~4.7k.
Origin: R-008
Affects: Docs/FORMAT.md, Docs/format/, NodeScribeLibrary.cpp, graph.py

### D-018 The README holds the current state; history and ideas go to Docs/
Decision: what each version changed goes to `Docs/history.md`, proposals to `Docs/ideas.md`, and what a developer or agent needs to work on the plugin to `Docs/development.md`, in English only; the README is for a newcomer and its *Status* is the present only.
Why: an agent reads *Status* and *Development* before touching the code. *Status* had grown to 27 KB of changelog, paid again in every development session.
Origin: user asked for the plugin to stay token efficient as it grows; agent's proposal
Affects: README.md, README.pt-BR.md, Docs/history.md, Docs/ideas.md, Docs/development.md

### D-019 An event already in the graph with no wire is reused, not refused
Decision: `event X` that finds an existing X with no link on any pin takes that node, moves it next to the new chain and does not count it as created; with any link, the duplicate guard refuses as before. A second `event X` in the same text is still a duplicate.
Why: the reader leaves out exactly those events, so a reading with no `BeginPlay` could not take one, and the text gave no reason. Hit by a fresh agent on `BP_TesteGolem`, which had to delete the stub with the native toolset. An unlinked event carries no behaviour, so reusing it decides nothing; linking it turns a disabled stub into a real node, as on screen.
Origin: user, from the fresh agent's R-008 check
Not chosen: deleting the stub and creating a new event, as the editor's menu does -- a Custom Event with no wire may still hold parameters someone declared.
Affects: NodeScribeBuilder.cpp (ReuseEmptyEventOrRefuse), Docs/FORMAT.md (Events)

### D-020 Components are created by a word of their own, through the panel's subsystem
Decision: `component Name : Class [in Parent]` adds a component and `delete component Name` removes one, through `USubobjectDataSubsystem`; an existing name with the same class is an anchor. The header names every component with its parent (`# components:`).
Why: a `Name : Class` block that names nothing is also what a typo looks like, and creating a component for a typo is a guess. The subsystem brings the panel's rules -- root attachment, valid names, inherited components not deletable, only spawnable classes.
Origin: R-003
Affects: NodeScribeComponents.cpp, NodeScribeObjectWriter.cpp, NodeScribeObjectReader.cpp, Docs/format/sheet.md

### D-021 A block of instanced objects is the array's whole value
Decision: an array of instanced objects reads as a block, one element per line by the Details panel's name, its changed properties under it. Written back, the block replaces the array: same-class elements are kept and changed, missing ones leave (named in the return). The lines under an element are applied only once it is in the array.
Why: the Engine's export held both kinds of quote and the sheet could not hold it at all; an element has no name, so a list of changes would have nothing to anchor on. Applying after the array is set is what lets an owner recompute -- a Gameplay Effect's tag component recomputed nothing otherwise.
Origin: R-007
Affects: NodeScribeInstanced.cpp, NodeScribeObjectReader.cpp, NodeScribeObjectWriter.cpp, Docs/format/sheet.md

### D-022 An Input Mapping Context is a `mappings` section, by reflection
Decision: `Key -> Action` lines after a `mappings` line, triggers and modifiers indented under each; a list of changes, where a mapping's indented lines are its whole set of triggers and modifiers, all or nothing for that mapping. Enhanced Input is reached by reflection, not linked.
Why: the same shape as the Designer section (D-001); NodeScribe should not need Enhanced Input to load.
Origin: R-004
Affects: NodeScribeInputMapping.cpp, Docs/format/input-mapping.md

### D-023 FORMAT.md says the rules; why they are so lives in Docs/design.md
Decision: the core keeps rules and examples; the reasons moved to `Docs/design.md`, which nothing serves through `get_format_docs`.
Why: every task reads the core. Measured with two fresh agents writing the same ten typical graphs, one from each version: 8 of 10 pasted clean before and after, with the same two failures; the core went from 20,707 to 16,528 characters.
Origin: R-009
Affects: Docs/FORMAT.md, Docs/design.md

### D-024 The project's graphs round-trip in the suite, through deleted copies
Decision: `project_graphs_round_trip` duplicates each Blueprint under `/Game` into the scratch folder, refills each graph with the original's reading and compares; a difference the reading does not warn about fails, and each copy is deleted, with a garbage collection, before the next.
Why: the silent differences were being found by tripping on them. Copies left in memory are second classes with the same functions and turned every call ambiguous.
Origin: R-010
Not chosen: `/Game/BossRush/Testes/` as the roadmap item said -- the suite's own scratch folder is never saved either, and keeps the plugin's suite independent of BossRush.
Affects: Tests/run_tests.py, Docs/development.md

### D-025 Inserting into an existing chain is `after <a line of the reading>:`
Decision: the node is found by re-reading the graph and comparing the `after` line with each execution node's line; the block goes in after it and what followed is relinked to the block's last node. The reading's `$names` resolve inside the block. An `after` matching no node or several refuses the whole write; with `replace` it is refused.
Why: the line is what the assistant has in front of it, and a reading is deterministic. Reasons per rule in `Docs/design.md` > Inserting into a chain that exists.
Origin: R-015
Not chosen: making `replace` keep reconvergences -- the format only describes trees, so a graph that reconverges could never be rewritten whole; a `before` keyword, since `after` the node upstream covers it.
Affects: NodeScribeParser, NodeScribeBuilder, NodeScribeReader (ExecLines, NodeNames), Docs/FORMAT.md

### D-026 A Custom Event's parameters are `(Name : Type)` on the event line
Decision: `event X (A : Float, B : Actor)` declares a Custom Event's parameters, with the types of `variable`; the reading writes them only for Custom Events. An event of the parent class, of an implemented interface or of a dispatcher takes its signature from there: a declared list that differs is a warning. An existing Custom Event with other parameters is refused, not changed. By-reference parameters, replication and Call in Editor have no form yet and are warned about on reading.
Why: an event has no inputs, so its parentheses were free; `:` already pairs name and type in `variable`. Reasons per rule in `Docs/design.md` > Events.
Origin: R-016
Not chosen: indented `param` lines under the event -- indentation already means an execution block; writing the signature of interface and override events too -- it is the class's, and repeating it only creates a way to disagree.
Affects: NodeScribeParser (EventParams), NodeScribeBuilder, NodeScribeReader, Docs/FORMAT.md

### D-027 An interface function is a message, and its implementations are not candidates
Decision: the catalog skips a class's implementation of an interface function unless the line names the class (`BossRushPlayer.Obter Facção`); the interface's function is created as `UK2Node_Message`. A plain call on an interface reference reads with a warning: it comes back as the message.
Why: the implementation tied with its interface and made every call ambiguous; as a plain call the Target wants an interface reference, and an Actor's wire was dropped. The message is the editor menu's form and takes any object.
Origin: R-019
Not chosen: recreating the plain call when the Target is an interface reference -- the text cannot tell the Target's type without guessing, and both do the same at runtime.
Affects: NodeScribeCatalog, NodeScribeBuilder, NodeScribeReader, Docs/FORMAT.md

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

### F-005 Default Starting Data only initialises attribute sets
Fact: `UAbilitySystemComponent::DefaultStartingData` is `TArray<FAttributeDefaults>` -- an AttributeSet class and a DataTable -- and grants no ability. In BossRush, `BP_Golem` has `ActionAttributes` from `DT_Elisande`, and no Blueprint of the Golem calls Give Ability (only `Lobisomem_Zoo1` and `BossRushPlayer` do).
Source: Engine `AbilitySystemComponent.h:209`, `GameplayAbilityTypes.h:360`; `read_object` on `BP_Golem`; a search of the project's assets for `GiveAbility`
Verified: 2026-10-26

### F-006 Spawn Actor from Class needs its pins before PostPlacedNewNode
Fact: in 5.8 `UK2Node_SpawnActorFromClass::PostPlacedNewNode` calls `FindPinChecked` on the Scale Method pin; `UBlueprintNodeSpawner` allocates pins first, the generic schema action does not.
Source: Engine `K2Node_SpawnActorFromClass.cpp:358`, `BlueprintNodeSpawner.cpp:347`; the crash in `project_graphs_round_trip`
Verified: 2026-10-26
