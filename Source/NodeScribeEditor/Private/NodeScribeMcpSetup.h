#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

/**
 * The two settings that separate "plugin installed" from "plugin working".
 *
 * NodeScribe does not speak MCP: it registers a toolset in the
 * ToolsetRegistry, and what puts that on the air is the Engine's own
 * ModelContextProtocol plugin. They are optional dependencies in the
 * `.uplugin` -- without them the plugin stays whole on the editor side, only
 * the assistant reaches nothing.
 *
 * The problem is that installing all three is not enough, and the two missing
 * things give no error at all -- it is all silence:
 *
 * 1. The Engine's server is born off (`bAutoStartServer` is false by
 *    default). Without turning it on, every editor launch requires running
 *    `ModelContextProtocol.StartServer` by hand.
 *
 * 2. The client needs to know the address. In Claude Code that is an entry in
 *    `.mcp.json` at the project root, which nobody writes by themselves.
 *
 * In both cases the failure is silent: the assistant connects, gets an empty
 * list (or does not even connect) and moves on without saying it lost
 * anything. A whole session went by here with the server off before anyone
 * noticed.
 *
 * It cannot be solved with a plugin ini: Unreal injects `<Plugin>/Config` into
 * the project's hierarchy, but only for branches it already knows by file
 * name, and the result does not reach `EditorPerProjectUserSettings`. That was
 * measured, not deduced.
 *
 * So it is code. Nothing here links against Epic's plugin: the settings are
 * read through reflection by class name, and the server starts through a
 * console command. Without ModelContextProtocol installed, both fail quietly,
 * which is right -- the dependency really is optional.
 */
class FNodeScribeMcpSetup
{
public:
	/** Schedules the check for after everyone has loaded. */
	static void Start();

	static void Stop();

	/**
	 * Turns `bAutoStartServer` on if it is off, and starts the server now.
	 *
	 * @return what happened, in one line, for the log and for the menu.
	 */
	static FString EnsureServer();

	/**
	 * Puts this project's entry into `.mcp.json`, if it is not there yet.
	 *
	 * Strictly additive: an entry that already exists under another name, or
	 * with another URL, stays as it is. Touching what the person wrote would be
	 * worse than doing nothing -- they may be pointing at a tunnel, another
	 * port, another editor.
	 *
	 * @param bForce  fixes the URL of an entry of ours that is out of date. It is
	 *                what the menu item sends, and what startup does not send.
	 */
	static FString EnsureMcpJsonEntry(bool bForce);

	/** `<Project>/.mcp.json`. Exposed for the documentation and for testing. */
	static FString GetMcpJsonPath();

	/** Registers the item under Tools, to redo both things on demand. */
	static void RegisterStartupHook();

	static void Unregister();

private:
	static void RegisterMenu();
	static bool Check(float DeltaTime);

	/**
	 * Port and path configured in Epic's plugin, through reflection.
	 *
	 * @return false if ModelContextProtocol is not in the project.
	 */
	static bool ReadServerAddress(int32& OutPort, FString& OutPath);

	static FTSTicker::FDelegateHandle TickHandle;
	static FDelegateHandle StartupCallbackHandle;
};
