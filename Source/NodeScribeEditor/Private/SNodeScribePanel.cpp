#include "SNodeScribePanel.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeParser.h"
#include "NodeScribeTypes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationItem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace
{
	const TCHAR* const PlaceholderScript =
		TEXT("# Cole aqui a lista de nodes.\n")
		TEXT("# Uma linha = um node. Indente sob um rotulo para seguir um ramo.\n")
		TEXT("#\n")
		TEXT("# evento BeginPlay\n")
		TEXT("# pc = Get Player Controller\n")
		TEXT("# Branch (Condition = $bAtivo)\n")
		TEXT("#   verdadeiro:\n")
		TEXT("#     Print String (In String = \"ligado\")\n")
		TEXT("#   falso:\n")
		TEXT("#     Print String (In String = \"desligado\")\n");

	FLinearColor GetSeverityColor(ENodeScribeSeverity Severity)
	{
		switch (Severity)
		{
		case ENodeScribeSeverity::Error:   return FLinearColor(1.0f, 0.35f, 0.35f);
		case ENodeScribeSeverity::Warning: return FLinearColor(1.0f, 0.78f, 0.30f);
		default:                           return FLinearColor(0.70f, 0.70f, 0.70f);
		}
	}

	void ShowToast(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 4.0f;
		Info.bFireAndForget = true;

		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

void SNodeScribePanel::Construct(const FArguments& InArgs)
{
	ScriptText = FText::FromString(PlaceholderScript);
	SummaryText = LOCTEXT("Idle", "Cole o texto, confira o destino e clique em Inserir.");

	RefreshTargets();

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Destino ----------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TargetLabel", "Destino:"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(TargetCombo, SComboBox<TSharedPtr<FNodeScribeTarget>>)
				.OptionsSource(&Targets)
				.OnGenerateWidget(this, &SNodeScribePanel::OnGenerateTargetWidget)
				.OnSelectionChanged(this, &SNodeScribePanel::OnTargetSelected)
				[
					SNew(STextBlock)
					.Text(this, &SNodeScribePanel::GetSelectedTargetText)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Atualizar"))
				.ToolTipText(LOCTEXT("RefreshTip", "Reler quais grafos de Blueprint estao abertos."))
				.OnClicked(this, &SNodeScribePanel::OnRefreshClicked)
			]
		]

		// ---- Texto ------------------------------------------------------
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.Text(this, &SNodeScribePanel::GetScriptText)
			.OnTextChanged(this, &SNodeScribePanel::OnScriptTextChanged)
			.AllowMultiLine(true)
			.SelectAllTextWhenFocused(false)
			.AutoWrapText(false)
		]

		// ---- Botoes -----------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Insert", "Inserir no grafo"))
				.ToolTipText(LOCTEXT("InsertTip", "Cria os nodes direto no grafo escolhido. Ctrl+Z desfaz."))
				.IsEnabled(this, &SNodeScribePanel::CanRun)
				.OnClicked(this, &SNodeScribePanel::OnInsertClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Copy", "Copiar para clipboard"))
				.ToolTipText(LOCTEXT("CopyTip", "Nao escreve no asset. Monta os nodes e deixa no clipboard para voce colar com Ctrl+V onde quiser."))
				.IsEnabled(this, &SNodeScribePanel::CanRun)
				.OnClicked(this, &SNodeScribePanel::OnCopyClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "Limpar"))
				.OnClicked(this, &SNodeScribePanel::OnClearClicked)
			]
		]

		// ---- Resumo -----------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SNodeScribePanel::GetSummaryText)
			.AutoWrapText(true)
		]

		// ---- Diagnosticos -----------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SBox)
			.MaxDesiredHeight(220.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(DiagnosticList, SListView<TSharedPtr<FNodeScribeDiagnostic>>)
					.ListItemsSource(&Diagnostics)
					.OnGenerateRow(this, &SNodeScribePanel::OnGenerateDiagnosticRow)
					.SelectionMode(ESelectionMode::None)
				]
			]
		]
	];
}

