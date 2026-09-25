#include "NodeScribeCommandFile.h"

#include "NodeScribeLibrary.h"
#include "NodeScribeTypes.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FTSTicker::FDelegateHandle FNodeScribeCommandFile::TickHandle;

namespace
{
	/**
	 * Half a second.
	 *
	 * There is a cost per cycle and a cost per wait. Looking at a file that is
	 * almost never there is cheap; waiting half a second more for the editor to
	 * start closing changes nothing in a cycle that takes minutes.
	 */
	const float PollSeconds = 0.5f;
}

FString FNodeScribeCommandFile::GetCommandPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"), TEXT("command.txt"));
}

FString FNodeScribeCommandFile::GetResponsePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"), TEXT("response.txt"));
}

void FNodeScribeCommandFile::Start()
{
	// A stale command from a previous session does not count: the editor would
	// close by itself on opening, and whoever wrote it is long gone.
	IFileManager::Get().Delete(*GetCommandPath(), /*RequireExists*/ false);

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&FNodeScribeCommandFile::Tick), PollSeconds);
}

void FNodeScribeCommandFile::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FNodeScribeCommandFile::Tick(float DeltaTime)
{
	const FString CommandPath = GetCommandPath();

	if (!IFileManager::Get().FileExists(*CommandPath))
	{
		return true;
	}

	FString Command;
	if (!FFileHelper::LoadFileToString(Command, *CommandPath))
	{
		// Probably caught mid-write. Do not delete: the next cycle gets it whole,
		// and deleting here would lose the command.
		return true;
	}

	// Delete before executing. `quit` brings the process down, and a command
	// that survives the shutdown runs again on the next launch.
	IFileManager::Get().Delete(*CommandPath, /*RequireExists*/ false);

	Command.TrimStartAndEndInline();

	FString Response;
	if (Command.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		Response = UNodeScribeLibrary::SaveAllAndQuit();
	}
	else
	{
		Response = FString::Printf(
			TEXT("[error]: unknown command `%s`. Known: quit."), *Command);
	}

	UE_LOG(LogNodeScribe, Log, TEXT("command.txt `%s`: %s"), *Command, *Response);
	FFileHelper::SaveStringToFile(Response, *GetResponsePath());

	return true;
}
