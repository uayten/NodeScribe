#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/**
 * One command per file, for whoever has no MCP.
 *
 * The development cycle needs to close the editor to recompile, and today
 * that arrives through the MCP. Opening never depended on it -- it is a new
 * process from the terminal --, but closing did, and in one session the editor
 * was open with the MCP down: there was no way to close it without asking a
 * person to click the X.
 *
 * Killing the process would work and is the worst way out: it loses unsaved
 * work and skips the refusal to close with Play In Editor running.
 *
 * There is no network, port or protocol here -- it is a file that appears and
 * disappears. There is nothing to go down. A server of our own inside the
 * plugin would be more code to solve less, because it would still be something
 * that needs to be up.
 *
 * `Saved/NodeScribe/command.txt` vanishes when read; the output goes to
 * `response.txt`, with the same message the MCP would return.
 */
class FNodeScribeCommandFile
{
public:
	static void Start();
	static void Stop();

	/** Paths, exposed for the documentation and for testing. */
	static FString GetCommandPath();
	static FString GetResponsePath();

private:
	static bool Tick(float DeltaTime);

	static FTSTicker::FDelegateHandle TickHandle;
};
