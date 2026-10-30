"""NodeScribe round-trip tests, without opening the interface.

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript
        -script="<plugin>/Tests/run_tests.py" -unattended -nopause -NullRHI

The report goes to `Saved/NodeScribe/tests.txt` and the summary to the log. If
anything fails, the process exits with a non-zero code.

## What it tests, and why this way

**Fixed point.** Each case is written into an empty Blueprint, read (T1), T1
is written into a *second* empty Blueprint, and that one is read (T2). If
reader and writer are mirrors, T1 and T2 are identical. When they are not, one
of the two is lying, and the diff says which.

It is the test that needs no hand-kept expected answer: nothing to update when
the format changes, and no case that passes because it went stale together
with the code.

It catches what the August 2026 review caught: node names with parentheses,
a Break with a single visible field, a variable type that comes back
different. It does not catch what is only readability -- `True:` on a node
that is not a Branch comes back as `True:` and closes the fixed point just the
same. For that someone still has to look.

**Only the body goes into the comparison.** A line starting with `#` is a
header, an anchor or a diagnostic -- none of them is graph. And some differ on
purpose: an orphan node becomes a note in T1, and does not exist in the second
Blueprint to become a note in T2.

**Expected warning.** Where the format *cannot* cope, the deal is to fail out
loud. Those cases declare the piece of the warning that has to show up, and
its absence fails the case.
"""

import difflib
import os
import traceback

import unreal

# Test assets are born here and never saved: the commandlet exits without
# saving. Running with the editor open, do not save afterwards -- it is scratch.
DESTINATION = '/Game/NodeScribeTests'

# What an assistant pays to use the plugin, in characters (~4 per token).
# These are ceilings, not targets: every run prints the real numbers. Raising
# one is a decision -- say why in the commit -- because nothing else notices the
# spec, a tool's description or a reading growing until every session pays.
BUDGET = {
    # get_format_docs(): read by every task that writes or reads text.
    'core': 20000,
    # Each topic, read only when a task touches it.
    'topic': 10000,
    # Every tool's description, read whole when the assistant looks the
    # toolset up -- once in every session that uses the plugin.
    'descriptions': 11500,
    # A new tool costs a choice (Docs/development.md, *What must not break*); a new kind of
    # asset goes into read_object / write_object instead (DECISIONS, D-016).
    'tools': 11,
    # A reading's graph lines against the ones that made it, worst case.
    'reading_ratio': 2.5,
}

# Filled by round_trip: case name -> (graph characters written, read back).
READING_SIZES = {}


CASES = [
    {
        'name': 'collections',
        'text': '''
variable Mirrors : Map of Int64 to Actor
variable Marked : Set of Name
variable Enemies : Array of Actor
variable Counter : Integer

event Test
Print String (In String = "collections")
''',
    },
    {
        # R-016: without the parameter list the event came back with no outputs
        # and both `$hit.X` failed. The call above the event needs them in the
        # skeleton before its line.
        'name': 'custom_event_parameters',
        'text': '''
event Test
Scored (Points = 3, Who = "golem")

hit = event Scored (Points : Integer, Who : String, Targets : Array of Actor)
text = Append (A = $hit.Who, B = $hit.Points (Integer -> String))
Print String (In String = $text)
''',
    },
    {
        # A class event's signature is the class's: a list that differs is said.
        'name': 'override_event_ignores_declared_parameters',
        'text': '''
event BeginPlay (Damage : Float)
Print String (In String = "begin")
''',
        'expects_warning': ['takes its parameters from where it is declared'],
    },
    {
        'name': 'types',
        'text': '''
variable A : Vector
variable B : Timer Handle
variable C : Transform
variable D : Rotator
variable E : Actor
variable F : Pawn
variable G : Linear Color
variable H : Array of Vector
variable I : Map of Name to Vector

event Test
Print String (In String = "types")
''',
    },
    {
        'name': 'implicit_conversion',
        'text': '''
variable Counter : Integer

event Test
text = Append (A = "n = ", B = $Counter (Integer -> String))
Print String (In String = $text)
''',
    },
    {
        'name': 'branch_and_break',
        'text': '''
variable Position : Vector
variable Active : Boolean

event Test
Branch (Condition = $Active)
  true:
    p = Break Vector ($Position)
    Print String (In String = "in the air", Duration = 3.5)
  false:
    Print String (In String = "on the ground")
''',
    },
    {
        'name': 'macro_and_loop',
        'text': '''
variable Enemies : Array of Actor

event Test
For Each Loop (Array = $Enemies)
  loop body:
    Print String (In String = "an enemy")
''',
    },
    {
        'name': 'async_action',
        'text': '''
event Test
Async Load Game from Slot (Slot Name = "test")
  Completed:
    Print String (In String = "loaded")
''',
    },
    {
        'name': 'reused_custom_event',
        'text': '''
event Refresh Screen
Print String (In String = "refreshed")

event Test
Print String (In String = "about to refresh")
Refresh Screen
''',
    },
    {
        # Spawn Actor from Class looks its pins up while being placed; built in
        # the generic schema's order, it brought the editor down.
        'name': 'spawn_actor',
        'text': '''
event Test
transform = Make Transform
spawned = Spawn Actor from Class (Class = /Script/Engine.StaticMeshActor, Spawn Transform = $transform)
Destroy Actor (Target = $spawned)
''',
    },
    {
        # A static function whose parameter is literally called `Target`. The
        # reader writes `Target = $x`, and the builder used to map `Target` to
        # the self pin only, answering "no pin `Target`. Input pins: Target".
        'name': 'parameter_called_target',
        'text': '''
event Test
me = Self
bb = Get Blackboard (Target = $me)
n = KismetSystemLibrary.GetDisplayName (Object = $bb)
Print String (In String = $n)
''',
    },
    {
        # The chain after an impure Cast goes on under a label. It used to be
        # reported as coming back without one.
        'name': 'cast_with_continuation',
        'text': '''
variable Other : Actor

event Test
c = Cast to Character (Object = $Other)
  then:
    Print String (In String = "a character")
  Cast Failed:
    Print String (In String = "not a character")
''',
    },
    {
        # `Switch Has Authority` is a standard macro. A bare `Switch ` prefix
        # used to be read as `Switch on <enum>` and die as "enum not found".
        'name': 'switch_has_authority',
        'text': '''
event Test
Switch Has Authority
  Authority:
    Print String (In String = "server")
  Remote:
    Print String (In String = "client")
''',
    },
    {
        # `$Other` is an implicit Get of a variable, which is how the reader
        # writes it. The dispatcher's class used to be looked up only among
        # named lines, so this died as "no node called `Unbind OnDestroyed`".
        'name': 'dispatcher_on_variable',
        'text': '''
variable Other : Actor

event Test
done = event Done
Bind OnDestroyed (Target = $Other, Event = $done)
Unbind OnDestroyed (Target = $Other, Event = $done)
''',
    },
    {
        # The reader writes each chain under its event, so the bound event can
        # come after the line that binds it. The name used to become a
        # missing-variable Get, and the event line then "defined it again".
        'name': 'event_bound_before_its_line',
        'text': '''
variable Other : Actor

event Test
Bind OnDestroyed (Target = $Other, Event = $done)

done = event Done
Print String (In String = "destroyed")
''',
        'forbids_warning': ['does not exist', 'defined again'],
    },
    {
        # The first `$v.X` splits the struct pin; the second one used to find
        # it already split and give up -- only the raw
        # `MinimalViewInfo_bConstrainAspectRatio` resolved, not the name on screen.
        'name': 'second_field_of_split_struct',
        'text': '''
event Test
v = Make MinimalViewInfo (FOV = 75.0)
Print String (In String = $v.FOV)
Print String (In String = $v.Constrain Aspect Ratio)
''',
        'forbids_warning': ['has no output'],
    },
    {
        # The reader writes an Append with five inputs as A..E; the node is
        # born with two, and C..E used to be refused instead of added.
        'name': 'append_grows_pins',
        'text': '''
event Test
s = Append (A = "a", B = "b", C = "c", D = "d", E = "e")
Print String (In String = $s)
''',
    },
    {
        # `Set Visibility` is both a function of Widget and the setter of its
        # `Visibility` property. The arguments say which: `In Visibility` is the
        # function's pin. Taken as the property, the line died on that pin.
        'name': 'function_over_property_setter',
        'text': '''
variable Panel : Widget

event Test
Set Visibility (Target = $Panel, In Visibility = Hidden)
''',
    },
    {
        'name': 'declared_hole',
        'text': '''
event Test
Spawn Sound 2D (Sound = ?)
''',
        'expects_warning': ['left empty, waiting'],
    },
]


