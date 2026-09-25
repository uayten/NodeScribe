#pragma once

#include "CoreMinimal.h"

/**
 * The three buttons NodeScribe adds to the Blueprint editor's toolbar.
 *
 * They sit up there, next to Compile, and not in a separate window, because
 * that is where the hand already is when looking at a graph.
 */
class FNodeScribeGraphActions
{
public:
	/**
	 * Schedules the registration for when ToolMenus is ready. The asset editors'
	 * toolbars only exist after StartupModule.
	 */
	static void RegisterStartupHook();

	static void Unregister();

	/** Name of the Message Log channel where the diagnostics show up. */
	static const FName LogListingName;

private:
	/** Builds the section on the toolbars. Only runs after ToolMenus exists. */
	static void RegisterToolbar();
};
