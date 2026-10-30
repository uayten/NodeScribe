# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-024 -->

## Em teste

### R-017 Pinos de struct divididos em Spawn Actor e Set Color and Opacity
1. Abrir `Content/Vefects/Stylized_VFX/UI/UI`, aba Graph, clicar em Copy Whole Graph e colar o texto no chat: o agente confere os parâmetros dos eventos e o Set Color and Opacity com os pinos abertos.

### R-020 Expose on Spawn com a classe vinda de variável
1. Abrir `Content/BossRush/Testes/NS_Validacao`, aba Event Graph, evento `Teste Classe`: a classe do Spawn Actor from Class vem da variável `Classe Gancho`, e o pino Spawner Do Projétil está ligado no Self.

### R-021 Comentário com parênteses
1. No mesmo grafo, procurar o comentário: o texto aparece inteiro, com "(pode apagar depois):".

## Em andamento

## A fazer

### R-023 [ajuste] Evento depois de uma cadeia com ramos fica em cima dos nodes dela
Ajusta: posição dos nodes ao colar (Docs/FORMAT.md > Everything not written is the plugin's job)
Esperado: ao colar, nenhum evento fica em cima de outro node.
Como:
- Reproduz com `Saved/teste_r016_r021.txt` em `BP_TesteGolem`: o evento `Recebeu Golpe`, que vem depois do `Switch on String`, ficou em cima do Print String "zero" (ramo `0.0:`).