# ---------------------------------------------------------------------------


def body(text):
    """Only the graph lines: no header, anchor or diagnostic."""
    lines = []
    for line in text.splitlines():
        trimmed = line.strip()
        if not trimmed or trimmed.startswith('#'):
            continue
        lines.append(line.rstrip())
    return lines


def new_blueprint(name):
    path = '%s/%s' % (DESTINATION, name)
    # `options` is mandatory in the UFUNCTION's signature -- Python does not
    # have the default value C++ has. Without the empty string here, the whole
    # suite dies on its first line with `TypeError: create_asset() required
    # argument 'options' (pos 3) not found`, and has not run since `options`
    # was created.
    report = unreal.NodeScribeLibrary.create_asset(path, 'Actor', '')
    if report.startswith('[error]'):
        return None, report

    bp = unreal.load_asset(path)
    if not bp:
        return None, 'could not load %s' % path

    graph = unreal.load_object(bp, 'EventGraph')
    if not graph:
        return None, 'could not find the EventGraph of %s' % path

    return graph, ''


def round_trip(case):
    """Returns (passed, [report lines])."""
    name = case['name']
    report = []

    graph_a, error = new_blueprint('NS_%s_a' % name)
    if not graph_a:
        return False, ['could not prepare the first Blueprint: ' + error]

    written = unreal.NodeScribeLibrary.write_graph(graph_a, case['text'])
    t1 = unreal.NodeScribeLibrary.read_graph(graph_a)
    # Graph lines only: the header and the warnings are a fixed cost, and on a
    # three-line case they would be the whole ratio.
    READING_SIZES[name] = (len('\n'.join(body(case['text']))), len('\n'.join(body(t1))))

    if '[error]' in written:
        report.append('the write complained:')
        report.extend('  ' + l for l in written.splitlines())
        return False, report

    for expected in case.get('expects_warning', []):
        if expected not in t1 and expected not in written:
            report.append('the expected warning is missing: %r' % expected)
            report.append('the plugin has to fail out loud here.')
            return False, report

    graph_b, error = new_blueprint('NS_%s_b' % name)
    if not graph_b:
        return False, ['could not prepare the second Blueprint: ' + error]

    written_b = unreal.NodeScribeLibrary.write_graph(graph_b, t1)
    t2 = unreal.NodeScribeLibrary.read_graph(graph_b)

    # Checked on both writes: the second one pastes the reader's own order.
    for forbidden in case.get('forbids_warning', []):
        for label, output in (('first', written), ('second', written_b)):
            if forbidden in output:
                report.append('the %s write said %r, and should not:' % (label, forbidden))
                report.extend('  ' + l for l in output.splitlines())
                return False, report

    a, b = body(t1), body(t2)
    if a == b:
        return True, []

    report.append('the second reading did not match the first:')
    report.extend(difflib.unified_diff(a, b, 'T1 (reading of what I wrote)',
                                       'T2 (reading of T1 pasted)', lineterm='', n=2))
    report.append('')
    report.append('--- whole T1 ---')
    report.extend(t1.splitlines())
    return False, report


def replace_swaps_the_graph():
    """Replacing on a graph that reads back clean swaps the content, it does not pile up."""
    graph, error = new_blueprint('NS_replace_clean')
    if not graph:
        return False, [error]

    unreal.NodeScribeLibrary.write_graph(graph, 'event Before\nPrint String (In String = "before")\n')
    report = unreal.NodeScribeLibrary.write_graph(
        graph, 'event After\nPrint String (In String = "after")\n', True)

    text = unreal.NodeScribeLibrary.read_graph(graph)

    problems = []
    if 'deleted' not in report:
        problems.append('the replacement deleted nothing: %s' % report)
    if 'event Before' in text:
        problems.append('the old event is still in the graph')
    if 'event After' not in text:
        problems.append('the new event did not go in')

    if problems:
        problems.append('')
        problems.extend(text.splitlines())
        return False, problems

    return True, []


def empty_event_is_reused():
    """A new Blueprint's unlinked BeginPlay, hidden by the reader, takes `event BeginPlay`."""
    graph, error = new_blueprint('NS_empty_event')
    if not graph:
        return False, [error]

    problems = []

    # The factory stub is there, and the reading leaves it out: that is the
    # state in which refusing `event BeginPlay` had no visible reason.
    before = unreal.NodeScribeLibrary.read_graph(graph)
    if 'event BeginPlay' in before:
        problems.append('the reading shows the unlinked stub; this case no longer tests the hidden one')

    first = unreal.NodeScribeLibrary.write_graph(
        graph, 'event BeginPlay\nPrint String (In String = "ready")\n')
    if '[error]' in first:
        problems.append('the empty BeginPlay was refused: %s' % first)

    text = unreal.NodeScribeLibrary.read_graph(graph)
    if text.count('event BeginPlay') != 1:
        problems.append('expected exactly one `event BeginPlay` in the reading')

    # Now it carries a chain, and a second one is a real duplicate.
    second = unreal.NodeScribeLibrary.write_graph(
        graph, 'event BeginPlay\nPrint String (In String = "again")\n')
    if 'already exists' not in second:
        problems.append('a BeginPlay with a wire was not refused: %s' % second)

    if problems:
        problems.append('')
        problems.extend(text.splitlines())
        return False, problems

    return True, []


