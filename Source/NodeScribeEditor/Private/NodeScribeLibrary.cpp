#include "NodeScribeLibrary.h"

#include "NodeScribeAssetMaker.h"
#include "NodeScribeBuilder.h"
#include "NodeScribeObjectReader.h"
#include "NodeScribeObjectWriter.h"
#include "NodeScribeParser.h"
#include "NodeScribeReader.h"
#include "NodeScribeTags.h"
#include "NodeScribeTarget.h"
#include "NodeScribeTypes.h"

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
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

FString UNodeScribeLibrary::ReadObject(UObject* Object, const FString& Filter)
{
	return FNodeScribeObjectReader::ReadObject(Object, Filter);
}

FString UNodeScribeLibrary::WriteObject(UObject* Object, const FString& Text)
{
	const FNodeScribeObjectWriter::FResult Result =
		FNodeScribeObjectWriter::WriteObject(Object, Text);

	const FString Report = FString::Join(Result.Diagnostics, TEXT("\n"));

	// A contagem vai junto mesmo sem diagnostico: quem chamou nao esta' olhando
	// o painel de detalhes e precisa saber que algo aconteceu.
	return FString::Printf(TEXT("%d propriedade(s) alterada(s).%s%s"),
		Result.Applied,
		Report.IsEmpty() ? TEXT("") : TEXT("\n"),
		*Report);
}

FString UNodeScribeLibrary::CreateAsset(const FString& Path, const FString& Parent)
{
	return FNodeScribeAssetMaker::CreateAsset(Path, Parent);
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
		return TEXT("[erro]: sem editor.");
	}

	// Fechar no meio de um teste surpreende, e o ganho de tempo nao paga isso.
	if (GEditor->IsPlaySessionInProgress())
	{
		return TEXT("[erro]: ha' um Play In Editor rodando. Pare o Play antes.");
	}

	bool bNeededSaving = false;
	const bool bSaved = FEditorFileUtils::SaveDirtyPackages(
		/*bPromptUserToSave*/ false,
		/*bSaveMapPackages*/ true,
		/*bSaveContentPackages*/ true,
		/*bFastSave*/ false,
		/*bNotifyNoPackagesSaved*/ false,
		/*bCanBeDeclined*/ false,
		&bNeededSaving);

	if (bNeededSaving && !bSaved)
	{
		return TEXT("[erro]: algo nao pode ser salvo. Nao fechei -- resolva e chame de novo.");
	}

	// Adiado: sair aqui derrubaria a conexao antes desta resposta sair, e quem
	// chamou veria um erro de rede em vez da confirmacao.
	GEngine->DeferredCommands.Add(TEXT("QUIT_EDITOR"));

	return bNeededSaving
		? TEXT("Tudo salvo. Fechando o editor.")
		: TEXT("Nada pendente para salvar. Fechando o editor.");
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