// ---------------------------------------------------------------------------
// Destino
// ---------------------------------------------------------------------------

void SNodeScribePanel::RefreshTargets()
{
	// Preserva a escolha atual se o grafo continuar aberto.
	UEdGraph* PreviousGraph = SelectedTarget.IsValid() ? SelectedTarget->Graph.Get() : nullptr;

	Targets = FNodeScribeTarget::FindAllOpen();
	SelectedTarget.Reset();

	for (const TSharedPtr<FNodeScribeTarget>& Target : Targets)
	{
		if (PreviousGraph && Target->Graph.Get() == PreviousGraph)
		{
			SelectedTarget = Target;
			break;
		}
	}

	if (!SelectedTarget.IsValid() && Targets.Num() > 0)
	{
		SelectedTarget = Targets[0];
	}

	if (TargetCombo.IsValid())
	{
		TargetCombo->RefreshOptions();
		TargetCombo->SetSelectedItem(SelectedTarget);
	}
}

FReply SNodeScribePanel::OnRefreshClicked()
{
	// Tambem derruba o catalogo: e' a saida manual para quando voce acabou de
	// criar uma funcao ou variavel e o plugin ainda nao a conhece.
	FNodeScribeCatalog::Get().Invalidate();

	RefreshTargets();
	return FReply::Handled();
}

TSharedRef<SWidget> SNodeScribePanel::OnGenerateTargetWidget(TSharedPtr<FNodeScribeTarget> Target)
{
	return SNew(STextBlock).Text(Target.IsValid() ? Target->GetDisplayText() : FText::GetEmpty());
}

void SNodeScribePanel::OnTargetSelected(TSharedPtr<FNodeScribeTarget> Target, ESelectInfo::Type SelectInfo)
{
	SelectedTarget = Target;
}

FText SNodeScribePanel::GetSelectedTargetText() const
{
	if (SelectedTarget.IsValid() && SelectedTarget->IsValid())
	{
		return SelectedTarget->GetDisplayText();
	}

	return LOCTEXT("NoTarget", "Nenhum Blueprint aberto - abra um e clique em Atualizar");
}

bool SNodeScribePanel::HasUsableTarget() const
{
	return SelectedTarget.IsValid() && SelectedTarget->IsValid();
}

bool SNodeScribePanel::CanRun() const
{
	return HasUsableTarget() && !ScriptText.IsEmpty();
}

// ---------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------

void SNodeScribePanel::OnScriptTextChanged(const FText& NewText)
{
	ScriptText = NewText;
}

FText SNodeScribePanel::GetScriptText() const
{
	return ScriptText;
}

