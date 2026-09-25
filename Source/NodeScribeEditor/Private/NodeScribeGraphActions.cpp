#include "NodeScribeGraphActions.h"

#include "NodeScribeBuilder.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeParser.h"
#include "NodeScribeReader.h"
#include "NodeScribeTarget.h"
#include "NodeScribeTypes.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

const FName FNodeScribeGraphActions::LogListingName("NodeScribe");

namespace
{
	const FName MenuOwner("NodeScribe");

	/**
	 * The toolbar every asset editor inherits.
	 *
	 * Listing the editors one by one does not scale: each kind of Blueprint has
	 * its own toolbar -- Widget, Animation, Gameplay Ability, and the ones yet to
	 * exist. Registering on the parent, the section reaches all of them; the
	 * section is dynamic and does not show up where the context has no
	 * Blueprint editor, so a texture editor stays without any button.
	 */
	const TCHAR* const SharedToolbarName = TEXT("AssetEditor.DefaultToolBar");

	FBlueprintEditor* FindBlueprintEditor(const FToolMenuContext& Context)
	{
		UAssetEditorToolkitMenuContext* ToolkitContext = Context.FindContext<UAssetEditorToolkitMenuContext>();
		if (!ToolkitContext)
		{
			return nullptr;
		}

		TSharedPtr<FAssetEditorToolkit> Toolkit = ToolkitContext->Toolkit.Pin();
		if (!Toolkit.IsValid() || !Toolkit->IsBlueprintEditor())
		{
			return nullptr;
		}

		return static_cast<FBlueprintEditor*>(Toolkit.Get());
	}

	void ShowToast(const FText& Message, bool bSuccess, bool bOfferLog = false)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;