def replace_takes_own_break():
    """A generic struct Break that NodeScribe wrote can be replaced with its own reading.

    Every Break fills `Show Pin for Properties`, and the reader used to call it
    an option it could not write -- so `replace` refused the graph it had just
    written, as one with things the text cannot say.
    """
    graph, error = new_blueprint('NS_replace_break')
    if not graph:
        return False, [error]

    written = unreal.NodeScribeLibrary.write_graph(graph, '''
variable View : MinimalViewInfo

event Test
v = Break MinimalViewInfo ($View)
Print String (In String = $v.FOV)
''')
    if '[error]' in written:
        return False, ['the write complained:'] + written.splitlines()

    text = unreal.NodeScribeLibrary.read_graph(graph)
    report = unreal.NodeScribeLibrary.write_graph(graph, text, True)

    if '[error]' in report or 'Show Pin' in text:
        return False, ['replacing with its own reading was refused:'] + report.splitlines() \
            + ['', '--- reading ---'] + text.splitlines()

    return True, []


def replace_refuses_lossy_graph():
    """A graph with a loose data node cannot be replaced: it would not come back."""
    graph, error = new_blueprint('NS_replace_refuses')
    if not graph:
        return False, [error]

    # `pc` feeds nobody: it is an orphan, it vanishes from the text, and it
    # would not come back.
    unreal.NodeScribeLibrary.write_graph(
        graph, 'event Before\nPrint String (In String = "before")\npc = Get Player Controller\n')

    before = unreal.NodeScribeLibrary.read_graph(graph)
    report = unreal.NodeScribeLibrary.write_graph(graph, 'event After\n', True)
    after = unreal.NodeScribeLibrary.read_graph(graph)

    problems = []
    if '[error]' not in report:
        problems.append('the replacement should have been refused, and the return was: %s' % report)
    if 'event After' in after:
        problems.append('refused and wrote anyway')
    if body(before) != body(after):
        problems.append('refused and touched the graph anyway')

    if problems:
        problems.append('')
        problems.append('--- return ---')
        problems.extend(report.splitlines())
        return False, problems

    return True, []


def sheet_round_trip_changes_nothing():
    """Pasting a whole sheet back applies without a single error.

    The sheet's header and its `~ N properties at default` footer are reader
    output. The footer used to be parsed as a block header, so the most
    basic round trip -- read, paste back -- ended in an error on its last line.
    """
    graph, error = new_blueprint('NS_sheet')
    if not graph:
        return False, [error]

    path = '%s/NS_sheet' % DESTINATION
    bp = unreal.load_asset(path)

    created = unreal.NodeScribeLibrary.write_object(
        bp, 'variable Is Alive : Boolean editable = true\nvariable Speed : Float = 2.5\n')
    if '[error]' in created:
        return False, ['creating the variables complained:'] + created.splitlines()

    sheet = unreal.NodeScribeLibrary.read_object(bp, '')
    written = unreal.NodeScribeLibrary.write_object(bp, sheet)

    problems = []
    if '[error]' in written:
        problems.append('pasting the sheet back complained:')
        problems.extend('  ' + l for l in written.splitlines())

    again = unreal.NodeScribeLibrary.read_object(bp, '')
    if body(again) != body(sheet):
        problems.append('pasting the sheet back changed it:')
        problems.extend(difflib.unified_diff(body(sheet), body(again), 'before', 'after',
                                             lineterm='', n=2))

    if problems:
        problems.append('')
        problems.append('--- sheet ---')
        problems.extend(sheet.splitlines())
        return False, problems

    return True, []


def any_skeleton():
    """A Skeleton from the project, or None.

    The animation cases need a real skeleton -- an AnimBlueprint cannot be
    created without one, and on a finished asset the field is read-only.
    Taking the first one the registry has keeps the suite runnable in any
    project: what is tested is the text's round trip, and for that it does
    not matter whose skeleton it is.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = registry.get_assets_by_class(
        unreal.TopLevelAssetPath('/Script/Engine', 'Skeleton'), False)

    return str(found[0].package_name) if found else None


def anim_graph(name, skeleton):
    """An empty AnimGraph in a new AnimBlueprint."""
    path = '%s/%s' % (DESTINATION, name)

    report = unreal.NodeScribeLibrary.create_asset(
        path, 'AnimBlueprint', 'TargetSkeleton = %s' % skeleton)
    if report.startswith('[error]'):
        return None, report

    bp = unreal.load_asset(path)
    if not bp:
        return None, 'could not load %s' % path

    graph = unreal.load_object(bp, 'AnimGraph')
    if not graph:
        return None, 'could not find the AnimGraph of %s' % path

    return graph, ''


def state_machine_round_trip():
    """State, conduit, alias and transition options: round trip.

    No asset at all, on purpose. What is tested here is the structure -- who
    is a state, who is a conduit, who is an alias, what each transition keeps
    in the panel --, and for a long time none of that fitted in the text:
    aliases and conduits were not declared, and a transition with an automatic
    rule came back identical to a dead transition.
    """
    skeleton = any_skeleton()
    if not skeleton:
        return True, ['skipped: this project has no Skeleton.']

    text = """
variable Active : Boolean
variable Speed : Float

Machine = State Machine
  state Idle (Always Reset on Entry = true):
  state Walking:
  alias Any Of Them:
    Walking
    Idle
  conduit Passage:
    Get Active
  Idle -> Passage (Crossfade Duration = 0.35, Blend Mode = Cubic):
    Get Active
  Passage -> Walking (Priority Order = 2):
    KismetMathLibrary.Greater_DoubleDouble (A = $Speed, B = 10.0)
  Walking -> Idle (Automatic Rule Based on Sequence Player in State = true):
