# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-031 -->

## Em teste

## Em andamento

## A fazer

### R-026 [pesquisa] Parâmetros da função perdem a ligação ao recriar o grafo
Esperado: por que os pinos ligados aos parâmetros da função ficam sem ligação na cópia, e se isso é limite do teste ou do plugin.
Como:
- Afeta `GC_CorteAzul`, `GC_RaioAzul` e `BP_array_weapon_StaticMesh`: os erros são em `Target` e `Parameters`, e a leitura avisa que a entrada da função não é criada por uma linha

### R-027 [pesquisa] Erros que os avisos da leitura podem não explicar
Esperado: para cada caso, se o erro vem do que a leitura avisou ou se é um defeito silencioso.
Como:
- `BP_Pedra`: `Get Alvo Atual` e `Get Gameplay Ability do Laser` procuram a variável no próprio Blueprint
- `BP_array_weapon_SkeletaMesh`: a cópia chama `GetSkeletalMesh`, que só existe para Editor Utility
- `AtivarHabilidadeDeMana`: `Verifica se tem Mana e Segurou o botão` acusa o pino `Return Value`
- `PáginaInput`: `GetTagName` sem ligação
- `BP_Skeleton` e `Widget_Configurações`: `Equal` sem ligação; a leitura de `Widget_Configurações` avisa sobre `Equal (Enum)`
- `GA_Investida`: `Add` e `Contains Item` sem tipo
