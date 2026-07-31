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
	 * @param Path    onde criar, com nome: `/Game/BossRush/Testes/BTTask_Foo`.
	 * @param Parent  o tipo, pelo nome que aparece na tela: `BTTask_BlueprintBase`,
	 *                `GameplayEffect`, `BlackboardData`, `BehaviorTree`.
	 *
	 * @return o caminho do que foi criado, ou o motivo de nao ter dado.
	 *         Nunca sobrescreve: caminho ocupado e' erro, nao substituicao.
	 */
	static FString CreateAsset(const FString& Path, const FString& Parent);
};