"""

    graph_a, error = anim_graph('NS_machine_a', skeleton)
    if not graph_a:
        return False, ['could not prepare the first AnimBlueprint: ' + error]

    written = unreal.NodeScribeLibrary.write_graph(graph_a, text)
    if '[error]' in written:
        report = ['the write complained:']
        report.extend('  ' + l for l in written.splitlines())
        return False, report

    t1 = unreal.NodeScribeLibrary.read_graph(graph_a)

    # The automatic transition is born without a rule on purpose. A "never
    # fires" warning here would be false, and would teach people to ignore the
    # warning that exists for when it is true.
    if 'never fires' in written:
        report = ['warned that a transition never fires, and the automatic one does:']
        report.extend('  ' + l for l in written.splitlines())
        return False, report

    graph_b, error = anim_graph('NS_machine_b', skeleton)
    if not graph_b:
        return False, ['could not prepare the second AnimBlueprint: ' + error]

    unreal.NodeScribeLibrary.write_graph(graph_b, t1)
    t2 = unreal.NodeScribeLibrary.read_graph(graph_b)

    a, b = body(t1), body(t2)
    if a == b:
        return True, []

    report = ['the second reading did not match the first:']
    report.extend(difflib.unified_diff(a, b, 'T1', 'T2', lineterm='', n=2))
    report.append('')
    report.append('--- whole T1 ---')
    report.extend(t1.splitlines())
    return False, report


def blendspace_gets_grid():
    """Filling a BlendSpace has to rebuild the interpolation grid.

    Regression of the most silent bug the plugin ever had: the samples went
    in, the asset opened, the editor drew the points -- and the BlendSpace
    Player returned the reference pose, because what interpolates is the grid,
    and it stayed empty. The Blueprint compiled without a single warning.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = registry.get_assets_by_class(
        unreal.TopLevelAssetPath('/Script/Engine', 'AnimSequence'), False)

    if not found:
        return True, ['skipped: this project has no AnimSequence.']

    sequence = unreal.load_asset(str(found[0].package_name))
    skeleton = sequence.get_editor_property('skeleton')
    if not skeleton:
        return True, ['skipped: `%s` has no skeleton.' % sequence.get_name()]

    path = '%s/NS_blendspace' % DESTINATION
    report = unreal.NodeScribeLibrary.create_asset(
        path, 'BlendSpace1D', 'TargetSkeleton = %s' % skeleton.get_path_name())
    if report.startswith('[error]'):
        return False, [report]

    blendspace = unreal.load_asset(path)
    if not blendspace:
        return False, ['could not load %s' % path]

    lines = [
        'axis X : Speed = 0 .. 600',
        '%s = 0' % sequence.get_path_name(),
    ]
    written = unreal.NodeScribeLibrary.write_blend_space(blendspace, chr(10).join(lines))

    if '[error]' in written:
        return False, ['the write complained:'] + written.splitlines()

    text = unreal.NodeScribeLibrary.read_object(blendspace, '')
    if 'interpolation grid is empty' in text:
        return False, [
            'the sample went in and the grid stayed empty: this BlendSpace returns',
            'the reference pose, and nothing in the graph gives that away.',
            '',
        ] + text.splitlines()

    return True, []


def chain_reaches_function_entry():
    """Pasting into a function graph has to link the chain to the entry.

    The Construction Script is the case at hand: its entry already exists and
    is not created by a line, just like an AnimGraph's Output Pose. Without the
    link, the whole chain goes in, compiles without a warning, and never runs --
    and reading it back only gives that away by the `Function Entry` showing up
    alone at the end, instead of in front of the chain it triggers.
    """
    path = '%s/NS_function_entry' % DESTINATION

    report = unreal.NodeScribeLibrary.create_asset(path, 'Actor', '')
    if report.startswith('[error]'):
        return False, [report]

    bp = unreal.load_asset(path)
    graph = unreal.load_object(bp, 'UserConstructionScript') if bp else None
    if not graph:
        return False, ['could not find the UserConstructionScript of %s' % path]

    written = unreal.NodeScribeLibrary.write_graph(
        graph, 'Print String (In String = "from the construction script")')

    if '[error]' in written:
        return False, ['the write complained:'] + written.splitlines()

    lines = body(unreal.NodeScribeLibrary.read_graph(graph))

    # The reading walks from the entry: with the chain linked, the
    # `Function Entry` opens the text. Loose, it comes out afterwards, as an orphan.
    if not lines or lines[0].strip() != 'Function Entry':
        return False, [
            'the Function Entry does not open the reading: the chain went in loose and',
            'never runs.',
            '',
        ] + lines

    return True, []


def insert_into_existing_chain():
    """`after <a line of the reading>:` inserts into a chain that is already there.

    The R-015 case in small: a value computed up the chain, a Branch, and a
    node to slip in right after another inside the `true` branch. Also at the
    end of a branch, at the head of the other one through a label, and the
    refusals -- ambiguous, missing, with `replace` -- that must leave the graph
    as it was.
    """
    import re

    graph, error = new_blueprint('NS_insert_after')
    if not graph:
        return False, [error]

    problems = []

    base = unreal.NodeScribeLibrary.write_graph(graph, '''
variable bOn : Boolean
event Update
t = Get Game Time in Seconds
Branch (Condition = $bOn)
  true:
    Print String (In String = "volts", Duration = $t)
    Print String (In String = "end")
  false:
    Print String (In String = "off")
''')
    if '[error]' in base:
        return False, ['the base graph complained:'] + base.splitlines()

    # The name is the reading's, not the text's: an `after` block uses what
    # the assistant saw.
    before = unreal.NodeScribeLibrary.read_graph(graph)
    found = re.search(r'In String = "?volts"?, Duration = \$(\w+)', before)
    if not found:
        return False, ['the reading has no named Duration on the volts line:'] + before.splitlines()
    time_name = found.group(1)

    def write(label, text, replace=False):
        written = unreal.NodeScribeLibrary.write_graph(graph, text, replace)
        if '[error]' in written:
            problems.append('%s complained:' % label)
            problems.extend('  ' + l for l in written.splitlines())
        return written

    write('the insertion in the middle', '''
after Print String (In String = "volts"):
  Print String (In String = "current", Duration = $%s)
''' % time_name)
    write('the insertion at the end', '''
after Print String (In String = "end"):
  Print String (In String = "tail")
''')
    write('the insertion through a label', '''
after Branch (Condition = $bOn):
  false:
    Print String (In String = "first in false")
''')

    text = unreal.NodeScribeLibrary.read_graph(graph)
    lines = [l.strip().replace('"', '') for l in body(text)]

    wanted = [
        'true:',
        'Print String (In String = volts, Duration = $%s)' % time_name,
        'Print String (In String = current, Duration = $%s)' % time_name,
        'Print String (In String = end)',
        'Print String (In String = tail)',
        'false:',
        'Print String (In String = first in false)',
        'Print String (In String = off)',
    ]
    # Labels in lower case: `True:` or `true:` is the formatter's call.
    chain = [l if l.startswith('Print String') else l.lower()
             for l in lines if l.startswith('Print String') or l.endswith(':')]
    if chain != wanted:
        problems.append('the chain is not the one the insertions describe:')
        problems.extend(difflib.unified_diff(wanted, chain, 'wanted', 'read', lineterm='', n=2))

    # One Get Game Time, used twice: a second one would mean the name did not
    # reach the existing node.
    if text.count('Get Game Time in Seconds') != 1:
        problems.append('`$%s` did not reuse the node already in the graph' % time_name)

    if '# ->' in text:
        problems.append('the insertion left a reconvergence or a side input behind')

    # Refusals: each leaves the reading exactly as it was.
    settled = body(text)
    refusals = [
        ('an ambiguous `after`', 'after Print String:\n  Print String (In String = "x")\n', False, 'matches'),
        ('an `after` that finds nothing', 'after Print String (In String = "nope"):\n  Print String (In String = "x")\n',
         False, 'matches no node'),
        ('an `after` with `replace`', 'after Print String (In String = "end"):\n  Print String (In String = "x")\n',
         True, 'replace'),
    ]
    for label, refused, replace, expected in refusals:
        written = unreal.NodeScribeLibrary.write_graph(graph, refused, replace)
        if '[error]' not in written or expected not in written:
            problems.append('%s was not refused with %r: %s' % (label, expected, written))
        if body(unreal.NodeScribeLibrary.read_graph(graph)) != settled:
            problems.append('%s changed the graph' % label)

    if problems:
        problems.append('')
        problems.extend(text.splitlines())
        return False, problems

    return True, []


