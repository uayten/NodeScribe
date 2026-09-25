#include "NodeScribeLibrary.h"

#include "NodeScribeAssetMaker.h"
#include "NodeScribeBlendSpace.h"
#include "NodeScribeBuilder.h"
#include "NodeScribeObjectReader.h"
#include "NodeScribeObjectWriter.h"
#include "NodeScribeParser.h"
#include "NodeScribeReader.h"
#include "NodeScribeTags.h"
#include "NodeScribeTarget.h"
#include "NodeScribeTypes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace
{
	const TCHAR* SeverityLabel(ENodeScribeSeverity Severity)
	{
		switch (Severity)
		{
		case ENodeScribeSeverity::Error:   return TEXT("error");
		case ENodeScribeSeverity::Warning: return TEXT("warning");
		default:                           return TEXT("note");
		}
	}

	/**
	 * Diagnostics as plain text.
	 *
	 * An outside caller sees neither the Message Log nor the red comments in the
	 * graph, so everything the plugin refused has to come back here -- it is the
	 * only channel that caller has.
	 */
	FString FormatDiagnostics(const TArray<FNodeScribeDiagnostic>& Diagnostics)
	{
		TArray<FString> Lines;
		Lines.Reserve(Diagnostics.Num());

		for (const FNodeScribeDiagnostic& Diagnostic : Diagnostics)
		{
			Lines.Add(Diagnostic.Line > 0
				? FString::Printf(TEXT("line %d [%s]: %s"),
					Diagnostic.Line, SeverityLabel(Diagnostic.Severity), *Diagnostic.Message)
				: FString::Printf(TEXT("[%s]: %s"),
					SeverityLabel(Diagnostic.Severity), *Diagnostic.Message));
		}

		return FString::Join(Lines, TEXT("\n"));
	}
}

/**
 * Deletes what can be deleted from the graph, and says how many.
 *
 * A function entry node refuses to be deleted (`CanUserDeleteNode`), and
 * rightly so: it is born with the function. Skipping those is the right
 * behaviour, not a limitation.
 */
static int32 RemoveDeletableNodes(UEdGraph* Graph, UBlueprint* Blueprint)
{
	TArray<UEdGraphNode*> ToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->CanUserDeleteNode())
		{
			ToRemove.Add(Node);
		}
	}

	for (UEdGraphNode* Node : ToRemove)
	{
		FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);
	}

	return ToRemove.Num();
}

