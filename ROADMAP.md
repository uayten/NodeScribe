# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-031 -->

## Em teste

## Em andamento

## A fazer

### R-025 [feature] Make Array volta ao recriar o grafo
Esperado: Blueprint com Make Array, como a lista de Object Types de um Overlap, continua compilando depois de recriado.
Como:
- Hoje a leitura avisa que não sabe escrever `Make Array`, e a cópia dá erro no pino `Object Types`
- Afeta `GA_AtaqueBásico`, `GA_Investida`, `GA_3Hits`, `BTService_LocalizarJogador`, `OndaSpawnavel`; `BP_Pedra` também tem o aviso

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
