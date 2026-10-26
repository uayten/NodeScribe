# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-015 -->

## Em teste

<!-- Passos de teste: um passo pode mandar pedir algo ao assistente; depois do pedido feito, ele é reescrito com o que o usuário confere. -->

## Em andamento

## A fazer

### R-002 [feature] Ler o Default Starting Data do AbilitySystem
Esperado: saber, lendo o BP_Golem, se o componente AbilitySystem concede as habilidades do Golem.

### R-003 [feature] Adicionar componentes a um Blueprint pelo texto
Esperado: pedir ao assistente um componente novo num Blueprint (o que hoje se faz no painel Components) e ele aparecer lá, já configurado.

### R-004 [feature] Input Mapping Context como texto
Esperado: ler e escrever um Input Mapping Context como uma lista tecla → ação, com os triggers e modifiers de cada mapeamento, em vez da lista enorme de structs que sai hoje.

### R-005 [feature] Mover e reordenar widgets no Designer
Esperado: mudar um widget de lugar ou de ordem pelo texto do Designer.

### R-006 [feature] Conteúdo de named slots no Designer
Esperado: ver e montar o que fica dentro dos named slots de um Widget Blueprint filho, como o resto do Designer.

### R-007 [feature] Componentes de Gameplay Effect
Esperado: montar um cooldown novo sem cliques, incluindo o componente Grant Tags to Target Actor.

### R-009 [ajuste] Prosa enxuta no FORMAT.md
Ajusta: a especificação do formato (Docs/FORMAT.md)
Esperado: o FORMAT.md dizer só as regras; o porquê de cada uma fica num documento de design à parte.
Como:
- As afirmações medidas do README ficam onde estão

### R-010 [feature] Teste automático de ida e volta nos grafos do projeto
Esperado: qualquer grafo do projeto que não volta igual depois de lido e colado aparece sozinho como falha, sem alguém tropeçar nele (como o Set Visibility, os pinos do Append e os nomes de struct dividida).
Como:
- Para cada grafo: ler, gravar num rascunho, ler de novo, comparar
- Rascunhos só em /Game/BossRush/Testes/, nada salvo
- Junto da suíte de testes que já existe

### R-011 [ajuste] Confirmar no editor a ida e volta da state machine
Ajusta: state machine do AnimGraph (Docs/format/animgraph.md)
Esperado: uma state machine com regra de transição lida, colada de volta e compilada sem erro no editor, não só na suíte automática.
Como:
- Rascunho `/Game/Retarget/ABP_TesteSM` no projeto Metahuman; apagar no fim
- A regra tem que voltar como linha sob `Idle -> Running:`, sem a nota de nós órfãos
