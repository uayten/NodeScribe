# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-012 -->

## Em teste

### R-001 Designer de Widget Blueprint como texto
1. Pedir ao assistente para ler o Designer do WBP_ConnectedJoyShockInfo e abrir esse widget no editor: a resposta lista os widgets na mesma ordem e hierarquia do painel Hierarchy, com os mesmos textos da tela.
2. Pedir ao assistente uma linha "Temperature" igual à do Is Still no WBP_TesteDesigner e abrir o widget no Designer: a linha nova aparece logo abaixo da do Is Still, com a mesma fonte, alinhamento e largura.
3. Pedir ao assistente um widget com o nome da classe escrito errado (por exemplo "TextBlok"): ele responde com o erro e sugere "TextBlock", e o Designer não muda.
4. Pedir ao assistente um grafo no WBP_TesteDesigner que esconde a resposta da bateria e escreve nela um texto juntando quatro pedaços, e clicar em Compile: compila sem erro, e o nó Append aparece com quatro entradas.
5. Pedir ao assistente para regravar por inteiro o EventGraph de um rascunho a partir da própria leitura: ele grava, sem recusar por "coisas que o texto não diz".

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
Depende de: R-001

### R-006 [feature] Conteúdo de named slots no Designer
Esperado: ver e montar o que fica dentro dos named slots de um Widget Blueprint filho, como o resto do Designer.
Depende de: R-001

### R-007 [feature] Componentes de Gameplay Effect
Esperado: montar um cooldown novo sem cliques, incluindo o componente Grant Tags to Target Actor.

### R-008 [ajuste] FORMAT.md por tópico
Ajusta: a especificação do formato (Docs/FORMAT.md)
Esperado: uma tarefa comum de grafo custar ~3-4 mil tokens de leitura da especificação, em vez dos ~9 mil de hoje.
Como:
- Um núcleo curto (regras comuns de grafo + tabela de palavras-chave), lido por padrão
- As seções de tópico (AnimGraph, volta, Designer, Behavior Tree) lidas só quando a tarefa pede; `get_format_docs` recebe o tópico
- Peso hoje: 963 linhas (~36 KB); AnimGraph 255, volta 115, Designer 83, Behavior Tree 42, núcleo ~400
- Atualizar junto a instrução do CLAUDE.md do BossRush, que hoje manda ler o arquivo inteiro

### R-009 [ajuste] Prosa enxuta no FORMAT.md
Ajusta: a especificação do formato (Docs/FORMAT.md)
Esperado: o FORMAT.md dizer só as regras; o porquê de cada uma fica num documento de design à parte.
Como:
- As afirmações medidas do README ficam onde estão
Depende de: R-008

### R-010 [feature] Teste automático de ida e volta nos grafos do projeto
Esperado: qualquer grafo do projeto que não volta igual depois de lido e colado aparece sozinho como falha, sem alguém tropeçar nele (como o Set Visibility, os pinos do Append e os nomes de struct dividida).
Como:
- Para cada grafo: ler, gravar num rascunho, ler de novo, comparar
- Rascunhos só em /Game/BossRush/Testes/, nada salvo
- Junto da suíte de testes que já existe

### R-011 [ajuste] Confirmar no editor a ida e volta da state machine
Ajusta: state machine do AnimGraph (Docs/FORMAT.md > AnimGraph)
Esperado: uma state machine com regra de transição lida, colada de volta e compilada sem erro no editor, não só na suíte automática.
Como:
- Rascunho `/Game/Retarget/ABP_TesteSM` no projeto Metahuman; apagar no fim
- A regra tem que voltar como linha sob `Idle -> Running:`, sem a nota de nós órfãos
