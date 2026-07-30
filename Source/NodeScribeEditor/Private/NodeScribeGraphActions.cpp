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
	 * Cada editor de Blueprint tem a propria barra. O de Widget nao herda a do
	 * Blueprint comum, entao registrar so' numa deixaria o botao invisivel
	 * exatamente onde este plugin comecou a ser usado.
	 */
	const TCHAR* const ToolbarNames[] = {
		TEXT("AssetEditor.BlueprintEditor.ToolBar"),
		TEXT("AssetEditor.WidgetBlueprintEditor.ToolBar"),
		TEXT("AssetEditor.AnimationBlueprintEditor.ToolBar")
	};

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

		// Link em vez de abrir sozinho: `FMessageLog::Open` traz para a frente a
		// aba onde o log foi ancorado da primeira vez, que costuma ser dentro de
		// OUTRO editor de asset. O efeito era o editor pular de Blueprint no meio
		// do trabalho -- barulho que nao vale o atalho.
		if (bOfferLog)
		{
			Info.HyperlinkText = LOCTEXT("OpenLog", "Ver detalhes no Message Log");
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
	 * Diagnosticos vao para o Message Log, e nao para um toast so'.
	 *
	 * Um toast some em cinco segundos; se o aviso importava, ele tinha que
	 * continuar acessivel depois. O log abre sozinho quando algo pede atencao e
	 * fica quieto quando esta' tudo certo.
	 */
	void Report(
		const TArray<FNodeScribeDiagnostic>& Diagnostics,
		int32 ErrorCount,
		int32 WarningCount,
		const FText& Summary,
		const FString& SourceLabel = FString())
	{
		FMessageLog Log(FNodeScribeGraphActions::LogListingName);

		// O canal do Message Log e' um so' para o editor inteiro, e a aba dele
		// fica ancorada onde foi aberta pela primeira vez -- pode ser outro
		// asset. Nao da' para mover a aba, entao a pagina diz de onde veio.
		Log.NewPage(SourceLabel.IsEmpty()
			? Summary
			: FText::Format(NSLOCTEXT("NodeScribe", "LogPage", "{0}  -  {1}"),
				FText::FromString(SourceLabel), Summary));

		for (const FNodeScribeDiagnostic& Diagnostic : Diagnostics)
		{
			const FString Prefix = (Diagnostic.Line > 0)
				? FString::Printf(TEXT("linha %d: "), Diagnostic.Line)
				: FString();

			Log.Message(ToMessageSeverity(Diagnostic.Severity), FText::FromString(Prefix + Diagnostic.Message));
		}

		ShowToast(Summary, ErrorCount == 0, Diagnostics.Num() > 0);
	}

	// --- Acoes -----------------------------------------------------------

	void CopyToClipboard(const FNodeScribeReader::FResult& Result, const FText& SourceDescription)
	{
		if (Result.NodeCount == 0)
		{
			Report(Result.Diagnostics, Result.ErrorCount, Result.WarningCount,
				LOCTEXT("ReadNothing", "Nada para copiar."));
			return;
		}

		FPlatformApplicationMisc::ClipboardCopy(*Result.Text);

		const FText Summary = FText::Format(
			LOCTEXT("ReadDone", "{0} node(s) de {1} copiados como texto. Cole onde quiser."),
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
			ShowToast(LOCTEXT("NoSelection", "Nenhum node selecionado."), false);
			return;
		}

		CopyToClipboard(
			FNodeScribeReader::Read(Selected, Editor->GetBlueprintObj()),
			LOCTEXT("SourceSelection", "seleção"));
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
			ShowToast(LOCTEXT("NoGraph", "Nenhum grafo aberto."), false);
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
			ShowToast(LOCTEXT("NoGraph", "Nenhum grafo aberto."), false);
			return;
		}

		FString ClipboardText;
		FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

		if (ClipboardText.TrimStartAndEnd().IsEmpty())
		{
			ShowToast(LOCTEXT("EmptyClipboard", "O clipboard esta' vazio."), false);
			return;
		}

		// O formato interno da Unreal comeca assim. Mandar isso para o parser do
		// NodeScribe daria um monte de erro sem sentido; o Ctrl+V normal do
		// grafo ja' resolve esse caso melhor do que nos.
		if (ClipboardText.StartsWith(TEXT("BEGIN OBJECT")))
		{
			ShowToast(LOCTEXT("UnrealClipboard",
				"Isso e' o formato interno da Unreal, nao texto do NodeScribe. Use Ctrl+V normal no grafo."), false);
			return;
		}

		TArray<FNodeScribeDiagnostic> ParseDiagnostics;
		const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(ClipboardText, ParseDiagnostics);

		const FScopedTransaction Transaction(LOCTEXT("PasteTransaction", "NodeScribe: colar nodes"));
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

		// Os nodes entram abaixo de tudo que ja' existe, o que costuma ser fora
		// da tela. Sem pular ate' la', clicar em Colar parece nao ter feito nada.
		if (Result.CreatedNodes.Num() > 0)
		{
			Editor->JumpToNode(Result.CreatedNodes[0], false);
		}

		const FText Summary = (Result.ErrorCount > 0)
			? FText::Format(
				LOCTEXT("PasteWithErrors", "{0} node(s) colados, mas {1} linha(s) nao resolveram."),
				FText::AsNumber(Result.CreatedNodes.Num()), FText::AsNumber(Result.ErrorCount))
			: FText::Format(
				LOCTEXT("PasteDone", "{0} node(s) colados. Ctrl+Z desfaz."),
				FText::AsNumber(Result.CreatedNodes.Num()));

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

	for (const TCHAR* ToolbarName : ToolbarNames)
	{
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(FName(ToolbarName));
		if (!Toolbar)
		{
			continue;
		}

		FToolMenuSection& Section = Toolbar->AddSection(
			"NodeScribe",
			LOCTEXT("SectionLabel", "NodeScribe"),
			FToolMenuInsert(NAME_None, EToolMenuInsertType::Last));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribePaste",
			FToolUIActionChoice(MakeAction(&ExecutePaste)),
			LOCTEXT("PasteLabel", "Colar"),
			LOCTEXT("PasteTooltip",
				"Le' o texto do NodeScribe que esta' no clipboard e cria os nodes neste grafo. Ctrl+Z desfaz."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste")));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribeCopySelected",
			FToolUIActionChoice(MakeAction(&ExecuteCopySelected)),
			LOCTEXT("CopySelectedLabel", "Copiar selecionado"),
			LOCTEXT("CopySelectedTooltip",
				"Transcreve os nodes selecionados para texto e poe no clipboard. Nao altera o grafo."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy")));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"NodeScribeCopyGraph",
			FToolUIActionChoice(MakeAction(&ExecuteCopyGraph)),
			LOCTEXT("CopyGraphLabel", "Copiar grafo inteiro"),
			LOCTEXT("CopyGraphTooltip",
				"Transcreve o grafo aberto inteiro para texto e poe no clipboard. Nao altera o grafo."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.SelectAll")));
	}
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