def designer_section(text):
    """The lines from `designer` on, without comments: what a write takes back."""
    lines = text.splitlines()
    if 'designer' not in lines:
        return []
    return body('\n'.join(lines[lines.index('designer'):]))


def in_order(lines, wanted):
    """True when every wanted line shows up in `lines`, in this order.

    Quotes are left out of the comparison: whether `Battery` needs them is the
    formatter's call, not what this test is about.
    """
    position = 0
    for line in lines:
        if position < len(wanted) and line.replace('"', '') == wanted[position].replace('"', ''):
            position += 1
    return position == len(wanted)


def designer_writes_rows():
    """The Designer as text: create a tree, add a row `like` another, paste back.

    The row case is the one that motivated it: a label and an answer like the
    ones already on screen, placed right after them, in one short text.
    """
    path = '%s/NS_designer' % DESTINATION
    created = unreal.NodeScribeLibrary.create_asset(path, 'UserWidget', '')
    if created.startswith('[error]'):
        return False, [created]

    bp = unreal.load_asset(path)
    if not isinstance(bp, unreal.WidgetBlueprint):
        return False, ['create_asset with UserWidget did not give a Widget Blueprint: %s' % bp]

    # The factory may or may not give a default root, depending on the
    # project's settings. The test starts from an empty Designer either way.
    start = designer_section(unreal.NodeScribeLibrary.read_object(bp, ''))
    reset = ''
    if len(start) > 1:
        reset = 'delete widget %s\n' % start[1].split(' : ')[0].strip()

    first = 'designer\n' + reset + '''Root : VerticalBox
  IsStillRow : HorizontalBox
    IsStillLabel : TextBlock
      Text = "Is still?"
    IsStillAnswer : TextBlock variable
      Text = "-"
      slot Padding = (Left=8.0,Top=0.0,Right=0.0,Bottom=0.0)
  Footer : TextBlock
'''
    written = unreal.NodeScribeLibrary.write_object(bp, first)
    if '[error]' in written:
        return False, ['creating the tree complained:'] + written.splitlines()

    # Root and IsStillRow are anchors; the new row goes between IsStillRow and
    # Footer because that is where the text puts it.
    row = '''designer
Root : VerticalBox
  IsStillRow : HorizontalBox
  BatteryRow : HorizontalBox like IsStillRow
    BatteryLabel : TextBlock like IsStillLabel
      Text = "Battery"
    BatteryAnswer : TextBlock variable like IsStillAnswer
'''
    written = unreal.NodeScribeLibrary.write_object(bp, row)
    if '[error]' in written:
        return False, ['adding the row complained:'] + written.splitlines()

    sheet = unreal.NodeScribeLibrary.read_object(bp, '')
    tree = designer_section(sheet)

    problems = []
    expected = [
        'Root : VerticalBox',
        '  IsStillRow : HorizontalBox',
        '    IsStillLabel : TextBlock',
        '      Text = "Is still?"',
        '    IsStillAnswer : TextBlock variable',
        '      Text = "-"',
        '  BatteryRow : HorizontalBox',
        '    BatteryLabel : TextBlock',
        '      Text = Battery',
        '    BatteryAnswer : TextBlock variable',
        '      Text = "-"',
        '  Footer : TextBlock',
    ]
    if not in_order(tree, expected):
        problems.append('the tree is not what was written, in that order. Expected, in order:')
        problems.extend('  ' + l for l in expected)

    if sum(1 for l in tree if l.strip().startswith('slot Padding')) != 2:
        problems.append('`like` did not copy the slot\'s padding onto BatteryAnswer.')

    # Pasting the whole reading back is the fixed point: it changes nothing.
    again_written = unreal.NodeScribeLibrary.write_object(bp, sheet)
    if '[error]' in again_written:
        problems.append('pasting the reading back complained:')
        problems.extend('  ' + l for l in again_written.splitlines())

    again = designer_section(unreal.NodeScribeLibrary.read_object(bp, ''))
    if again != tree:
        problems.append('pasting the reading back changed it:')
        problems.extend(difflib.unified_diff(tree, again, 'before', 'after', lineterm='', n=2))

    # Refused, with nothing changed: a move under a widget that is not a
    # panel, an unknown class, a second root.
    for label, text in (
        ('move under a text', 'designer\nIsStillLabel : TextBlock\n  Footer : TextBlock\n'),
        ('unknown class', 'designer\nRoot : VerticalBox\n  Ghost : NoSuchWidget\n'),
        ('second root', 'designer\nOther : VerticalBox\n'),
    ):
        refused = unreal.NodeScribeLibrary.write_object(bp, text)
        if '[error]' not in refused:
            problems.append('the %s was not refused:' % label)
            problems.extend('  ' + l for l in refused.splitlines())

    # A typo is answered with the name it missed, for classes and properties.
    for text, wanted in (
        ('designer\nRoot : VerticalBox\n  Ghost : TextBlok\n', 'TextBlock'),
        ('designer\nFooter : TextBlock\n  Txet = "x"\n', 'Text'),
    ):
        refused = unreal.NodeScribeLibrary.write_object(bp, text)
        if 'Similar' not in refused or wanted not in refused:
            problems.append('a typo was not answered with `%s`:' % wanted)
            problems.extend('  ' + l for l in refused.splitlines())

    after = designer_section(unreal.NodeScribeLibrary.read_object(bp, ''))
    if after != tree:
        problems.append('a refused write changed the tree:')
        problems.extend(difflib.unified_diff(tree, after, 'before', 'after', lineterm='', n=2))

    if problems:
        problems.append('')
        problems.append('--- reading ---')
        problems.extend(sheet.splitlines())
        return False, problems

    return True, []


def new_widget_blueprint(name, options=''):
    """An empty Widget Blueprint: the factory's default root, if any, removed."""
    path = '%s/%s' % (DESTINATION, name)
    created = unreal.NodeScribeLibrary.create_asset(path, 'UserWidget', options)
    if created.startswith('[error]'):
        return None, created

    bp = unreal.load_asset(path)
    start = designer_section(unreal.NodeScribeLibrary.read_object(bp, ''))
    if len(start) > 1 and not start[1].startswith('named slot'):
        unreal.NodeScribeLibrary.write_object(
            bp, 'designer\ndelete widget %s\n' % start[1].split(' : ')[0].strip())
    return bp, ''


def write_clean(problems, label, target, text):
    """Writes, and records a complaint when the write reports an error."""
    written = unreal.NodeScribeLibrary.write_object(target, text)
    if '[error]' in written:
        problems.append('%s complained:' % label)
        problems.extend('  ' + l for l in written.splitlines())
    return written


