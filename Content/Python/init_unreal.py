# Loaded by Unreal when the plugin starts. Registers the NodeScribe toolset
# with the ToolsetRegistry, which is what exposes it through the MCP.

from nodescribe_toolset import toolsets

toolsets._registration.register()
