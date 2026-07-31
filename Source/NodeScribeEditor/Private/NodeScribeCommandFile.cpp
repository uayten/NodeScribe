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
	 * Meio segundo.
	 *
	 * Existe um custo por ciclo e um custo por espera. Olhar um arquivo que
	 * quase nunca esta' la' e' barato; esperar meio segundo a mais para o editor
	 * comecar a fechar nao muda nada num ciclo que leva minutos.
	 */
	const float PollSeconds = 0.5f;
}

FString FNodeScribeCommandFile::GetCommandPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"), TEXT("comando.txt"));
}

FString FNodeScribeCommandFile::GetResponsePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeScribe"), TEXT("resposta.txt"));
}

void FNodeScribeCommandFile::Start()
{
	// Comando velho de uma sessao anterior nao vale: o editor fecharia sozinho
	// ao abrir, e quem escreveu aquilo ja' foi embora.
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
		// Provavelmente pego no meio da escrita. Nao apaga: o proximo ciclo pega
		// inteiro, e apagar aqui perderia o comando.
		return true;
	}

	// Apagar antes de executar. `quit` derruba o processo, e um comando que
	// sobrevive ao fechamento roda de novo na proxima abertura.
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
			TEXT("[erro]: nao conheco o comando `%s`. Conheco: quit."), *Command);
	}

	UE_LOG(LogNodeScribe, Log, TEXT("comando.txt `%s`: %s"), *Command, *Response);
	FFileHelper::SaveStringToFile(Response, *GetResponsePath());

	return true;
}