def paste_back_changes_nothing(problems, label, target, section=body):
    """The fixed point of any sheet: a reading pasted back reads the same."""
    reading = unreal.NodeScribeLibrary.read_object(target, '')
    write_clean(problems, 'pasting the %s back' % label, target, reading)
    again = unreal.NodeScribeLibrary.read_object(target, '')
    if section(again) != section(reading):
        problems.append('pasting the %s back changed it:' % label)
        problems.extend(difflib.unified_diff(section(reading), section(again), 'before', 'after',
                                             lineterm='', n=2))
    return reading


def components_by_text():
    """`component Name : Class [in Parent]` adds a configured component.

    The header names every component with its parent, the block shows what
    changed, a second `component` line for the same name is an anchor, and a
    class the Components panel does not offer is refused.
    """
    graph, error = new_blueprint('NS_components')
    if not graph:
        return False, [error]
    bp = unreal.load_asset('%s/NS_components' % DESTINATION)

    problems = []
    write_clean(problems, 'adding the components', bp, '''component Hitbox : Box Collision
  Box Extent = (X=40.0,Y=40.0,Z=60.0)
component Marker : ArrowComponent in Hitbox
component Spinner : RotatingMovementComponent
''')

    sheet = unreal.NodeScribeLibrary.read_object(bp, '')
    header = next((l for l in sheet.splitlines() if l.startswith('# components:')), '')
    for wanted in ('Hitbox', 'Marker in Hitbox', 'Spinner'):
        if wanted not in header:
            problems.append('the components header does not say `%s`: %r' % (wanted, header))
    if not in_order(sheet.splitlines(), ['Hitbox : BoxComponent']) or 'Box Extent' not in sheet:
        problems.append('the Hitbox block with its Box Extent is missing.')
    if any(l.startswith(('Hitbox =', 'Marker =')) for l in sheet.splitlines()):
        problems.append('the variable pointing at a component came out as a property line.')

    write_clean(problems, 'the same component line again', bp, 'component Hitbox : BoxComponent\n')
    paste_back_changes_nothing(problems, 'sheet', bp)

    for label, text in (
        ('a class the panel does not offer', 'component Walker : CharacterMovementComponent\n'),
        ('another class under a taken name', 'component Hitbox : SphereComponent\n'),
        ('a parent that does not exist', 'component Lost : Box Collision in Nowhere\n'),
    ):
        refused = unreal.NodeScribeLibrary.write_object(bp, text)
        if '[error]' not in refused:
            problems.append('%s was not refused:' % label)
            problems.extend('  ' + l for l in refused.splitlines())

    write_clean(problems, 'deleting a component', bp, 'delete component Marker\n')
    header = next((l for l in unreal.NodeScribeLibrary.read_object(bp, '').splitlines()
                   if l.startswith('# components:')), '')
    if 'Marker' in header:
        problems.append('`delete component Marker` left it: %r' % header)

    if problems:
        problems.extend(['', '--- sheet ---'] + sheet.splitlines())
        return False, problems
    return True, []


def effect_components_block():
    """A Gameplay Effect's `Components` as a block: a whole cooldown by text."""
    if not hasattr(unreal, 'GameplayEffect'):
        return True, ['did not run: GameplayAbilities is not loaded']

    tags = [l[4:] for l in unreal.NodeScribeLibrary.read_tags('').splitlines() if l.startswith('tag ')]
    if not tags:
        return True, ['did not run: the project declares no Gameplay Tag']
    tag = tags[0]

    path = '%s/NS_effect' % DESTINATION
    created = unreal.NodeScribeLibrary.create_asset(path, 'GameplayEffect', '')
    if created.startswith('[error]'):
        return False, [created]
    effect = unreal.load_asset(path)

    problems = []
    write_clean(problems, 'writing the cooldown', effect, '''Duration Policy = HasDuration
Duration Magnitude:
  Scalable Float Magnitude = (Value=7.0)
Components:
  Grant Tags to Target Actor
    Add Tags:
      Added = (GameplayTags=((TagName="%s")))
''' % tag)

    sheet = unreal.NodeScribeLibrary.read_object(effect, '')
    lines = sheet.splitlines()
    if not in_order(lines, ['Components:', '  Grant Tags to Target Actor']):
        problems.append('the Components block did not come out with its element.')
    granted = next((l for l in lines if 'Add Tags' in l), '')
    # CombinedTags is what the component recomputes when told its container
    # changed -- the sign the change reached it under the struct's name.
    if granted.count(tag) < 2:
        problems.append('Add Tags did not recompute its combined tags: %r' % granted)

    paste_back_changes_nothing(problems, 'sheet', effect)

    refused = unreal.NodeScribeLibrary.write_object(effect, 'Components:\n  No Such Component\n')
    if '[error]' not in refused or 'Grant Tags' not in unreal.NodeScribeLibrary.read_object(effect, ''):
        problems.append('an unknown element class was not refused, or changed the array:')
        problems.extend('  ' + l for l in refused.splitlines())

    emptied = unreal.NodeScribeLibrary.write_object(effect, 'Components:\n')
    if 'removed' not in emptied or 'Grant Tags' in unreal.NodeScribeLibrary.read_object(effect, ''):
        problems.append('an empty block did not empty the array, or did not say so:')
        problems.extend('  ' + l for l in emptied.splitlines())

    if problems:
        problems.extend(['', '--- sheet ---'] + lines)
        return False, problems
    return True, []


def input_mapping_section():
    """An Input Mapping Context as `Key -> Action` lines, both ways."""
    for name in ('NS_IA_Jump', 'NS_IA_Move'):
        created = unreal.NodeScribeLibrary.create_asset('%s/%s' % (DESTINATION, name), 'InputAction', '')
        if created.startswith('[error]'):
            return True, ['did not run: %s' % created]

    path = '%s/NS_mapping' % DESTINATION
    created = unreal.NodeScribeLibrary.create_asset(path, 'InputMappingContext', '')
    if created.startswith('[error]'):
        return True, ['did not run: %s' % created]
    context = unreal.load_asset(path)

    problems = []
    wanted = [
        'mappings',
        'SpaceBar -> NS_IA_Jump',
        '  trigger Hold',
        '    Hold Time Threshold = 0.75',
        'Gamepad_Left2D -> NS_IA_Move',
        '  modifier Dead Zone',
        '    Lower Threshold = 0.25',
        '  modifier Negate',
        'W -> NS_IA_Move',
    ]
    write_clean(problems, 'writing the mappings', context, '\n'.join(wanted) + '\n')

    reading = unreal.NodeScribeLibrary.read_object(context, '')
    if not in_order(reading.splitlines(), wanted):
        problems.append('the mappings did not come back as written. Expected, in order:')
        problems.extend('  ' + l for l in wanted)

    paste_back_changes_nothing(problems, 'mappings', context)

    # A modifier that does not resolve leaves the whole mapping's set alone.
    refused = unreal.NodeScribeLibrary.write_object(
        context, 'mappings\nSpaceBar -> NS_IA_Jump\n  trigger Tap\n  modifier No Such Modifier\n')
    after = unreal.NodeScribeLibrary.read_object(context, '')
    if '[error]' not in refused or not in_order(after.splitlines(), wanted[1:4]):
        problems.append('a bad modifier was not refused, or changed the triggers:')
        problems.extend('  ' + l for l in refused.splitlines())

    write_clean(problems, 'deleting a mapping', context, 'mappings\ndelete mapping W -> NS_IA_Move\n')
    if 'W -> NS_IA_Move' in unreal.NodeScribeLibrary.read_object(context, ''):
        problems.append('`delete mapping` left it.')

    if problems:
        problems.extend(['', '--- reading ---'] + reading.splitlines())
        return False, problems
    return True, []


