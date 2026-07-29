#pragma once

#include "CoreMinimal.h"

/**
 * Os tres botoes que o NodeScribe acrescenta a' barra do editor de Blueprint.
 *
 * Ficam la' em cima, ao lado de Compile, e nao numa janela separada, porque e'
 * onde a mao ja' esta' quando se olha para um grafo. A janela do NodeScribe
 * continua existindo para quando se quer editar o texto antes de inserir.
 */
class FNodeScribeGraphActions
{
public:
	/**
	 * Agenda o registro para quando o ToolMenus estiver pronto. As barras dos
	 * editores de asset so' existem depois do StartupModule.
	 */
	static void RegisterStartupHook();

	static void Unregister();

	/** Nome do canal do Message Log onde os diagnosticos aparecem. */
	static const FName LogListingName;

private:
	/** Monta a secao nas barras. So' roda depois que o ToolMenus existe. */
	static void RegisterToolbar();
};
