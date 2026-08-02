#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UClass;
class UObject;

/**
 * De onde a ficha le' e onde ela escreve.
 *
 * Existe pelo mesmo motivo que `NodeScribePropertyText`: leitor e escritor de
 * ficha sao espelhos. Se cada um decidisse sozinho qual objeto carrega os
 * valores, ou quais componentes existem, a ficha sairia falando de um objeto e
 * voltaria mexendo em outro -- e isso grava valor errado num asset, que compila,
 * roda e so' da' as caras em playtest.
 */
namespace NodeScribeObjectTarget
{
	/**
	 * O objeto que carrega os valores.
	 *
	 * Blueprint e classe nao tem valor nenhum em si: quem guarda e' o CDO da
	 * classe compilada. E' de la' que o painel de detalhes le', entao e' de la'
	 * que a ficha tem que ler para dizer a mesma coisa que a tela.
	 */
	UObject* ResolveTarget(UObject* Object);

	/** O Blueprint por tras do alvo, quando ha' um. */
	UBlueprint* FindBlueprint(const UObject* Requested, const UClass* Class);

	/**
	 * Os componentes do alvo, pelo nome com que a ficha os escreve.
	 *
	 * Duas origens, porque um Blueprint guarda em dois lugares: o que veio do
	 * construtor em C++ vive no proprio CDO, e o que foi arrastado no editor
	 * vive como template no SimpleConstructionScript. Ler so' um deles esconde
	 * metade dos componentes sem avisar.
	 */
	TMap<FString, UObject*> CollectComponents(UObject* Target, UBlueprint* Blueprint);
}
