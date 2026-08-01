#pragma once

#include "CoreMinimal.h"

/**
 * Gameplay Tags: ler e criar.
 *
 * Tag nao e' asset nem propriedade -- vive num ini, `DefaultGameplayTags.ini`
 * ou um arquivo de `Config/Tags/` --, entao nao cabia em `create_asset` nem na
 * ficha. E' o ultimo tipo de coisa que ainda dependia de alguem abrir uma
 * janela de configuracao: um cooldown novo precisa da tag existir antes de o
 * Gameplay Effect poder concede-la.
 *
 * Capacidade, nao economia. Aqui nao havia nem ferramenta nativa para comparar.
 */
class FNodeScribeTags
{
public:
	/** As tags declaradas, filtradas por trecho do nome. Vazio traz todas. */
	static FString ReadTags(const FString& Filter);

	/**
	 * Cria uma tag por linha do texto. Tag que ja' foi declarada e' pulada sem
	 * erro -- colar a mesma lista duas vezes nao deve doer.
	 *
	 * Nao apaga nem renomeia: as duas coisas quebram todo asset que usa a tag, e
	 * isso pede uma decisao, nao um efeito de formatacao.
	 *
	 * @param Source  o arquivo de destino, pelo nome que aparece no editor
	 *                (`BossRush.ini`). Vazio deixa a Engine escolher, que hoje
	 *                da' `DefaultGameplayTags.ini`.
	 */
	static FString WriteTags(const FString& Text, const FString& Source);
};
