#pragma once

#include "CoreMinimal.h"

class FProperty;
struct FEdGraphPinType;

/**
 * Valor de propriedade <-> texto, e o vocabulario de nomes que os dois lados
 * usam.
 *
 * Existe porque leitor e escritor sao espelhos. Se cada um formatasse do seu
 * jeito, o texto sairia de um e nao voltaria pelo outro -- e no caso de
 * propriedade a diferenca aparece como valor gravado errado num asset, que
 * compila, roda e so' da' as caras em playtest. Tudo que traduz valor mora
 * aqui, uma vez.
 *
 * `DescribePinType` e as aspas vieram do NodeScribeReader por isso mesmo: o
 * tipo que a ficha escreve tem que ser o mesmo que o grafo escreve, senao o
 * usuario aprende dois vocabularios para a mesma coisa.
 */
namespace NodeScribePropertyText
{
	// -- Aspas ---------------------------------------------------------------

	/** Caracteres que mudariam o sentido da linha se ficassem soltos. */
	bool NeedsQuotes(const FString& Value);

	/**
	 * O parser nao tem sequencia de escape: uma aspa dentro do valor quebraria a
	 * linha. Escolhemos a aspa que nao aparece no texto; se as duas aparecerem,
	 * quem chama avisa em vez de emitir algo que nao volta.
	 */
	bool TryQuote(const FString& Value, FString& OutQuoted);

	// -- Nomes ---------------------------------------------------------------

	/**
	 * O tipo como o formato escreve: `Float`, `Integer`, `TimerHandle`,
	 * `Array de Name`. Vazio quando o tipo nao tem forma no texto.
	 */
	FString DescribePinType(const FEdGraphPinType& PinType);

	/** O mesmo, partindo de uma propriedade. Vazio quando nao tem forma. */
	FString DescribeType(const FProperty* Property);

	/** O nome que aparece no painel de detalhes: `bShowMouseCursor` -> `Show Mouse Cursor`. */
	FString DisplayName(const FProperty* Property);

	// -- Visibilidade --------------------------------------------------------

	/**
	 * true quando a propriedade entra na ficha.
	 *
	 * Entra o que aparece no painel de detalhes (CPF_Edit) *ou* o que tem node
	 * de Get no grafo (CPF_BlueprintVisible). A maioria tem os dois carimbos,
	 * mas nao todas, e as excecoes sao justamente as que importam:
	 * `ACharacter::bIsCrouched` e' BlueprintReadOnly sem Edit -- nao aparece no
	 * painel e e' lida no grafo o tempo todo. Um filtro so' de painel
	 * responderia "nao achei" para ela.
	 *
	 * Sai a transiente, que nem e' salva em disco: `APawn::LastHitBy` e' estado
	 * de execucao, nao configuracao, e numa ficha seria ruido que muda sozinho.
	 */
	bool IsVisible(const FProperty* Property);

	// -- Valor ---------------------------------------------------------------

	/**
	 * Valor -> texto pronto para ir depois do `=`, ja' com aspas se precisar.
	 *
	 * @return false quando o valor nao tem representacao que volte igual. Quem
	 *         chama avisa; ninguem emite um texto que parece certo e volta
	 *         diferente.
	 */
	bool ValueToText(const FProperty* Property, const void* ValuePtr, FString& OutText);

	/**
	 * Texto -> valor, o espelho exato de ValueToText.
	 *
	 * @return false com o motivo em OutError. O valor fica intocado no erro.
	 */
	bool TextToValue(const FProperty* Property, void* ValuePtr, const FString& Text, FString& OutError);

	/**
	 * true quando o valor difere do arquetipo -- a pergunta que a ficha faz de
	 * cada propriedade, e a razao de ela caber numa tela.
	 */
	bool DiffersFromDefault(const FProperty* Property, const void* ValuePtr, const void* DefaultPtr);
}