FReply SNodeScribePanel::OnClearClicked()
{
	ScriptText = FText::GetEmpty();
	Diagnostics.Reset();
	SummaryText = LOCTEXT("Cleared", "Limpo.");

	if (DiagnosticList.IsValid())
	{
		DiagnosticList->RequestListRefresh();
	}

	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Acoes
// ---------------------------------------------------------------------------

FReply SNodeScribePanel::OnInsertClicked()
{
	if (!HasUsableTarget())
	{
		return FReply::Handled();
	}

	UBlueprint* Blueprint = SelectedTarget->Blueprint.Get();
	UEdGraph* Graph = SelectedTarget->Graph.Get();

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(ScriptText.ToString(), ParseDiagnostics);

	const FScopedTransaction Transaction(LOCTEXT("InsertTransaction", "NodeScribe: inserir nodes"));
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

	PresentResult(Result, LOCTEXT("Inserted", "inseridos no grafo"));

	return FReply::Handled();
}

FReply SNodeScribePanel::OnCopyClicked()
{
	if (!HasUsableTarget())
	{
		return FReply::Handled();
	}

	UBlueprint* Blueprint = SelectedTarget->Blueprint.Get();

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(ScriptText.ToString(), ParseDiagnostics);

	// Grafo descartavel, mas com o Blueprint como outer: e' assim que os nodes de
	// variavel conseguem resolver `self`. Nada aqui e' salvo no asset.
	UEdGraph* ScratchGraph = NewObject<UEdGraph>(Blueprint, UEdGraph::StaticClass(), NAME_None, RF_Transient);
	ScratchGraph->Schema = UEdGraphSchema_K2::StaticClass();

	FNodeScribeBuilder::FResult Result = FNodeScribeBuilder::Build(
		Statements, ScratchGraph, Blueprint, FVector2D::ZeroVector);

	Result.Diagnostics.Insert(ParseDiagnostics, 0);
	for (const FNodeScribeDiagnostic& Diagnostic : ParseDiagnostics)
	{
		if (Diagnostic.Severity == ENodeScribeSeverity::Error)
		{
			++Result.ErrorCount;
		}
	}

	if (Result.CreatedNodes.Num() > 0)
	{
		TSet<UObject*> NodeSet;
		for (UEdGraphNode* Node : Result.CreatedNodes)
		{
			NodeSet.Add(Node);
		}

		FString ExportedText;
		FEdGraphUtilities::ExportNodesToText(NodeSet, ExportedText);
		FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
	}

	PresentResult(Result, LOCTEXT("Copied", "no clipboard - va' ao grafo e aperte Ctrl+V"));

	return FReply::Handled();
}

void SNodeScribePanel::PresentResult(const FNodeScribeBuilder::FResult& Result, const FText& SuccessVerb)
{
	Diagnostics.Reset();
	for (const FNodeScribeDiagnostic& Diagnostic : Result.Diagnostics)
	{
		Diagnostics.Add(MakeShared<FNodeScribeDiagnostic>(Diagnostic));
	}

	if (DiagnosticList.IsValid())
	{
		DiagnosticList->RequestListRefresh();
	}

	const int32 NodeCount = Result.CreatedNodes.Num();

	if (Result.ErrorCount > 0)
	{
		SummaryText = FText::Format(
			LOCTEXT("SummaryWithErrors", "{0} node(s) {1}, mas {2} linha(s) nao resolveram - procure os comentarios vermelhos no grafo."),
			FText::AsNumber(NodeCount), SuccessVerb, FText::AsNumber(Result.ErrorCount));

		ShowToast(SummaryText, false);
	}
	else if (Result.WarningCount > 0)
	{
		SummaryText = FText::Format(
			LOCTEXT("SummaryWithWarnings", "{0} node(s) {1}. {2} pino(s) esperam uma escolha sua."),
			FText::AsNumber(NodeCount), SuccessVerb, FText::AsNumber(Result.WarningCount));

		ShowToast(SummaryText, true);
	}
	else
	{
		SummaryText = FText::Format(
			LOCTEXT("SummaryClean", "{0} node(s) {1}."),
			FText::AsNumber(NodeCount), SuccessVerb);

		ShowToast(SummaryText, true);
	}
}

FText SNodeScribePanel::GetSummaryText() const
{
	return SummaryText;
}

// ---------------------------------------------------------------------------
// Diagnosticos
// ---------------------------------------------------------------------------

TSharedRef<ITableRow> SNodeScribePanel::OnGenerateDiagnosticRow(
	TSharedPtr<FNodeScribeDiagnostic> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Prefix = (Item->Line > 0)
		? FString::Printf(TEXT("linha %d: "), Item->Line)
		: FString();

	return SNew(STableRow<TSharedPtr<FNodeScribeDiagnostic>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Prefix + Item->Message))
			.ColorAndOpacity(FSlateColor(GetSeverityColor(Item->Severity)))
			.AutoWrapText(true)
		];
}

#undef LOCTEXT_NAMESPACE
