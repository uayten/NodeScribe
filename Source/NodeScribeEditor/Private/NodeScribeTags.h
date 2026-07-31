#pragma once

#include "CoreMinimal.h"

/**
 * Gameplay Tags: listar e criar.
 *
 * Tag nao e' asset nem propriedade -- vive em `Config/DefaultGameplayTags.ini`
 * --, entao nao cabia em `create_asset` nem na ficha. E' o ultimo tipo de coisa
 * que ainda dependia de alguem abrir uma janela de configuracao: um cooldown
 * novo precisa da tag existir antes de o Gameplay Effect poder concede-la.
 *
 * Capacidade, nao economia. Aqui nao havia nem ferramenta nativa para comparar.
 */
class FNodeScribeTags
{
public:
	/** As tags que existem, filtradas por trecho do nome. Vazio traz todas. */
	static FString ListTags(const FString& Filter);

	/**
	 * Cria uma tag por linha do texto. Tag que ja' existe e' pulada sem erro --
	 * colar a mesma lista duas vezes nao deve doer.
	 */
	static FString AddTags(const FString& Text);
};