		// A link instead of opening it directly: `FMessageLog::Open` brings to the
		// front the tab where the log was docked the first time, which is usually
		// inside ANOTHER asset editor. The effect was the editor jumping to another
		// Blueprint in the middle of the work -- noise not worth the shortcut.
		if (bOfferLog)
		{
			Info.HyperlinkText = LOCTEXT("OpenLog", "See details in the Message Log");
			Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
			{
				FMessageLog(FNodeScribeGraphActions::LogListingName).Open(EMessageSeverity::Info, true);
			});
		}

		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	EMessageSeverity::Type ToMessageSeverity(ENodeScribeSeverity Severity)
	{
		switch (Severity)
		{
		case ENodeScribeSeverity::Error:   return EMessageSeverity::Error;
		case ENodeScribeSeverity::Warning: return EMessageSeverity::Warning;
		default:                           return EMessageSeverity::Info;
		}
	}

	/**
	 * Diagnostics go to the Message Log, not only to a toast.
	 *
	 * A toast vanishes in five seconds; if the warning mattered, it had to stay
	 * reachable afterwards. The log is offered when something needs attention
	 * and stays quiet when everything is fine.
	 */
	void Report(
		const TArray<FNodeScribeDiagnostic>& Diagnostics,
		int32 ErrorCount,
		int32 WarningCount,
		const FText& Summary,
		const FString& SourceLabel = FString())
	{
		FMessageLog Log(FNodeScribeGraphActions::LogListingName);

		// The Message Log channel is a single one for the whole editor, and its
		// tab stays docked where it was first opened -- which may be another
		// asset. The tab cannot be moved, so the page says where it came from.
		Log.NewPage(SourceLabel.IsEmpty()
			? Summary
			: FText::Format(NSLOCTEXT("NodeScribe", "LogPage", "{0}  -  {1}"),
				FText::FromString(SourceLabel), Summary));

		for (const FNodeScribeDiagnostic& Diagnostic : Diagnostics)
		{
			const FString Prefix = (Diagnostic.Line > 0)
				? FString::Printf(TEXT("line %d: "), Diagnostic.Line)
				: FString();

			Log.Message(ToMessageSeverity(Diagnostic.Severity), FText::FromString(Prefix + Diagnostic.Message));
		}

		ShowToast(Summary, ErrorCount == 0, Diagnostics.Num() > 0);
	}

	// --- Actions ---------------------------------------------------------

	void CopyToClipboard(const FNodeScribeReader::FResult& Result, const FText& SourceDescription)
	{
		if (Result.NodeCount == 0)
		{
			Report(Result.Diagnostics, Result.ErrorCount, Result.WarningCount,
				LOCTEXT("ReadNothing", "Nothing to copy."));
			return;
		}

		FPlatformApplicationMisc::ClipboardCopy(*Result.Text);

		const FText Summary = FText::Format(
			LOCTEXT("ReadDone", "{0} node(s) from {1} copied as text. Paste them anywhere."),
			FText::AsNumber(Result.NodeCount), SourceDescription);

		Report(Result.Diagnostics, Result.ErrorCount, Result.WarningCount, Summary);
	}

	void ExecuteCopySelected(const FToolMenuContext& Context)
	{
		FBlueprintEditor* Editor = FindBlueprintEditor(Context);
		if (!Editor)
		{
			return;
		}

		TArray<UEdGraphNode*> Selected;
		for (UObject* Object : Editor->GetSelectedNodes())
		{
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
			{
				Selected.Add(Node);
			}
		}

		if (Selected.Num() == 0)
		{
			ShowToast(LOCTEXT("NoSelection", "No node selected."), false);
			return;
		}

		CopyToClipboard(
			FNodeScribeReader::Read(Selected, Editor->GetBlueprintObj()),
			LOCTEXT("SourceSelection", "the selection"));
	}

	void ExecuteCopyGraph(const FToolMenuContext& Context)
	{
		FBlueprintEditor* Editor = FindBlueprintEditor(Context);
		if (!Editor)
		{
			return;
		}

		UEdGraph* Graph = Editor->GetFocusedGraph();
		if (!Graph)
		{
			ShowToast(LOCTEXT("NoGraph", "No graph open."), false);
			return;
		}

		CopyToClipboard(
			FNodeScribeReader::ReadGraph(Graph, Editor->GetBlueprintObj()),
			FText::FromString(Graph->GetName()));
	}

	void ExecutePaste(const FToolMenuContext& Context)
	{
		FBlueprintEditor* Editor = FindBlueprintEditor(Context);
		if (!Editor)
		{
			return;
		}

		UEdGraph* Graph = Editor->GetFocusedGraph();
		UBlueprint* Blueprint = Editor->GetBlueprintObj();

		if (!Graph || !Blueprint)
		{
			ShowToast(LOCTEXT("NoGraph", "No graph open."), false);
			return;
		}

		FString ClipboardText;
		FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

		if (ClipboardText.TrimStartAndEnd().IsEmpty())
		{
			ShowToast(LOCTEXT("EmptyClipboard", "The clipboard is empty."), false);
			return;
		}

		// Unreal's internal format starts like this. Sending it to the NodeScribe
		// parser would give a pile of meaningless errors; the graph's regular
		// Ctrl+V already handles that case better than we do.
		if (ClipboardText.StartsWith(TEXT("BEGIN OBJECT")))
		{
			ShowToast(LOCTEXT("UnrealClipboard",
				"That is Unreal's internal format, not NodeScribe text. Use the regular Ctrl+V in the graph."), false);
			return;
		}

		TArray<FNodeScribeDiagnostic> ParseDiagnostics;
		const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(ClipboardText, ParseDiagnostics);

		const FScopedTransaction Transaction(LOCTEXT("PasteTransaction", "NodeScribe: paste nodes"));
		Blueprint->Modify();
		Graph->Modify();

		FNodeScribeBuilder::FResult Result = FNodeScribeBuilder::Build(
			Statements, Graph, Blueprint, FNodeScribeTarget::FindFreeOrigin(Graph));

		Result.Diagnostics.Insert(ParseDiagnostics, 0);
		for (const FNodeScribeDiagnostic& Diagnostic : ParseDiagnostics)
		{
			if (Diagnostic.Severity == ENodeScribeSeverity::Error)
			{
				++Result.ErrorCount;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		// The nodes go in below everything that already exists, which is usually
		// off screen. Without jumping there, clicking Paste looks like it did
		// nothing.
		if (Result.CreatedNodes.Num() > 0)
		{
			Editor->JumpToNode(Result.CreatedNodes[0], false);
		}

		// The empty-pin warning used to stay only in the log, and the log is
		// where nobody looks before pressing Play. A forgotten `?` on an object
		// pin compiles and only blows up at runtime, so it needs to show up here.
		//
		// The count is of warnings, not of pins: an empty pin is one kind among
		// several (a link of the wrong type is another), and calling them all
		// "pins waiting for your choice" said something that was not true.
		FText Summary;

		if (Result.ErrorCount > 0)
		{
			Summary = FText::Format(
				LOCTEXT("PasteWithErrors", "{0} node(s) pasted, but {1} line(s) did not resolve."),
				FText::AsNumber(Result.CreatedNodes.Num()), FText::AsNumber(Result.ErrorCount));
		}
		else if (Result.WarningCount > 0)
		{
			Summary = FText::Format(
				LOCTEXT("PasteWithWarnings", "{0} node(s) pasted, with {1} warning(s) to check before pressing Play."),
				FText::AsNumber(Result.CreatedNodes.Num()), FText::AsNumber(Result.WarningCount));
		}
		else
		{
			Summary = FText::Format(
				LOCTEXT("PasteDone", "{0} node(s) pasted. Ctrl+Z undoes it."),
				FText::AsNumber(Result.CreatedNodes.Num()));
		}

		Report(Result.Diagnostics, Result.ErrorCount, Result.WarningCount, Summary,
			FString::Printf(TEXT("%s -> %s"), *Blueprint->GetName(), *Graph->GetName()));
	}

	bool HasGraph(const FToolMenuContext& Context)
	{
		const FBlueprintEditor* Editor = FindBlueprintEditor(Context);
		return Editor && Editor->GetFocusedGraph() != nullptr;
	}

	FToolUIAction MakeAction(void (*Execute)(const FToolMenuContext&))
	{
		FToolUIAction Action;
		Action.ExecuteAction = FToolMenuExecuteAction::CreateStatic(Execute);
		Action.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&HasGraph);
		return Action;
	}

	FDelegateHandle StartupCallbackHandle;
}

