#include "NodeScribeEditorModule.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeCommandFile.h"
#include "NodeScribeGraphActions.h"
#include "NodeScribeTypes.h"

#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

DEFINE_LOG_CATEGORY(LogNodeScribe);

namespace
{
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle BlueprintCompiledHandle;
}

void FNodeScribeEditorModule::StartupModule()
{
	FNodeScribeGraphActions::RegisterStartupHook();
	FNodeScribeCommandFile::Start();

	// O catalogo e' montado uma vez e guardado em memoria, e sem isto ele nunca
	// sabia do que nasceu depois: criar uma funcao num Blueprint e chama-la de
	// *outro* Blueprint, na mesma sessao, dava "nao achei nenhum node chamado X"
	// para uma funcao que existe. O `FindOwnFunction` ja' cobria a classe que
	// voce esta' editando -- o buraco era todo o resto, e e' justamente o fluxo
	// de quem cria um asset e escreve o grafo em seguida.
	//
	// A reconstrucao e' preguicosa: invalidar so' marca, e o proximo uso paga.
	//
	// GEditor ainda nao existe aqui -- este modulo carrega antes dele.
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([]
	{
		if (GEditor)
		{
			BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([]
			{
				FNodeScribeCatalog::Get().Invalidate();
			});
		}
	});

	UE_LOG(LogNodeScribe, Log, TEXT("NodeScribe pronto. Botoes na barra do editor de Blueprint."));
}

void FNodeScribeEditorModule::ShutdownModule()
{
	if (GEditor && BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
	}

	FNodeScribeCommandFile::Stop();
	FNodeScribeGraphActions::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNodeScribeEditorModule, NodeScribeEditor)
