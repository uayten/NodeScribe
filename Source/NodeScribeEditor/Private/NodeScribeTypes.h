#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNodeScribe, Log, All);

/** Gravidade de uma mensagem devolvida ao usuario depois de transcrever. */
enum class ENodeScribeSeverity : uint8
{
	/** Deu certo, mas vale saber (ex.: "escolhi PrintString de KismetSystemLibrary"). */
	Info,
	/** O node entrou, mas falta decisao sua (ex.: pino de asset vazio). */
	Warning,
	/** Nao consegui. Um comentario vermelho foi deixado no grafo no lugar. */
	Error
};

struct FNodeScribeDiagnostic
{
	ENodeScribeSeverity Severity = ENodeScribeSeverity::Info;

	/** Linha do texto de entrada (1-based). 0 = mensagem geral. */
	int32 Line = 0;

	FString Message;

	FNodeScribeDiagnostic() = default;

	FNodeScribeDiagnostic(ENodeScribeSeverity InSeverity, int32 InLine, FString InMessage)
		: Severity(InSeverity)
		, Line(InLine)
		, Message(MoveTemp(InMessage))
	{
	}
};

/** Um argumento entre parenteses: `In String = "ola"` ou `Target = $pc`. */
struct FNodeScribeArg
{
	/** Nome do pino. Vazio = argumento posicional. */
	FString PinName;

	/** Valor literal, ou o nome referenciado quando bIsReference. */
	FString Value;

	/** true quando o valor veio escrito como `$algo`. */
	bool bIsReference = false;
};

/**
 * Uma linha do script, ja quebrada em partes.
 * A estrutura e' plana de proposito: o aninhamento e' reconstruido pelo builder
 * a partir de Indent, o que evita um tipo recursivo e simplifica o parser.
 */
struct FNodeScribeStatement
{
	/** Linha original no texto (1-based), usada para apontar erros. */
	int32 LineNumber = 0;

	/** Nivel de indentacao (contagem de niveis, nao de espacos). */
	int32 Indent = 0;

	/** A linha como o usuario escreveu, para reproduzir em comentario de erro. */
	FString RawLine;

	/** true quando a linha e' so um rotulo de saida de execucao, ex.: `verdadeiro:`. */
	bool bIsLabel = false;

	/** Texto do rotulo, sem os dois pontos. */
	FString Label;

	/** Nome dado a saida principal, de `pc = Get Player Controller`. */
	FString OutputName;

	/** O que criar: `Get Player Controller`, `Branch`, `Cast to BP_Boss`... */
	FString NodeExpression;

	TArray<FNodeScribeArg> Args;
};
