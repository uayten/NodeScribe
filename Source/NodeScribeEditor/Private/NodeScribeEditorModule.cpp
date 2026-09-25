#include "NodeScribeEditorModule.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeCommandFile.h"
#include "NodeScribeGraphActions.h"
#include "NodeScribeMcpSetup.h"
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

	// The two settings without which the plugin sits installed and mute. See
	// the NodeScribeMcpSetup header: both fail silently, and the price of not
	// checking is a whole session believing the assistant sees the project
	// when it sees nothing at all.
	FNodeScribeMcpSetup::RegisterStartupHook();
	FNodeScribeMcpSetup::Start();

	// The catalog is built once and kept in memory, and without this it never
	// knew about what was born afterwards: creating a function in a Blueprint
	// and calling it from *another* Blueprint, in the same session, gave "no
	// node called X" for a function that exists. `FindOwnFunction` already
	// covered the class being edited -- the hole was everything else, and it is
	// precisely the flow of someone who creates an asset and writes the graph
	// right after.
	//
	// The rebuild is lazy: invalidating only marks it, and the next use pays.
	//
	// GEditor does not exist yet here -- this module loads before it.
	PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]
	{
		if (GEditor)
		{
			BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([]
			{
				FNodeScribeCatalog::Get().Invalidate();
			});
		}
	});

	UE_LOG(LogNodeScribe, Log, TEXT("NodeScribe ready. Buttons on the Blueprint editor toolbar."));
}

void FNodeScribeEditorModule::ShutdownModule()
{
	if (GEditor && BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}

	FNodeScribeMcpSetup::Stop();
	FNodeScribeMcpSetup::Unregister();

	FNodeScribeCommandFile::Stop();
	FNodeScribeGraphActions::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNodeScribeEditorModule, NodeScribeEditor)
