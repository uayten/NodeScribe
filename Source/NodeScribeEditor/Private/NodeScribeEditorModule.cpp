#include "NodeScribeEditorModule.h"

#include "NodeScribeTypes.h"
#include "SNodeScribePanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

DEFINE_LOG_CATEGORY(LogNodeScribe);

const FName FNodeScribeEditorModule::TabName("NodeScribe");

namespace
{
	TSharedRef<SDockTab> SpawnNodeScribeTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SNodeScribePanel)
			];
	}
}

void FNodeScribeEditorModule::StartupModule()
{
	const IWorkspaceMenuStructure& MenuStructure =
		FModuleManager::LoadModuleChecked<FWorkspaceMenuStructureModule>("WorkspaceMenuStructure")
			.GetWorkspaceMenuStructure();

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&SpawnNodeScribeTab))
		.SetDisplayName(LOCTEXT("TabTitle", "NodeScribe"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Transcreve uma lista de nodes em texto para nodes reais no Blueprint."))
		.SetGroup(MenuStructure.GetToolsCategory());

	UE_LOG(LogNodeScribe, Log, TEXT("NodeScribe pronto. Janela -> Ferramentas -> NodeScribe."));
}

void FNodeScribeEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNodeScribeEditorModule, NodeScribeEditor)