FString UNodeScribeLibrary::WriteGraph(UEdGraph* Graph, const FString& Text, bool bReplace)
{
	if (!Graph)
	{
		return TEXT("[error]: no graph given.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return TEXT("[error]: this graph does not belong to a Blueprint.");
	}

	// The replacement guard, before any change.
	//
	// Erasing is the only thing this plugin does that cannot be checked
	// afterwards: what vanished does not show up in the text that is left. So
	// the deciding question is not "is the new text good", but "does the graph
	// that is there fit in text". If its reading loses something -- a
	// reconvergence, a Cast with a continuation, a node the format cannot name,
	// a loose data node --, erasing destroys exactly what nobody has written.
	//
	// There is no forced mode, on purpose. Whoever really wants to clear selects
	// everything in the graph and presses Delete: a human gesture, visible, with
	// Ctrl+Z right there. Or calls ClearGraph, which hands back what it erased.
	if (bReplace)
	{
		const FNodeScribeReader::FResult Current = FNodeScribeReader::ReadGraph(Graph, Blueprint);

		if (Current.WarningCount > 0 || Current.LostNodeCount > 0)
		{
			TArray<FString> Reasons;
			for (const FNodeScribeDiagnostic& Diagnostic : Current.Diagnostics)
			{
				if (Diagnostic.Severity == ENodeScribeSeverity::Warning)
				{
					Reasons.Add(TEXT("  - ") + Diagnostic.Message);
				}
			}

			if (Current.LostNodeCount > 0)
			{
				Reasons.Add(FString::Printf(
					TEXT("  - %d data node(s) feed nothing, and would not come back."),
					Current.LostNodeCount));
			}

			return FString::Printf(
				TEXT("[error]: did not replace this graph -- it has things the text cannot say, ")
				TEXT("and erasing would destroy exactly those. Nothing was changed.\n%s\n")
				TEXT("Write without `replace`, or delete by hand what you want to swap (Ctrl+A, Delete in the graph) and write afterwards."),
				*FString::Join(Reasons, TEXT("\n")));
		}
	}

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(Text, ParseDiagnostics);

	const FScopedTransaction Transaction(LOCTEXT("WriteGraphTransaction", "NodeScribe: write graph"));
	Blueprint->Modify();
	Graph->Modify();

	// Inside the transaction: Ctrl+Z undoes the erase and the write at once.
	int32 Removed = 0;
	if (bReplace)
	{
		Removed = RemoveDeletableNodes(Graph, Blueprint);
	}

	FNodeScribeBuilder::FResult Result = FNodeScribeBuilder::Build(
		Statements, Graph, Blueprint, FNodeScribeTarget::FindFreeOrigin(Graph));

	Result.Diagnostics.Insert(ParseDiagnostics, 0);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	const FString Report = FormatDiagnostics(Result.Diagnostics);

	// The count goes along even when there is no diagnostic: the caller is not
	// looking at the graph and needs to know something happened.
	const FString Deleted = Removed > 0
		? FString::Printf(TEXT("%d node(s) deleted, "), Removed)
		: FString();

	return FString::Printf(TEXT("%s%d node(s) created.%s%s"),
		*Deleted,
		Result.CreatedNodes.Num(),
		Report.IsEmpty() ? TEXT("") : TEXT("\n"),
		*Report);
}

FString UNodeScribeLibrary::ClearGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("[error]: no graph given.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return TEXT("[error]: this graph does not belong to a Blueprint.");
	}

	// Read before erasing. This is what separates this gesture from a forced
	// mode: the graph comes back in the response, and what the reading could not
	// say comes back as a warning -- so whoever erased knows what they lost,
	// instead of finding out later.
	const FNodeScribeReader::FResult Before = FNodeScribeReader::ReadGraph(Graph, Blueprint);

	const FScopedTransaction Transaction(LOCTEXT("ClearGraphTransaction", "NodeScribe: clear graph"));
	Blueprint->Modify();
	Graph->Modify();

	const int32 Removed = RemoveDeletableNodes(Graph, Blueprint);

	if (Removed == 0)
	{
		return TEXT("The graph was already empty. Nothing was changed.");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%d node(s) deleted. What was there:"), Removed));
	Lines.Add(Before.Text.IsEmpty() ? TEXT("# (nothing the text could express)") : Before.Text);

	const FString Report = FormatDiagnostics(Before.Diagnostics);
	if (!Report.IsEmpty())
	{
		Lines.Add(Report);
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString UNodeScribeLibrary::ReadGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("[error]: no graph given.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);

	FNodeScribeReader::FResult Result = FNodeScribeReader::ReadGraph(Graph, Blueprint);

	const FString Report = FormatDiagnostics(Result.Diagnostics);
	if (Report.IsEmpty())
	{
		return Result.Text;
	}

	// The warnings go in commented out: the text stays pasteable back as it is.
	TArray<FString> Commented;
	Report.ParseIntoArrayLines(Commented);

	for (FString& Line : Commented)
	{
		Line.InsertAt(0, TEXT("# "));
	}

	return Result.Text + TEXT("\n\n") + FString::Join(Commented, TEXT("\n"));
}

FString UNodeScribeLibrary::ReadObject(UObject* Object, const FString& Filter)
{
	return FNodeScribeObjectReader::ReadObject(Object, Filter);
}

FString UNodeScribeLibrary::WriteObject(UObject* Object, const FString& Text)
{
	const FNodeScribeObjectWriter::FResult Result =
		FNodeScribeObjectWriter::WriteObject(Object, Text);

	const FString Report = FString::Join(Result.Diagnostics, TEXT("\n"));

	// The count goes along even without a diagnostic: the caller is not looking
	// at the details panel and needs to know something happened.
	return FString::Printf(TEXT("%d change(s) applied.%s%s"),
		Result.Applied,
		Report.IsEmpty() ? TEXT("") : TEXT("\n"),
		*Report);
}

FString UNodeScribeLibrary::CreateAsset(const FString& Path, const FString& Parent, const FString& Options)
{
	return FNodeScribeAssetMaker::CreateAsset(Path, Parent, Options);
}

FString UNodeScribeLibrary::WriteBlendSpace(UBlendSpace* BlendSpace, const FString& Text)
{
	const NodeScribeBlendSpace::FResult Result = NodeScribeBlendSpace::Write(BlendSpace, Text);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%d sample(s), %d axis/axes."),
		Result.SamplesAdded, Result.AxesSet));

	Lines.Append(Result.Diagnostics);

	return FString::Join(Lines, TEXT("\n"));
}

FString UNodeScribeLibrary::ReadTags(const FString& Filter)
{
	return FNodeScribeTags::ReadTags(Filter);
}

FString UNodeScribeLibrary::WriteTags(const FString& Text, const FString& Source)
{
	return FNodeScribeTags::WriteTags(Text, Source);
}

FString UNodeScribeLibrary::SaveAllAndQuit()
{
	if (!GEditor)
	{
		return TEXT("[error]: no editor.");
	}

	// Closing in the middle of a test is a surprise, and the time saved does not pay for it.
	if (GEditor->IsPlaySessionInProgress())
	{
		return TEXT("[error]: a Play In Editor session is running. Stop Play first.");
	}

	// The Slate path asks "are you sure?" in a modal dialog when this option is
	// on. Whoever calls this is a program: nobody would be there to click, and
	// the editor would hang with no explanation.
	if (GetDefault<UEditorPerProjectUserSettings>()->bConfirmEditorClose)
	{
		return TEXT("[error]: 'Confirm on Editor Close' is on -- closing would open a dialog\n")
			TEXT("only a human can close. Untick it in Editor Preferences > General > Loading & Saving.");
	}

	bool bNeededSaving = false;
	FEditorFileUtils::SaveDirtyPackages(
		/*bPromptUserToSave*/ false,
		/*bSaveMapPackages*/ true,
		/*bSaveContentPackages*/ true,
		/*bFastSave*/ false,
		/*bNotifyNoPackagesSaved*/ false,
		/*bCanBeDeclined*/ false,
		&bNeededSaving);

	// SaveDirtyPackages' return value is no proof, and neither is bNeededSaving.
	// They are two silent-loss paths:
	//
	// - InternalSavePackages only returns false when the user cancels ("Only
	//   cancel should return false", says the engine's comment). A package that
	//   failed to save -- read-only, locked in source control, error midway --
	//   returns success. And with bPromptUserToSave and bCanBeDeclined false
	//   there is no possible cancel, so the return value is always true.
	//
	// - If every dirty package is in FEditorFileUtils::PackagesNotSavedDuringSaveAll
	//   (the list of what the user unticked in some save dialog, which lasts the
	//   whole session), the function does not even try to save and still returns
	//   bNeededSaving = false.
	//
	// So we ask again who is still dirty, and then we know.
	TArray<UPackage*> StillDirty;
	FEditorFileUtils::GetDirtyWorldPackages(StillDirty);
	FEditorFileUtils::GetDirtyContentPackages(StillDirty);

	if (StillDirty.Num() > 0)
	{
		TArray<FString> Names;
		Names.Reserve(StillDirty.Num());
		for (const UPackage* Package : StillDirty)
		{
			Names.Add(Package->GetName());
		}
		Names.Sort();

		return FString::Printf(
			TEXT("[error]: did not close -- %d package(s) are still unsaved after the save:\n%s\n")
			TEXT("Save by hand (Ctrl+Shift+S) and see what the editor complains about."),
			Names.Num(),
			*FString::Join(Names, TEXT("\n")));
	}

	// QUIT_EDITOR skips the Slate shutdown: it goes straight to
	// UUnrealEdEngine::CloseEditor -> RequestEngineExit. The open asset editors
	// stay alive, and are only torn down after the main window already died --
	// with their preview scene pointing at destroyed things. That is the
	// AnimationBlueprintEditor crash. The engine itself warns, in
	// EditorServer.cpp, next to QUIT_EDITOR: "Don't call quit_editor directly
	// with slate".
	//
	// CLOSE_SLATE_MAINFRAME is the right door. It lands in
	// FMainFrameHandler::ShutDownEditor, which in the right order: closes the
	// asset editors (BroadcastEditorClose), turns off the autosave restore file
	// -- it was what made the editor offer "recover" on the next launch --,
	// saves the window position, and only then queues QUIT_EDITOR.
	//
	// Deferred because quitting here would drop the connection before this
	// response got out, and the caller would see a network error instead of the
	// confirmation.
	GEngine->DeferredCommands.Add(TEXT("CLOSE_SLATE_MAINFRAME"));

	return bNeededSaving
		? TEXT("Everything saved. Closing the editor.")
		: TEXT("Nothing pending to save. Closing the editor.");
}

FString UNodeScribeLibrary::GetFormatDocs()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeScribe"));
	if (!Plugin.IsValid())
	{
		return TEXT("[error]: could not find the NodeScribe plugin.");
	}

	const FString DocsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("FORMAT.md"));

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *DocsPath))
	{
		return FString::Printf(TEXT("[error]: could not read %s"), *DocsPath);
	}

	return Contents;
}

#undef LOCTEXT_NAMESPACE
