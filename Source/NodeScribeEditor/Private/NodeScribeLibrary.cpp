#include "NodeScribeLibrary.h"

#include "NodeScribeBuilder.h"
#include "NodeScribeParser.h"
#include "NodeScribeReader.h"
#include "NodeScribeTarget.h"
#include "NodeScribeTypes.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace
{
	const TCHAR* SeverityLabel(ENodeScribeSeverity Severity)
	{
		switch (Severity)
		{
		case ENodeScribeSeverity::Error:   return TEXT("erro");
		case ENodeScribeSeverity::Warning: return TEXT("aviso");
		default:                           return TEXT("nota");
		}
	}

	/**
	 * Diagnosticos em texto puro.
	 *
	 * Quem chama de fora nao ve' o Message Log nem os comentarios vermelhos no
	 * grafo, entao tudo que o plugin recusou tem que voltar aqui -- e' o unico
	 * canal que esse chamador tem.
	 */
	FString FormatDiagnostics(const TArray<FNodeScribeDiagnostic>& Diagnostics)
	{
		TArray<FString> Lines;
		Lines.Reserve(Diagnostics.Num());

		for (const FNodeScribeDiagnostic& Diagnostic : Diagnostics)
		{
			Lines.Add(Diagnostic.Line > 0
				? FString::Printf(TEXT("linha %d [%s]: %s"),
					Diagnostic.Line, SeverityLabel(Diagnostic.Severity), *Diagnostic.Message)
				: FString::Printf(TEXT("[%s]: %s"),
					SeverityLabel(Diagnostic.Severity), *Diagnostic.Message));
		}

		return FString::Join(Lines, TEXT("\n"));
	}
}

FString UNodeScribeLibrary::WriteGraph(UEdGraph* Graph, const FString& Text)
{
	if (!Graph)
	{
		return TEXT("[erro]: nenhum grafo informado.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return TEXT("[erro]: esse grafo nao pertence a um Blueprint.");
	}

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(Text, ParseDiagnostics);

	const FScopedTransaction Transaction(LOCTEXT("WriteGraphTransaction", "NodeScribe: escrever grafo"));
	Blueprint->Modify();
	Graph->Modify();

	FNodeScribeBuilder::FResult Result = FNodeScribeBuilder::Build(
		Statements, Graph, Blueprint, FNodeScribeTarget::FindFreeOrigin(Graph));

	Result.Diagnostics.Insert(ParseDiagnostics, 0);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	const FString Report = FormatDiagnostics(Result.Diagnostics);

	// A contagem vai junto mesmo quando nao ha' diagnostico: quem chamou nao
	// esta' olhando o grafo e precisa saber que algo aconteceu.
	return FString::Printf(TEXT("%d node(s) criados.%s%s"),
		Result.CreatedNodes.Num(),
		Report.IsEmpty() ? TEXT("") : TEXT("\n"),
		*Report);
}

FString UNodeScribeLibrary::ReadGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("[erro]: nenhum grafo informado.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);

	FNodeScribeReader::FResult Result = FNodeScribeReader::ReadGraph(Graph, Blueprint);

	const FString Report = FormatDiagnostics(Result.Diagnostics);
	if (Report.IsEmpty())
	{
		return Result.Text;
	}

	// Os avisos entram comentados: o texto continua colavel de volta como esta'.
	TArray<FString> Commented;
	Report.ParseIntoArrayLines(Commented);

	for (FString& Line : Commented)
	{
		Line.InsertAt(0, TEXT("# "));
	}

	return Result.Text + TEXT("\n\n") + FString::Join(Commented, TEXT("\n"));
}

FString UNodeScribeLibrary::GetFormatDocs()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeScribe"));
	if (!Plugin.IsValid())
	{
		return TEXT("[erro]: nao achei o plugin NodeScribe.");
	}

	const FString DocsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("FORMATO.md"));

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *DocsPath))
	{
		return FString::Printf(TEXT("[erro]: nao consegui ler %s"), *DocsPath);
	}

	return Contents;
}

#undef LOCTEXT_NAMESPACE
