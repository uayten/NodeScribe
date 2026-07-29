#pragma once

#include "CoreMinimal.h"
#include "NodeScribeTypes.h"

/**
 * Converte o texto colado pelo usuario numa lista plana de statements.
 *
 * O parser e' deliberadamente permissivo: qualquer linha que ele nao entenda
 * vira um statement com NodeExpression igual a linha crua, e quem reclama e'
 * o builder -- que tem contexto para dizer *por que* nao deu, e deixa um
 * comentario vermelho no grafo. Falhar aqui em silencio seria pior.
 */
class FNodeScribeParser
{
public:
	static TArray<FNodeScribeStatement> Parse(const FString& Text, TArray<FNodeScribeDiagnostic>& OutDiagnostics);

private:
	/** Remove `#` ate o fim da linha, respeitando aspas. */
	static FString StripComment(const FString& Line);

	/** Conta a indentacao em niveis. Tab = 1 nivel, cada 2 espacos = 1 nivel. */
	static int32 MeasureIndent(const FString& Line);

	/** Acha o `(` de abertura dos argumentos, ignorando o que estiver entre aspas. */
	static int32 FindArgsOpenParen(const FString& Line);

	/** Quebra `A = 1, B = "x, y"` em partes, respeitando aspas e parenteses. */
	static TArray<FString> SplitArgs(const FString& Inner);

	static FNodeScribeArg ParseArg(const FString& Raw);

	/** Tira aspas simples ou duplas das pontas, se houver. */
	static FString Unquote(const FString& In);
};
