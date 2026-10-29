# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-016 -->

## Em teste

### R-003 Adicionar componentes a um Blueprint pelo texto
1. No Content Browser, abrir a pasta BossRush/Testes e dar duplo clique em BP_TesteGolem: no painel Components aparecem AbilitySystem e Hitbox, e Hitbox fica pendurado em CharacterMesh0.
2. Clicar em Hitbox: no painel Details, Box Extent está em 40, 40, 60.

### R-004 Input Mapping Context como texto
1. Na mesma pasta, abrir IMC_Teste: a lista de mapeamentos tem as mesmas ações e teclas do ActionInputMapping, mais E em Ataque, e Movimentação não tem a tecla W.
2. Em Ataque, abrir a tecla E: em Triggers aparece Tap.
3. Em Movimentação, abrir Gamepad Left Thumbstick 2D-Axis: em Modifiers aparecem To World Space e Dead Zone, com Lower Threshold em 0,3.

### R-005 Mover e reordenar widgets no Designer
1. Abrir WBP_TesteDesigner, aba Designer: na Hierarchy, debaixo de Root, BatteryRow vem antes de TemperatureRow, e na prévia a linha "Battery Volts" aparece acima de "Temperature".
2. Na Hierarchy, abrir HorizontalBox: Footer é o último item, depois de IsStillAnswer, e na prévia o "Text Block" aparece na mesma linha de "Is Still?".
3. Clicar em Compile: compila sem erro.

### R-006 Conteúdo de named slots no Designer
1. Ainda no WBP_TesteDesigner, na Hierarchy, abrir Framed: dentro do slot Body estão FramedRow e FramedText, e na prévia "Inside the slot" aparece logo abaixo de "Host".
2. Abrir WBP_TesteSlotChild, aba Designer: a Hierarchy mostra os slots Body e Foot preenchidos, e na prévia aparecem "Host", "From the child" e "Text Block", nessa ordem.

### R-007 Componentes de Gameplay Effect
1. Abrir GE_TesteCooldown, na mesma pasta: Duration Policy é Has Duration, com 7 na duração.
2. Em Components: há um Grant Tags to Target Actor, com Cooldown.Golem.Salto em Add Tags.

## Em andamento

## A fazer

### R-015 [feature] Inserir nodes numa cadeia de execução que já existe
Esperado: pelo texto, acrescentar um node no meio ou no fim de uma cadeia de execução de um grafo existente, sem apagar e recriar o grafo inteiro.
Fatos de partida (JoyShockLibrary4Unreal, 29/10/2026):
- `write_graph` sem `replace` só cria cadeias novas; não há como dizer "depois deste node que já existe".
- `write_graph` com `replace` recusa no EventGraph de `/JoyShockLibrary4Unreal/UI/WBP_ConnectedJoyShockInfo`, porque a leitura perde duas reconvergências: `event Construct` e `event OnClicked of HeaderButton` entram no mesmo `IsValid`. O formato descreve árvores, então esse grafo não tem caminho de reescrita.
- O caso que ficou travado: uma linha Battery Current no painel do demo, que precisa de um `TextBlock.SetText` dentro do evento `Update Sensor Readout`, no ramo `True` do Branch, logo depois do `SetText` de `BatteryVoltsAnswer` (R-031 do JoyShockLibrary4Unreal). Bom caso de teste.
Em aberto: a sintaxe para apontar o node existente (pelo nome que a leitura dá, como `$jSL4UGetSwitch2Sensors`, ou por um `after`/`before`), e se vale também fazer o `replace` preservar reconvergências, o que resolveria o mesmo caso por outro lado.
