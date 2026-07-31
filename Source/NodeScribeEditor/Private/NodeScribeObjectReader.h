#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * Um objeto como ficha: uma linha por propriedade, e so' o que difere do
 * padrao.
 *
 * A compressao vem de nao mandar o que da' para resolver deste lado. Num CDO
 * tipico 95% das propriedades estao no valor de fabrica, e `Gravity Scale =
 * 1.0` nao carrega informacao nenhuma. E' a mesma jogada do catalogo, aplicada
 * a propriedade em vez de a nome de node.
 *
 * O espelho -- escrever ficha de volta -- ainda nao existe. Ler nao estraga
 * asset nenhum, entao vale rodar sozinho enquanto o formato ainda esta'
 * mudando de ideia.
 */
class FNodeScribeObjectReader
{
public:
	/**
	 * @param Object  o alvo. Blueprint e classe viram o CDO delas.
	 * @param Filter  vazio devolve o que difere do padrao. Com texto, devolve
	 *                as propriedades cujo nome casa -- inclusive as que estao
	 *                no padrao, porque ali a pergunta e' "existe e quanto vale",
	 *                nao "o que mudou".
	 */
	static FString ReadObject(UObject* Object, const FString& Filter);
};
