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

    # Refused, with nothing changed: a move, an unknown class, a second root.
    for label, text in (
        ('move', 'designer\nRoot : VerticalBox\n  IsStillRow : HorizontalBox\n    Footer : TextBlock\n'),
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


OTHERS = [
    ('designer_writes_rows', designer_writes_rows),
    ('chain_reaches_function_entry', chain_reaches_function_entry),
    ('replace_swaps_the_graph', replace_swaps_the_graph),
    ('replace_refuses_lossy_graph', replace_refuses_lossy_graph),
    ('replace_takes_own_break', replace_takes_own_break),
    ('sheet_round_trip_changes_nothing', sheet_round_trip_changes_nothing),
    ('state_machine_round_trip', state_machine_round_trip),
    ('blendspace_gets_grid', blendspace_gets_grid),
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