def designer_moves_and_slots():
    """Reorder, move, and fill named slots -- of an instance and of a child."""
    host, error = new_widget_blueprint('NS_slot_host')
    if not host:
        return False, [error]

    problems = []
    write_clean(problems, 'building the host', host,
                'designer\nRoot : VerticalBox\n  Body : NamedSlot\n  Foot : NamedSlot\n')

    bp, error = new_widget_blueprint('NS_moves')
    if not bp:
        return False, [error]
    write_clean(problems, 'building the tree', bp, '''designer
Root : VerticalBox
  A : TextBlock
  B : TextBlock
  C : TextBlock
  Row : HorizontalBox
''')

    # C and A swap the places they had; B, which the text does not name,
    # keeps its own. Then B moves into Row, and the instance's slot is filled.
    write_clean(problems, 'reordering', bp, 'designer\nRoot : VerticalBox\n  C : TextBlock\n  A : TextBlock\n')
    write_clean(problems, 'moving and filling a slot', bp, '''designer
Root : VerticalBox
  Row : HorizontalBox
    B : TextBlock
  Framed : NS_slot_host
    named slot Body
      Inside : TextBlock
''')

    tree = designer_section(unreal.NodeScribeLibrary.read_object(bp, ''))
    names = [l for l in tree if ' : ' in l or l.strip().startswith('named slot')]
    expected = [
        'Root : VerticalBox',
        '  C : TextBlock',
        '  A : TextBlock',
        '  Row : HorizontalBox',
        '    B : TextBlock',
        '  Framed : NS_slot_host',
        '    named slot Body',
        '      Inside : TextBlock',
    ]
    if names != expected:
        problems.append('the tree is not the expected one:')
        problems.extend(difflib.unified_diff(expected, names, 'expected', 'read', lineterm='', n=2))

    paste_back_changes_nothing(problems, 'Designer', bp, designer_section)

    for label, text in (
        ('filling a slot that is taken', 'designer\nFramed : NS_slot_host\n  named slot Body\n    Other : TextBlock\n'),
        ('a slot that does not exist', 'designer\nFramed : NS_slot_host\n  named slot Nope\n    Other : TextBlock\n'),
        ('moving into a slot', 'designer\nFramed : NS_slot_host\n  named slot Foot\n    A : TextBlock\n'),
        ('moving the root', 'designer\nRow : HorizontalBox\n  Root : VerticalBox\n'),
    ):
        refused = unreal.NodeScribeLibrary.write_object(bp, text)
        if '[error]' not in refused:
            problems.append('%s was not refused:' % label)
            problems.extend('  ' + l for l in refused.splitlines())

    # A child Widget Blueprint designs its parent's named slots.
    child, error = new_widget_blueprint(
        'NS_slot_child', 'ParentClass = %s/NS_slot_host.NS_slot_host_C' % DESTINATION)
    if not child:
        problems.append(error)
    else:
        empty = designer_section(unreal.NodeScribeLibrary.read_object(child, ''))
        if not any(l.startswith('named slot Body') for l in empty):
            problems.append('the child does not list its parent\'s slots: %r' % empty)
        write_clean(problems, 'filling the child\'s slot', child,
                    'designer\nnamed slot Body\n  FromChild : TextBlock\n')
        filled = designer_section(unreal.NodeScribeLibrary.read_object(child, ''))
        if not in_order(filled, ['named slot Body', '  FromChild : TextBlock']):
            problems.append('the child\'s slot did not come back filled: %r' % filled)
        paste_back_changes_nothing(problems, 'child Designer', child, designer_section)

    if problems:
        problems.extend(['', '--- tree ---'] + tree)
        return False, problems
    return True, []


PROJECT_SKIP = (DESTINATION, '/Game/BossRush/Testes')
BLUEPRINT_ASSET_CLASSES = ('Blueprint', 'WidgetBlueprint', 'AnimBlueprint', 'GameplayAbilityBlueprint',
                           'EditorUtilityBlueprint', 'EditorUtilityWidgetBlueprint')


def project_graphs_round_trip():
    """Every graph of the project: read, written into a copy, read again.

    The copy is the Blueprint duplicated into DESTINATION -- same parent,
    variables, functions and components, so a call to its own function
    resolves as in the original. Each of the copy's graphs is emptied with
    clear_graph and refilled with the original's reading. A difference the
    reading does not warn about is a failure: the text was not the graph and
    nothing said so -- the Set Visibility, Append pins and split-struct names
    were found by tripping on them. One the reading warns about is listed
    apart, as declared. Nothing is saved: the commandlet exits without saving.
    """
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = [a for a in registry.get_assets_by_path('/Game', recursive=True)
              if str(a.asset_class_path.asset_name) in BLUEPRINT_ASSET_CLASSES
              and not str(a.package_name).startswith(PROJECT_SKIP)]
    if not assets:
        return True, ['did not run: no Blueprint under /Game']

    failures, declared, checked = [], [], 0

    for index, asset in enumerate(sorted(assets, key=lambda a: str(a.package_name))):
        source = str(asset.package_name)
        name = str(asset.asset_name)
        original = unreal.load_asset(source)
        if not original:
            failures.append('%s: could not load' % source)
            continue

        header = unreal.NodeScribeLibrary.read_object(original, '').splitlines()
        graphs = next((l[len('# graphs: '):].split(', ') for l in header if l.startswith('# graphs: ')), [])
        if not graphs:
            continue

        copy_path = '%s/Project/%s_%d' % (DESTINATION, name, index)
        if not unreal.EditorAssetLibrary.duplicate_asset(source, copy_path):
            failures.append('%s: could not duplicate it' % source)
            continue
        copy_name = copy_path.rsplit('/', 1)[1]

        for graph_name in graphs:
            label = '%s:%s' % (name, graph_name)
            graph = unreal.load_object(None, '%s.%s:%s' % (source, name, graph_name))
            target = unreal.load_object(None, '%s.%s:%s' % (copy_path, copy_name, graph_name))
            if not graph or not target:
                failures.append('%s: could not find the graph (or its copy)' % label)
                continue

            first = unreal.NodeScribeLibrary.read_graph(graph)
            unreal.NodeScribeLibrary.clear_graph(target)
            written = unreal.NodeScribeLibrary.write_graph(target, first, False)
            second = unreal.NodeScribeLibrary.read_graph(target)
            checked += 1

            if body(first) == body(second):
                continue

            diff = list(difflib.unified_diff(body(first), body(second), 'read', 'pasted and read',
                                             lineterm='', n=1))[2:14]
            # Said out loud: a warning, or the note naming the data nodes that
            # feed nothing and were left out of the text.
            if '[warning]' in first or 'were left out of the text' in first:
                declared.append(label)
            else:
                failures.append(label)
                failures.extend('    ' + l for l in diff)
                failures.extend('    | ' + l for l in written.splitlines() if '[error]' in l)

        # Gone before the next one: a copy left in memory is a second class
        # with the same functions, and every call to them reads as ambiguous.
        unreal.EditorAssetLibrary.delete_asset(copy_path)
        unreal.SystemLibrary.collect_garbage()

    # The diffs go to a file of their own: with a project's worth of graphs
    # they would bury the rest of the report.
    details = os.path.join(unreal.Paths.project_saved_dir(), 'NodeScribe', 'project_graphs.txt')
    os.makedirs(os.path.dirname(details), exist_ok=True)
    with open(details, 'w', encoding='utf-8') as details_file:
        details_file.write('\n'.join(failures + ['', 'declared: ' + ', '.join(declared)]))

    silent = [l for l in failures if not l.startswith('    ')]
    summary = '%d graph(s) of %d Blueprint(s): %d silent difference(s), %d declared by a warning' % (
        checked, len(assets), len(silent), len(declared))
    if silent:
        return False, [summary, 'silent: ' + ', '.join(silent), 'diffs in ' + details]
    return True, [summary]


