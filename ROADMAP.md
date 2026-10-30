# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-031 -->

## Em teste

## Em andamento

## A fazer

### R-027 [pesquisa] Erros que os avisos da leitura podem não explicar
Esperado: para cada caso, se o erro vem do que a leitura avisou ou se é um defeito silencioso.
Como:
- `BP_Pedra`: `Get Alvo Atual` e `Get Gameplay Ability do Laser` procuram a variável no próprio Blueprint
- `BP_array_weapon_SkeletaMesh`: a cópia chama `GetSkeletalMesh`, que só existe para Editor Utility
- `AtivarHabilidadeDeMana`: `Verifica se tem Mana e Segurou o botão` acusa o pino `Return Value`
- `PáginaInput`: `GetTagName` sem ligação
- `BP_Skeleton` e `Widget_Configurações`: `Equal` sem ligação; a leitura de `Widget_Configurações` avisa sobre `Equal (Enum)`
- `GA_Investida`: `Add` e `Contains Item` sem tipo
- `BP_array_weapon_StaticMesh`: `Target` sem ligação; a leitura avisa sobre `Get (a copy)`, e `$staticMeshComponent2` não vem de nenhuma linha que o builder conheça
