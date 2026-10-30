# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-022 -->

## Em teste

## Em andamento

## A fazer

### R-016 [feature] Parâmetros de eventos customizados e de interface
Esperado: um evento customizado ou de interface com parâmetros, lido e colado de volta, volta com os mesmos parâmetros.
Como:
- Hoje o formato não tem como declarar parâmetro de evento; é a maior parte das 24 diferenças silenciosas que a suíte acha nos grafos do projeto (`project_graphs_round_trip`, diffs em `Saved/NodeScribe/project_graphs.txt`).
- Cada item daqui até o R-021 fecha quando o seu grupo some da lista de diferenças silenciosas da suíte; quem confere é o agente.

### R-017 [ajuste] Pinos de struct divididos em Spawn Actor e Set Color and Opacity
Ajusta: a leitura e a escrita de `$x.Pin` em struct dividido (Docs/FORMAT.md > When the node has more than one output)
Esperado: um pino de struct dividido (Split Struct Pin) nesses nodes volta dividido, com os mesmos valores.

### R-018 [ajuste] Nomes que começam com dígito e rótulos numéricos de Switch
Ajusta: nomes de variável e rótulos de Switch (Docs/FORMAT.md > Switch)
Esperado: uma variável como `3 Corações` e um `Switch on Int` com rótulos `0:`, `1:` voltam iguais.

### R-019 [ajuste] Funções de interface
Ajusta: chamada de função e de mensagem de interface (Docs/FORMAT.md > When two nodes share a name)
Esperado: uma função de interface que também tem implementação no Blueprint volta sem ficar ambígua, e uma mensagem de interface volta com o seu Target.

### R-020 [ajuste] Expose on Spawn com a classe vinda de variável
Ajusta: Docs/FORMAT.md > Creating widgets and spawning actors
Esperado: um Spawn Actor ou Create Widget cuja classe vem de uma variável volta com os pinos Expose on Spawn e os seus valores.

### R-021 [ajuste] Comentário com parênteses
Ajusta: Docs/FORMAT.md > Comments
Esperado: um comentário cujo texto tem uma parte entre parênteses volta com ela.
