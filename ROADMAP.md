# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-023 -->

## Em teste

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

## Em andamento

## A fazer

### R-022 [feature] write_graph compila e aponta a linha de cada erro
Esperado: depois de colar, os erros de compilação do Blueprint voltam na mesma resposta, cada um apontando a linha do texto que criou o node com problema, sem precisar clicar em Compile nem ler o log.
Como:
- Sem ferramenta nova: o `write_graph` compila ao fim da escrita e acrescenta `line N [error]: <mensagem do compilador>` às notas que já devolve.
- Erro em node que o texto não criou (já estava no grafo) sai sem linha, com o título do node (escolha do agente).
- Caso que motivou: no `NS_Validacao`, `0 Corações` virou a função do widget `Vida` e só o Compile mostrou; o `compile_blueprint` nativo só diz que falhou, e a mensagem saiu do log.
