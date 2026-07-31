#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * O espelho da ficha: texto entra, propriedade muda.
 *
 * Aceita o que `FNodeScribeObjectReader` produz, sem os comentarios -- bloco de
 * componente, membro de struct recuado, e `= padrao` para voltar ao valor de
 * fabrica.
 *
 * So' mexe no que a linha pede. Nao cria variavel, nao cria componente, nao
 * apaga nada: o texto e' uma lista de mudancas, nao uma descricao do estado
 * final. Colar uma ficha inteira de volta nao deve mexer em nada.
 *
 * Nada aqui adivinha. Nome de propriedade que nao resolve vira diagnostico com
 * os candidatos parecidos, e as outras linhas continuam sendo aplicadas -- uma
 * propriedade escrita no lugar errado e' o erro caro aqui, porque grava no
 * asset e so' aparece rodando.
 */
class FNodeScribeObjectWriter
{
public:
	struct FResult
	{
		int32 Applied = 0;
		TArray<FString> Diagnostics;
	};

	static FResult WriteObject(UObject* Object, const FString& Text);
};
