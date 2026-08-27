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

/**
 * Apaga o que da' para apagar do grafo, e diz quantos foram.
 *
 * Node de entrada de funcao recusa ser apagado (`CanUserDeleteNode`), e recusa
 * com razao: ele nasce com a funcao. Pular esses e' o comportamento certo, nao
 * uma limitacao.
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
		return TEXT("[erro]: nenhum grafo informado.");
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return TEXT("[erro]: esse grafo nao pertence a um Blueprint.");
	}

	// A guarda da substituicao, antes de qualquer alteracao.
	//
	// Apagar e' a unica coisa que este plugin faz que nao da' para conferir
	// depois: o que sumiu nao aparece no texto que sobrou. Entao a pergunta que
	// decide nao e' "o texto novo esta' bom", e sim "o grafo que esta' la' cabe
	// em texto". Se a leitura dele perde alguma coisa -- uma reconvergencia, um
	// Cast com continuacao, um node que o formato nao sabe nomear, um node de
	// dado solto --, apagar destroi exatamente aquilo que ninguem tem escrito.
	//
	// Nao ha' modo forcado de proposito. Quem realmente quer limpar seleciona
	// tudo no grafo e aperta Delete: e' um gesto humano, visivel, e com Ctrl+Z
	// do lado.
	if (bReplace)
	{
		const FNodeScribeReader::FResult Current = FNodeScribeReader::ReadGraph(Graph, Blueprint);

		if (Current.WarningCount > 0 || Current.LostNodeCount > 0)
		{
			TArray<FString> Motivos;
			for (const FNodeScribeDiagnostic& Diagnostic : Current.Diagnostics)
			{
				if (Diagnostic.Severity == ENodeScribeSeverity::Warning)
				{
					Motivos.Add(TEXT("  - ") + Diagnostic.Message);
				}
			}

			if (Current.LostNodeCount > 0)
			{
				Motivos.Add(FString::Printf(
					TEXT("  - %d node(s) de dado nao alimentam nada, e nao voltariam."),
					Current.LostNodeCount));
			}

			return FString::Printf(
				TEXT("[erro]: nao substitui este grafo -- ele tem coisa que o texto nao sabe dizer, ")
				TEXT("e apagar destruiria justamente isso. Nada foi alterado.\n%s\n")
				TEXT("Escreva sem substituir, ou apague na mao o que quiser trocar (Ctrl+A, Delete no grafo) e escreva depois."),
				*FString::Join(Motivos, TEXT("\n")));
		}
	}

	TArray<FNodeScribeDiagnostic> ParseDiagnostics;
	const TArray<FNodeScribeStatement> Statements = FNodeScribeParser::Parse(Text, ParseDiagnostics);

	const FScopedTransaction Transaction(LOCTEXT("WriteGraphTransaction", "NodeScribe: escrever grafo"));
	Blueprint->Modify();
	Graph->Modify();

	// Dentro da transacao: o Ctrl+Z desfaz o apagar e o escrever de uma vez.
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

	// A contagem vai junto mesmo quando nao ha' diagnostico: quem chamou nao
	// esta' olhando o grafo e precisa saber que algo aconteceu.
	const FString Apagados = Removed > 0
		? FString::Printf(TEXT("%d node(s) apagados, "), Removed)
		: FString();

	return FString::Printf(TEXT("%s%d node(s) criados.%s%s"),
		*Apagados,
		Result.CreatedNodes.Num(),
		Report.IsEmpty() ? TEXT("") : TEXT("\n"),
		*Report);
}

FString UNodeScribeLibrary::ClearGraph(UEdGraph* Graph)
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

	// Ler antes de apagar. E' isto que separa este gesto de um modo forcado: o
	// grafo volta na resposta, e o que a leitura nao soube dizer volta como
	// aviso -- entao quem apagou sabe o que perdeu, em vez de descobrir depois.
	const FNodeScribeReader::FResult Before = FNodeScribeReader::ReadGraph(Graph, Blueprint);

	const FScopedTransaction Transaction(LOCTEXT("ClearGraphTransaction", "NodeScribe: esvaziar grafo"));
	Blueprint->Modify();
	Graph->Modify();

	const int32 Removed = RemoveDeletableNodes(Graph, Blueprint);

	if (Removed == 0)
	{
		return TEXT("O grafo ja' estava vazio. Nada foi alterado.");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%d node(s) apagados. O que estava la':"), Removed));
	Lines.Add(Before.Text.IsEmpty() ? TEXT("# (nada que o texto soubesse dizer)") : Before.Text);

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

FString UNodeScribeLibrary::CreateAsset(const FString& Path, const FString& Parent, const FString& Options)
{
	return FNodeScribeAssetMaker::CreateAsset(Path, Parent, Options);
}

FString UNodeScribeLibrary::WriteBlendSpace(UBlendSpace* BlendSpace, const FString& Text)
{
	const NodeScribeBlendSpace::FResult Result = NodeScribeBlendSpace::Write(BlendSpace, Text);

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%d sample(s), %d eixo(s)."),
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
		return TEXT("[erro]: sem editor.");
	}

	// Fechar no meio de um teste surpreende, e o ganho de tempo nao paga isso.
	if (GEditor->IsPlaySessionInProgress())
	{
		return TEXT("[erro]: ha' um Play In Editor rodando. Pare o Play antes.");
	}

	// O caminho do Slate pergunta "tem certeza?" num dialogo modal quando essa
	// opcao esta ligada. Quem chama isto e' um programa: ninguem estaria la para
	// clicar, e o editor ficaria pendurado sem explicacao.
	if (GetDefault<UEditorPerProjectUserSettings>()->bConfirmEditorClose)
	{
		return TEXT("[erro]: 'Confirm on Editor Close' esta' ligado -- fechar abriria um dialogo\n")
			TEXT("que so' um humano fecha. Desmarque em Editor Preferences > General > Loading & Saving.");
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

	// O retorno de SaveDirtyPackages nao serve de prova, e o bNeededSaving
	// tambem nao. Sao dois caminhos de perda silenciosa:
	//
	// - InternalSavePackages so devolve false quando o usuario cancela ("Only
	//   cancel should return false", diz o comentario da engine). Pacote que
	//   falhou ao gravar -- somente leitura, travado no controle de versao, erro
	//   no meio -- devolve sucesso. E com bPromptUserToSave e bCanBeDeclined em
	//   false nao ha cancelamento possivel, entao o retorno e sempre true.
	//
	// - Se todo pacote sujo estiver em FEditorFileUtils::PackagesNotSavedDuringSaveAll
	//   (a lista do que o usuario desmarcou em algum dialogo de salvar, que dura
	//   a sessao inteira), a funcao nem tenta gravar e ainda devolve
	//   bNeededSaving = false.
	//
	// Entao se pergunta de novo quem continua sujo, e ai sim se sabe.
	TArray<UPackage*> AindaSujos;
	FEditorFileUtils::GetDirtyWorldPackages(AindaSujos);
	FEditorFileUtils::GetDirtyContentPackages(AindaSujos);

	if (AindaSujos.Num() > 0)
	{
		TArray<FString> Nomes;
		Nomes.Reserve(AindaSujos.Num());
		for (const UPackage* Pacote : AindaSujos)
		{
			Nomes.Add(Pacote->GetName());
		}
		Nomes.Sort();

		return FString::Printf(
			TEXT("[erro]: nao fechei -- %d pacote(s) continuam sem salvar depois do save:\n%s\n")
			TEXT("Salve na mao (Ctrl+Shift+S) e veja o que o editor reclama."),
			Nomes.Num(),
			*FString::Join(Nomes, TEXT("\n")));
	}

	// O QUIT_EDITOR pula o desligamento do Slate: vai direto em
	// UUnrealEdEngine::CloseEditor -> RequestEngineExit. Os editores de asset
	// abertos ficam vivos, e so' sao desmontados depois que a janela principal ja
	// morreu -- com a cena de preview deles apontando para coisa destruida. E' o
	// crash do AnimationBlueprintEditor. A propria engine avisa, no
	// EditorServer.cpp, ao lado do QUIT_EDITOR: "Don't call quit_editor directly
	// with slate".
	//
	// CLOSE_SLATE_MAINFRAME e' a porta certa. Cai em
	// FMainFrameHandler::ShutDownEditor, que na ordem certa: fecha os editores de
	// asset (BroadcastEditorClose), desliga o arquivo de restauracao do autosave
	// -- e' ele que fazia o editor oferecer "recuperar" na abertura seguinte --,
	// salva a posicao da janela, e so' entao enfileira o QUIT_EDITOR.
	//
	// Adiado porque sair aqui derrubaria a conexao antes desta resposta sair, e
	// quem chamou veria um erro de rede em vez da confirmacao.
	GEngine->DeferredCommands.Add(TEXT("CLOSE_SLATE_MAINFRAME"));

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
