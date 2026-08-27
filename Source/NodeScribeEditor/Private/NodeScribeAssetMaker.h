#pragma once

#include "CoreMinimal.h"

/**
 * Criar asset.
 *
 * Existe por **capacidade, nao por economia**: o toolset nativo da Engine tem
 * `duplicate`, `move` e `delete`, e nao tem criacao. Sem isto, todo asset novo
 * -- um Gameplay Effect de cooldown, uma BTTask, um BTService -- e' um pedido
 * de clique para uma pessoa, e o resto do trabalho para' esperando.
 *
 * Nao economiza token nenhum, e o README diz isso com todas as letras.
 */
class FNodeScribeAssetMaker
{
public:
	/**
	 * @param Path     onde criar, com nome: `/Game/BossRush/Testes/BTTask_Foo`.
	 * @param Parent   o tipo, pelo nome que aparece na tela: `BTTask_BlueprintBase`,
	 *                 `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 * @param Options  propriedades da *factory*, no formato da ficha, aplicadas
	 *                 antes de criar.
	 *
	 * Options existe porque ha' asset que nao se cria so' com o tipo. Um
	 * AnimBlueprint precisa saber o esqueleto, e um BlendSpace tambem; sem isso
	 * a Engine abre um dialogo modal pedindo -- que ninguem clica do outro lado
	 * de uma chamada -- ou cria um asset quebrado. Consertar depois nao serve:
	 * `Skeleton` e' somente-leitura no asset pronto, de proposito.
	 *
	 * A factory e' um UObject como outro qualquer, entao quem escreve nela e' o
	 * mesmo `FNodeScribeObjectWriter` da ficha:
	 *
	 *     CreateAsset("/Game/Anims/ABP_Sophia", "AnimBlueprint",
	 *                 "TargetSkeleton = /Game/MetaHumans/.../metahuman_base_skel")
	 *
	 * @return o caminho do que foi criado, ou o motivo de nao ter dado.
	 *         Nunca sobrescreve: caminho ocupado e' erro, nao substituicao.
	 */
	static FString CreateAsset(const FString& Path, const FString& Parent, const FString& Options = FString());
};
