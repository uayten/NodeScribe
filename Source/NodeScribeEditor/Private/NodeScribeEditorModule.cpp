#include "NodeScribeEditorModule.h"

#include "NodeScribeCommandFile.h"
#include "NodeScribeGraphActions.h"
#include "NodeScribeTypes.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

DEFINE_LOG_CATEGORY(LogNodeScribe);

void FNodeScribeEditorModule::StartupModule()
{
	FNodeScribeGraphActions::RegisterStartupHook();
	FNodeScribeCommandFile::Start();

	UE_LOG(LogNodeScribe, Log, TEXT("NodeScribe pronto. Botoes na barra do editor de Blueprint."));
}

void FNodeScribeEditorModule::ShutdownModule()
{
	FNodeScribeCommandFile::Stop();
	FNodeScribeGraphActions::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNodeScribeEditorModule, NodeScribeEditor)
