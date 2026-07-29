#pragma once

#include "CoreMinimal.h"
#include "NodeScribeBuilder.h"
#include "NodeScribeTarget.h"
#include "NodeScribeTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"

class ITableRow;
class SMultiLineEditableTextBox;
template <typename ItemType> class SComboBox;
template <typename ItemType> class SListView;

/** Janela do NodeScribe: cola o texto, escolhe o destino, transcreve. */
class SNodeScribePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNodeScribePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// --- Destino ----------------------------------------------------------

	void RefreshTargets();
	FReply OnRefreshClicked();

	TSharedRef<SWidget> OnGenerateTargetWidget(TSharedPtr<FNodeScribeTarget> Target);
	void OnTargetSelected(TSharedPtr<FNodeScribeTarget> Target, ESelectInfo::Type SelectInfo);
	FText GetSelectedTargetText() const;

	// --- Acoes ------------------------------------------------------------

	FReply OnInsertClicked();
	FReply OnCopyClicked();
	FReply OnClearClicked();

	bool HasUsableTarget() const;
	bool CanRun() const;

	/** Publica os diagnosticos e monta a frase de resumo. */
	void PresentResult(const FNodeScribeBuilder::FResult& Result, const FText& SuccessVerb);

	// --- Entrada ----------------------------------------------------------

	void OnScriptTextChanged(const FText& NewText);
	FText GetScriptText() const;

	// --- Diagnosticos -----------------------------------------------------

	TSharedRef<ITableRow> OnGenerateDiagnosticRow(TSharedPtr<FNodeScribeDiagnostic> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FText GetSummaryText() const;

	FText ScriptText;
	FText SummaryText;

	TArray<TSharedPtr<FNodeScribeTarget>> Targets;
	TSharedPtr<FNodeScribeTarget> SelectedTarget;

	TArray<TSharedPtr<FNodeScribeDiagnostic>> Diagnostics;

	TSharedPtr<SComboBox<TSharedPtr<FNodeScribeTarget>>> TargetCombo;
	TSharedPtr<SListView<TSharedPtr<FNodeScribeDiagnostic>>> DiagnosticList;
};