def token_budget():
    """What every session pays stays under the ceilings in BUDGET."""
    import ast

    problems, numbers = [], []

    def check(label, value, ceiling):
        numbers.append('%s: %s (ceiling %s)' % (label, value, ceiling))
        if value > ceiling:
            problems.append('%s is over its ceiling: %s > %s' % (label, value, ceiling))

    core = unreal.NodeScribeLibrary.get_format_docs('')
    if core.startswith('[error]'):
        return False, [core]
    check('core', len(core), BUDGET['core'])

    unknown = unreal.NodeScribeLibrary.get_format_docs('no-such-topic')
    if not unknown.startswith('[error]'):
        return False, ['an unknown topic did not answer with an error']
    topics = unknown.split('Topics: ', 1)[-1].split('.', 1)[0].split(', ')
    if len(topics) < 4:
        problems.append('the error lists too few topics: %r' % unknown)
    for topic in topics:
        text = unreal.NodeScribeLibrary.get_format_docs(topic)
        if text.startswith('[error]'):
            problems.append('topic %s: %s' % (topic, text))
            continue
        check('topic ' + topic, len(text), BUDGET['topic'])
        # The core names every topic, or an assistant never learns it exists.
        if '`%s`' % topic not in core:
            problems.append('the core does not name the topic `%s`' % topic)

    if not unreal.NodeScribeLibrary.get_format_docs('../FORMAT').startswith('[error]'):
        problems.append('a topic with a path walked out of Docs/format')

    source = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          'Content', 'Python', 'nodescribe_toolset', 'toolsets', 'graph.py')
    with open(source, encoding='utf-8') as f:
        tree = ast.parse(f.read())
    tools = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef)]
    described = sum(len(ast.get_docstring(n) or '') for n in ast.walk(tree)
                    if isinstance(n, (ast.Module, ast.ClassDef, ast.FunctionDef)))
    check('descriptions', described, BUDGET['descriptions'])
    check('tools', len(tools), BUDGET['tools'])

    if READING_SIZES:
        def ratio(name):
            given, read = READING_SIZES[name]
            return read / max(given, 1)
        worst = max(READING_SIZES, key=ratio)
        check('reading_ratio (%s, %d -> %d chars)' % ((worst,) + READING_SIZES[worst]),
              round(ratio(worst), 2), BUDGET['reading_ratio'])

    # Passing, it still prints the numbers: growth shows before it fails.
    return not problems, problems + numbers


OTHERS = [
    ('designer_writes_rows', designer_writes_rows),
    ('chain_reaches_function_entry', chain_reaches_function_entry),
    ('replace_swaps_the_graph', replace_swaps_the_graph),
    ('empty_event_is_reused', empty_event_is_reused),
    ('insert_into_existing_chain', insert_into_existing_chain),
    ('replace_refuses_lossy_graph', replace_refuses_lossy_graph),
    ('replace_takes_own_break', replace_takes_own_break),
    ('sheet_round_trip_changes_nothing', sheet_round_trip_changes_nothing),
    ('state_machine_round_trip', state_machine_round_trip),
    ('blendspace_gets_grid', blendspace_gets_grid),
    ('components_by_text', components_by_text),
    ('effect_components_block', effect_components_block),
    ('input_mapping_section', input_mapping_section),
    ('designer_moves_and_slots', designer_moves_and_slots),
    ('project_graphs_round_trip', project_graphs_round_trip),
    # Last: it reads the sizes the round trips above recorded.
    ('token_budget', token_budget),
]


def main():
    parts = []
    failures = []

    for case in CASES:
        passed, report = round_trip(case)
        parts.append('%s  %s' % ('ok   ' if passed else 'FAIL ', case['name']))

        if not passed:
            failures.append(case['name'])
            parts.extend('       ' + l for l in report)
            parts.append('')
        elif report:
            # A case that passed and has something to say only says one thing:
            # that it did not run. A skipped test printed as `ok` is the same
            # hole the plugin refuses everywhere -- it looks like coverage and
            # is not.
            parts.extend('       ' + l for l in report)

    for name, function in OTHERS:
        # An exception here used to bring the whole `main` down, and the report
        # -- which is only written at the end -- did not come out: the one from
        # a previous run stayed on disk, with the old result and the old count.
        # Reading that after a run that died is worse than having no report.
        try:
            passed, report = function()
        except Exception:
            passed, report = False, traceback.format_exc().splitlines()

        parts.append('%s  %s' % ('ok   ' if passed else 'FAIL ', name))

        if not passed:
            failures.append(name)
            parts.extend('       ' + l for l in report)
            parts.append('')
        elif report:
            parts.extend('       ' + l for l in report)

    summary = '%d case(s), %d failure(s)' % (len(CASES) + len(OTHERS), len(failures))
    parts.append('')
    parts.append(summary)

    text = '\n'.join(parts)

    output = os.path.join(unreal.Paths.project_saved_dir(), 'NodeScribe', 'tests.txt')
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, 'w', encoding='utf-8') as report_file:
        report_file.write(text)

    unreal.log('NodeScribe tests: %s -- report in %s' % (summary, output))

    if failures:
        unreal.log_error('NodeScribe tests: failed %s' % ', '.join(failures))
        raise RuntimeError('NodeScribe: %s' % summary)


main()
