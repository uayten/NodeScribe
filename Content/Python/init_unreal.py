# Carregado pela Unreal quando o plugin sobe. Registra o toolset do NodeScribe
# no ToolsetRegistry, que e' quem o expoe pelo MCP.

from nodescribe_toolset import toolsets

toolsets._registration.register()
