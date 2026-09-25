#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NodeScribeLibrary.generated.h"

class UBlendSpace;
class UEdGraph;

/**
 * NodeScribe's surface for outside callers: Python, MCP, automation.
 *
 * It exists because of cost, not capability. An agent that builds a graph
 * through the conventional MCP spends most of its tokens *discovering* node
 * names -- one call per type, each returning dozens of identifiers.
 * NodeScribe's catalog resolves `Print String` locally, so a whole graph fits
 * in two calls and a few hundred tokens.
 */
UCLASS()
class NODESCRIBEEDITOR_API UNodeScribeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Transcribes text in the NodeScribe format into real nodes in the graph.
	 *
	 * @param bReplace  true erases what is already in the graph before writing,
	 *                  instead of appending. **Only if the current graph comes
	 *                  back clean when read** -- if there is any "this does not
	 *                  come back the same" warning, or a data node nobody
	 *                  consumes, the replacement is refused and nothing changes.
	 *                  Erasing from a text that lost something would destroy
	 *                  precisely what the text could not say.
	 *
	 * @return Diagnostics as text, one line per message. Empty = all good.
	 *         Never throws: a line that does not resolve becomes a red comment
	 *         in the graph and a line here, and the rest of the text keeps being
	 *         created.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteGraph(UEdGraph* Graph, const FString& Text, bool bReplace = false);

	/** Reads the whole graph back as text in the same format. */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadGraph(UEdGraph* Graph);

	/**
	 * Empties the graph, and **returns as text what was in it**.
	 *
	 * It is the missing gesture. `WriteGraph`'s replacement refuses to erase a
	 * graph the text cannot describe, and that refusal is right: what vanishes
	 * does not show up in what is left. But there are cases where the intent is
	 * precisely to throw it away -- the stubs a new Blueprint brings from the
	 * factory, an attempt that failed --, and there the refusal only forces
	 * someone to do by hand what the call would do.
	 *
	 * What changes compared to a forced mode, which this plugin does not have:
	 * nothing vanishes silently. The graph comes back transcribed in the
	 * response, together with the reading's warnings -- including the warning
	 * that part of it did not fit in text. Whoever erased keeps what they erased.
	 *
	 * Nodes the Engine marks as undeletable stay: an AnimGraph's Output Pose, a
	 * transition's Result, a function's entry. They are the same ones the
	 * editor's Ctrl+A + Delete keeps.
	 *
	 * A single transaction: Ctrl+Z brings the whole graph back.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ClearGraph(UEdGraph* Graph);

	/**
	 * An object as a sheet: one line per property, only what differs from the
	 * default.
	 *
	 * It exists for the same reason as the rest. Listing a Character's
	 * properties the conventional way returns the class's whole JSON schema --
	 * around 10,000 tokens, and only the shape, without any value; the values
	 * need a second call. The sheet answers both at once, because 95% of the
	 * properties are at their factory value and the factory value is resolved
	 * on this side.
	 *
	 * @param Filter  empty returns what changed. With text, it returns the
	 *                properties whose name matches, with type and value -- which
	 *                is what makes the separate step of listing the schema
	 *                unnecessary.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadObject(UObject* Object, const FString& Filter);

	/**
	 * The mirror: applies a sheet.
	 *
	 * The text is a list of changes, not the final state -- pasting a whole sheet
	 * back touches nothing beyond what the lines say. `= default` returns the
	 * property to its factory value. `variable Name : Type` creates a Blueprint
	 * variable, and only `delete variable Name` removes one.
	 *
	 * It does not raise: a line that does not resolve becomes a diagnostic with
	 * the similar names, and the others keep being applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteObject(UObject* Object, const FString& Text);

	/**
	 * Creates an empty asset.
	 *
	 * It exists for capability, not savings: the Engine's native toolset has
	 * duplicate, move and delete, and has no creation. Without this, every new
	 * asset is a request for a person to click, and the rest of the work stops.
	 *
	 * @param Path     where to create it, with its name: `/Game/MyGame/Tests/BTTask_Foo`.
	 * @param Parent   the type, by display name: `BTTask_BlueprintBase`,
	 *                 `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 * @param Options  factory properties, in sheet format, applied before
	 *                 creating. Some assets cannot be created from the type
	 *                 alone: an AnimBlueprint needs to know the skeleton, and so
	 *                 does a BlendSpace. Fixing it afterwards does not work -- on
	 *                 the finished asset the skeleton is read-only, on purpose.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString CreateAsset(const FString& Path, const FString& Parent, const FString& Options);

	/**
	 * Fills a BlendSpace: the axes and the samples, from text.
	 *
	 * It exists because the sheet does not reach. `SampleData` and
	 * `BlendParameters` are struct arrays, and writing into them by hand would
	 * skip the Engine's validation -- which is what rebuilds the interpolation
	 * grid. Without the grid the BlendSpace exists, opens, shows the points and
	 * interpolates nothing.
	 *
	 *     axis X : Speed = 0 .. 600
	 *     MM_Idle = 0
	 *     MF_Unarmed_Walk_Fwd = 300
	 *
	 * It does not erase what is already there: the text is a list of changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteBlendSpace(UBlendSpace* BlendSpace, const FString& Text);

	/**
	 * The declared Gameplay Tags, one per line.
	 *
	 * @param Filter  empty brings them all; with text, only the ones containing it.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString ReadTags(const FString& Filter);

	/**
	 * The mirror: creates the text's tags, one per line.
	 *
	 * It exists for capability, not savings -- a tag is neither an asset nor a
	 * property, it lives in an ini, and without this it is only born when
	 * someone opens the settings window. A new cooldown needs the tag to exist
	 * before the Gameplay Effect can grant it.
	 *
	 * An already declared tag is skipped without error. It neither deletes nor
	 * renames.
	 *
	 * @param Source  the target ini, by display name (`BossRush.ini`). Empty
	 *                lets the Engine choose, which gives
	 *                `DefaultGameplayTags.ini`. A source that does not exist is
	 *                refused with the list of the ones that do.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString WriteTags(const FString& Text, const FString& Source);

	/** The format's specification, for whoever has never seen it. */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString GetFormatDocs();

	/**
	 * Saves everything and closes the editor.
	 *
	 * It exists because recompiling the plugin requires the editor closed, and
	 * without this every fix cycle stops waiting for someone to click the X.
	 *
	 * It refuses while Play In Editor is running: closing in the middle of a
	 * test is a surprise, and the time saved does not pay for it. The shutdown is
	 * deferred for a moment so this response can get out before the connection
	 * drops.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeScribe")
	static FString SaveAllAndQuit();
};
