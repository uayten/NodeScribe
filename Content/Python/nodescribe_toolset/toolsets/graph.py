"""Blueprint graphs as compact text.

This toolset exists because of cost. Building a graph with the conventional
tools spends most of the tokens *discovering* node identifiers -- one call per
type, each returning dozens of results. NodeScribe resolves the names
locally, with tolerant lookup, so a whole graph fits in one call.
"""

import unreal

import toolset_registry


@unreal.uclass()
class NodeScribeTools(unreal.ToolsetDefinition):
    """Blueprint graphs and object properties as text, in one call."""

    @toolset_registry.tool_call
    @staticmethod
    def write_graph(graph: unreal.EdGraph, text: str,
                    replace: bool | None = None) -> str:
        """Creates nodes in a graph from text in the NodeScribe format.

        One line per node. Call get_format_docs() before the first time.

        Does not raise: a line that does not resolve becomes a red comment in
        the graph, and the rest keeps being created. The return says what
        happened.

        Args:
            graph: The graph to populate.
            text: The script in the NodeScribe format.
            replace: Omitted appends to what already exists. True erases the
                     graph before writing -- and **only if the current graph
                     comes back clean when read**. If there is a "this does not
                     come back the same" warning, or a data node nobody
                     consumes, the replacement is refused and nothing changes:
                     erasing from a text that lost something would destroy
                     precisely what the text could not say. There is no forced
                     mode; for that, use clear_graph.
        Returns:
            How many nodes went in, and one line per diagnostic.
        """
        return unreal.NodeScribeLibrary.write_graph(graph, text, bool(replace))

    @toolset_registry.tool_call
    @staticmethod
    def read_graph(graph: unreal.EdGraph) -> str:
        """Reads a whole graph as text in the NodeScribe format.

        The text comes back pasteable into write_graph. The header carries the
        asset, the graph, the graph's exact path (`refPath:`) and the declared
        variables. Warnings about what does not come back the same come out
        commented at the end.

        The path is `/Root/Folder/Asset.Asset:GraphName`. A plugin asset's root
        is the plugin's name (`/JoyShockLibrary4Unreal/...`), not `/Game/`, and
        the graph's name cannot be guessed -- `read_object` on the Blueprint
        lists the graphs it has.

        Args:
            graph: The graph to read.
        Returns:
            The script equivalent to the graph.
        """
        return unreal.NodeScribeLibrary.read_graph(graph)

    @toolset_registry.tool_call
    @staticmethod
    def clear_graph(graph: unreal.EdGraph) -> str:
        """Empties the graph, and returns as text what was in it.

        It is the missing gesture. write_graph's `replace` refuses to erase a
        graph the text cannot describe, and that refusal is right: what
        vanishes does not show up in what is left. But there are cases where
        the intent is precisely to throw it away -- the stubs a new Blueprint
        brings from the factory, an attempt that failed --, and there the
        refusal only forces someone to do by hand what the call would do.

        What changes compared to a forced mode: nothing vanishes silently. The
        graph comes back transcribed in the response, together with the
        reading's warnings -- including the warning that part of it did not
        fit in text. **Keep that return before writing over it.**

        Nodes the Engine marks as undeletable stay: an AnimGraph's Output Pose,
        a transition's Result, a function's entry.

        A single transaction: Ctrl+Z brings the whole graph back.

        Args:
            graph: The graph to empty.
        Returns:
            How many nodes left, and the text of what was there.
        """
        return unreal.NodeScribeLibrary.clear_graph(graph)

    @toolset_registry.tool_call
    @staticmethod
    def read_object(target: unreal.Object, filter: str | None = None) -> str:
        """Reads an object, class, CDO, actor or asset as a property sheet.

        One line per property, and only what differs from the factory value --
        in a typical CDO that is ~5% of them. The count at the end confirms the
        rest is at its default.

        There is no separate step for listing the schema. Looking for a
        specific property, pass the filter in this same call and the answer
        already comes with type and value. Never read everything to search
        afterwards.

        The target is an object path: `/Root/Folder/Asset.Asset`. A plugin
        asset's root is the plugin's name (`/JoyShockLibrary4Unreal/...`), not
        `/Game/`.

        Blackboards come out as `key Name : Type` lines, and Behavior Trees as
        an indented tree.

        Args:
            target: The object to read. Blueprints and classes become their CDO.
            filter: Omitted returns what changed. With text, returns the
                    properties whose name contains that text.
        Returns:
            The sheet.
        """
        return unreal.NodeScribeLibrary.read_object(target, filter or '')

    @toolset_registry.tool_call
    @staticmethod
    def write_object(target: unreal.Object, text: str) -> str:
        """Applies a property sheet to an object, class, CDO or asset.

        Same format as read_object. The text is a **list of changes**, not the
        final state: pasting a whole sheet back touches nothing beyond what the
        lines say. Send only the lines that change.

        `Name = default` returns the property to its factory value.
        An indented block under `Component:` or under `Struct:` reaches inside
        them.

        `variable Name : Type [editable] [= value]` creates a Blueprint
        variable, and `delete variable Name` removes one. It never creates
        components. A line that does not resolve becomes a diagnostic with the
        similar names, and the others are applied.

        Args:
            target: The object to change. Blueprints and classes become their CDO.
            text: The lines in the sheet format.
        Returns:
            How many changes were applied, and one line per diagnostic.
        """
        return unreal.NodeScribeLibrary.write_object(target, text)

    @toolset_registry.tool_call
    @staticmethod
    def create_asset(path: str, parent: str, options: str | None = None) -> str:
        """Creates an empty asset.

        It exists because the native toolset has duplicate, move and delete,
        and has no creation -- without this, every new asset depends on someone
        clicking.

        Never overwrites, and only creates inside /Game/. The asset stays
        dirty, unsaved, like any freshly created one in the editor.

        Some assets cannot be created from the type alone: an AnimBlueprint
        needs to know the skeleton, and so does a BlendSpace. That is what
        `options` is for -- and it cannot be left for later, because on the
        finished asset the skeleton is read-only. When the type needs
        configuration and nothing comes in `options`, the answer carries a
        note saying what was left blank.

            create_asset('/Game/Anims/ABP_Sophia', 'AnimBlueprint',
                         'TargetSkeleton = /Game/MetaHumans/.../metahuman_base_skel')

        Args:
            path: Where to create it, with its name: '/Game/MyGame/Tests/BTTask_Foo'.
            parent: The type, by display name: 'BTTask_BlueprintBase',
                    'GameplayEffect', 'BlackboardData', 'AnimBlueprint',
                    'BlendSpace', 'BlendSpace1D'.
            options: Factory properties, in the sheet format, one per line. If
                     any is not accepted, nothing is created.
        Returns:
            The path of what was created, or why it failed.
        """
        return unreal.NodeScribeLibrary.create_asset(path, parent, options or '')

    @toolset_registry.tool_call
    @staticmethod
    def write_blendspace(blend_space: unreal.BlendSpace, text: str) -> str:
        """Fills a BlendSpace: the axes and the samples.

        It exists because the sheet does not reach. `SampleData` and
        `BlendParameters` are struct arrays, and writing into them by hand
        would skip the Engine's validation, which is what rebuilds the
        interpolation grid. Without the grid the BlendSpace exists, opens,
        shows the points and interpolates nothing.

        One line per sample. The axes are applied before the samples, whatever
        comes first in the text -- a sample outside the range is refused, and
        setting the range afterwards does not bring it back.

            axis X : Speed = 0 .. 600
            MM_Idle = 0
            MF_Unarmed_Walk_Fwd = 300
            MF_Unarmed_Jog_Fwd = 600

        In two dimensions the Y axis goes in the same way and the sample gets
        the second position: `MF_Unarmed_Walk_Fwd = 0, 300`.

        It does not erase what is already there: the text is a list of changes.

        Args:
            blend_space: The asset to fill.
            text: The axis and sample lines.
        Returns:
            How many samples and axes went in, and one line per problem.
        """
        return unreal.NodeScribeLibrary.write_blend_space(blend_space, text)

    @toolset_registry.tool_call
    @staticmethod
    def read_tags(filter: str | None = None) -> str:
        """Reads the declared Gameplay Tags, one per line.

        Args:
            filter: Omitted brings them all. With text, only the ones containing it.
        Returns:
            One `tag Name` line per tag, with the count in the header.
        """
        return unreal.NodeScribeLibrary.read_tags(filter or '')

    @toolset_registry.tool_call
    @staticmethod
    def write_tags(text: str, source: str | None = None) -> str:
        """Creates Gameplay Tags, one per line.

        It exists because a tag is neither an asset nor a property -- it lives
        in an ini --, so neither create_asset nor write_object reaches it.
        Without this, every new tag depends on someone opening the settings
        window.

        Same format as read_tags: accepts `tag X` or just `X`, and the
        reading's header is ignored. An already declared tag is skipped without
        error. **It neither deletes nor renames** -- both break every asset
        that uses the tag.

        Args:
            text: One tag per line.
            source: The target ini, by display name: 'MyGame.ini'. Omitted lets
                    the Engine choose, which gives 'DefaultGameplayTags.ini' --
                    which may not be where the project's other tags live. A
                    source that does not exist is refused with the list of the
                    ones that do.
        Returns:
            How many were created and in which file, and one line per diagnostic.
        """
        return unreal.NodeScribeLibrary.write_tags(text, source or '')

    @toolset_registry.tool_call
    @staticmethod
    def save_all_and_quit() -> str:
        """Saves everything and closes the editor.

        It is for recompiling the plugin without depending on someone clicking
        the X -- Unreal holds the binaries while it is open.

        Refuses if Play In Editor is running. After this the MCP connection
        drops; reopening the editor is done from outside.

        Returns:
            What was saved, or why it did not close.
        """
        return unreal.NodeScribeLibrary.save_all_and_quit()

    @toolset_registry.tool_call
    @staticmethod
    def get_format_docs() -> str:
        """Returns the NodeScribe format specification.

        Call once per session, before writing a graph for the first time.
        """
        return unreal.NodeScribeLibrary.get_format_docs()
