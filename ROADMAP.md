# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-014 -->

## Em teste

<!-- Passos de teste: só o que o usuário faz e confere. A parte do assistente é feita antes de o item entrar aqui. -->

### R-008 [ajuste] FORMAT.md por tópico
1. Abrir `Docs/FORMAT.md` do NodeScribe: a tabela *Topics* aparece logo depois do índice, com os quatro tópicos e quando ler cada um.
2. Abrir a pasta `Docs/format/`: tem um arquivo para cada tópico da tabela (`animgraph`, `behavior-tree`, `designer`, `reading`).
3. Na próxima conversa sobre grafo no BossRush, conferir que o assistente lê só o `FORMAT.md` e só abre um arquivo de `Docs/format/` quando a tarefa é daquele tópico.

### R-012 [ajuste] Teto de tokens na suíte de testes
1. Abrir o arquivo `tests.txt` em `BossRush/Saved/NodeScribe/`: a última linha é `25 case(s), 0 failure(s)`, e logo acima está o `token_budget` com o tamanho do núcleo, de cada tópico, das descrições das ferramentas e a razão leitura/texto, cada um com seu teto.

### R-013 [ajuste] README só com o estado atual
1. Abrir o `README.md` do NodeScribe: a seção *Status* cabe numa tela e aponta para `Docs/history.md` e `Docs/ideas.md`.
2. Abrir esses dois arquivos: o histórico e as propostas que estavam no README estão lá, inteiros.

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