// ---------------------------------------------------------------------------

void FNodeScribeGraphActions::RegisterStartupHook()
{
	StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FNodeScribeGraphActions::RegisterToolbar));
}

void FNodeScribeGraphActions::RegisterToolbar()
{
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		FMessageLogInitializationOptions Options;
		Options.bShowPages = true;
		Options.bShowFilters = true;
		MessageLogModule.RegisterLogListing(LogListingName, LOCTEXT("LogLabel", "NodeScribe"), Options);
	}

	FToolMenuOwnerScoped OwnerScoped(MenuOwner);

	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(FName(SharedToolbarName));
	if (!Toolbar)
	{
		return;
	}

	// Dynamic: built at draw time, when it is already possible to ask whether
	// the context is a Blueprint editor. Without this the section would show up
	// empty in the texture editor, the sound editor, everywhere.
	Toolbar->AddDynamicSection(
		"NodeScribe",
		FNewSectionConstructChoice(FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		if (!FindBlueprintEditor(InMenu->Context))
		{
			return;
		}

		FToolMenuSection& Section = InMenu->AddSection(
			"NodeScribe",
			LOCTEXT("SectionLabel", "NodeScribe"),
			FToolMenuInsert(NAME_None, EToolMenuInsertType::Last));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribePaste",
			FToolUIActionChoice(MakeAction(&ExecutePaste)),
			LOCTEXT("PasteLabel", "Paste"),
			LOCTEXT("PasteTooltip",
				"Reads the NodeScribe text in the clipboard and creates the nodes in this graph. Ctrl+Z undoes it."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste")));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribeCopySelected",
			FToolUIActionChoice(MakeAction(&ExecuteCopySelected)),
			LOCTEXT("CopySelectedLabel", "Copy Selected"),
			LOCTEXT("CopySelectedTooltip",
				"Transcribes the selected nodes to text and puts it in the clipboard. Does not change the graph."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy")));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribeCopyGraph",
			FToolUIActionChoice(MakeAction(&ExecuteCopyGraph)),
			LOCTEXT("CopyGraphLabel", "Copy Whole Graph"),
			LOCTEXT("CopyGraphTooltip",
				"Transcribes the whole open graph to text and puts it in the clipboard. Does not change the graph."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.SelectAll")));
	})));
}

void FNodeScribeGraphActions::Unregister()
{
	if (StartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
		StartupCallbackHandle.Reset();
	}

	if (UObjectInitialized())
	{
		UToolMenus::UnregisterOwner(MenuOwner);
	}
}

#undef LOCTEXT_NAMESPACE
