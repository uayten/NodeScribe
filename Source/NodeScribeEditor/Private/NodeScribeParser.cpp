#include "NodeScribeParser.h"

namespace
{
	bool IsQuoteChar(TCHAR C)
	{
		return C == TEXT('"') || C == TEXT('\'');
	}
}

FString FNodeScribeParser::StripComment(const FString& Line)
{
	TCHAR OpenQuote = 0;
	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		const TCHAR C = Line[Index];

		if (OpenQuote != 0)
		{
			if (C == OpenQuote)
			{
				OpenQuote = 0;
			}
			continue;
		}

		if (IsQuoteChar(C))
		{
			OpenQuote = C;
			continue;
		}

		// `//` tambem serve como comentario, porque e' o que sai de um assistente
		// acostumado a escrever codigo.
		if (C == TEXT('#'))
		{
			return Line.Left(Index);
		}
		if (C == TEXT('/') && Index + 1 < Line.Len() && Line[Index + 1] == TEXT('/'))
		{
			return Line.Left(Index);
		}
	}

	return Line;
}

int32 FNodeScribeParser::MeasureIndent(const FString& Line)
{
	int32 Spaces = 0;
	int32 Levels = 0;

	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		const TCHAR C = Line[Index];
		if (C == TEXT('\t'))
		{
			++Levels;
		}
		else if (C == TEXT(' '))
		{
			++Spaces;
		}
		else
		{
			break;
		}
	}

	return Levels + (Spaces / 2);
}

int32 FNodeScribeParser::FindArgsOpenParen(const FString& Line)
{
	TCHAR OpenQuote = 0;
	for (int32 Index = 0; Index < Line.Len(); ++Index)
	{
		const TCHAR C = Line[Index];

		if (OpenQuote != 0)
		{
			if (C == OpenQuote)
			{
				OpenQuote = 0;
			}
			continue;
		}

		if (IsQuoteChar(C))
		{
			OpenQuote = C;
			continue;
		}

		if (C == TEXT('('))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

TArray<FString> FNodeScribeParser::SplitArgs(const FString& Inner)
{
	TArray<FString> Parts;

	TCHAR OpenQuote = 0;
	int32 Depth = 0;
	int32 Start = 0;

	for (int32 Index = 0; Index < Inner.Len(); ++Index)
	{
		const TCHAR C = Inner[Index];

		if (OpenQuote != 0)
		{
			if (C == OpenQuote)
			{
				OpenQuote = 0;
			}
			continue;
		}

		if (IsQuoteChar(C))
		{
			OpenQuote = C;
		}
		else if (C == TEXT('(') || C == TEXT('['))
		{
			++Depth;
		}
		else if (C == TEXT(')') || C == TEXT(']'))
		{
			--Depth;
		}
		else if ((C == TEXT(',') || C == TEXT(';')) && Depth == 0)
		{
			Parts.Add(Inner.Mid(Start, Index - Start));
			Start = Index + 1;
		}
	}

	Parts.Add(Inner.Mid(Start));

	for (int32 Index = Parts.Num() - 1; Index >= 0; --Index)
	{
		Parts[Index].TrimStartAndEndInline();
		if (Parts[Index].IsEmpty())
		{
			Parts.RemoveAt(Index);
		}
	}

	return Parts;
}

FString FNodeScribeParser::Unquote(const FString& In)
{
	if (In.Len() >= 2 && IsQuoteChar(In[0]) && In[In.Len() - 1] == In[0])
	{
		return In.Mid(1, In.Len() - 2);
	}
	return In;
}

FNodeScribeArg FNodeScribeParser::ParseArg(const FString& Raw)
{
	FNodeScribeArg Arg;

	// Procura o `=` (ou `:`) de atribuicao fora de aspas.
	int32 SplitAt = INDEX_NONE;
	TCHAR OpenQuote = 0;
	for (int32 Index = 0; Index < Raw.Len(); ++Index)
	{
		const TCHAR C = Raw[Index];

		if (OpenQuote != 0)
		{
			if (C == OpenQuote)
			{
				OpenQuote = 0;
			}
			continue;
		}

		if (IsQuoteChar(C))
		{
			OpenQuote = C;
			continue;
		}

		if (C == TEXT('=') || C == TEXT(':'))
		{
			SplitAt = Index;
			break;
		}
	}

	FString ValuePart;
	if (SplitAt != INDEX_NONE)
	{
		Arg.PinName = Raw.Left(SplitAt);
		Arg.PinName.TrimStartAndEndInline();
		ValuePart = Raw.Mid(SplitAt + 1);
	}
	else
	{
		// Argumento posicional: o builder casa pela ordem dos pinos de entrada.
		ValuePart = Raw;
	}

	ValuePart.TrimStartAndEndInline();

	if (ValuePart.StartsWith(TEXT("$")))
	{
		Arg.bIsReference = true;
		Arg.Value = ValuePart.Mid(1);
		Arg.Value.TrimStartAndEndInline();
	}
	else
	{
		Arg.Value = Unquote(ValuePart);
	}

	return Arg;
}

TArray<FNodeScribeStatement> FNodeScribeParser::Parse(const FString& Text, TArray<FNodeScribeDiagnostic>& OutDiagnostics)
{
	TArray<FNodeScribeStatement> Statements;

	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"), false);

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Line = Lines[LineIndex];
		Line.ReplaceInline(TEXT("\r"), TEXT(""));

		const int32 LineNumber = LineIndex + 1;
		const FString Original = Line;

		Line = StripComment(Line);

		// Cercas de bloco de markdown aparecem quando se copia uma resposta de chat.
		{
			FString Probe = Line;
			Probe.TrimStartAndEndInline();
			if (Probe.StartsWith(TEXT("```")))
			{
				continue;
			}
		}

		const int32 Indent = MeasureIndent(Line);

		Line.TrimStartAndEndInline();
		if (Line.IsEmpty())
		{
			continue;
		}

		// Marcadores de lista: um assistente costuma escrever `- Print String`.
		if (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* ")))
		{
			Line = Line.Mid(2);
			Line.TrimStartAndEndInline();
		}

		// Numeracao: `1. Print String` / `1) Print String`.
		{
			int32 Digits = 0;
			while (Digits < Line.Len() && FChar::IsDigit(Line[Digits]))
			{
				++Digits;
			}
			if (Digits > 0 && Digits < Line.Len() && (Line[Digits] == TEXT('.') || Line[Digits] == TEXT(')')))
			{
				Line = Line.Mid(Digits + 1);
				Line.TrimStartAndEndInline();
			}
		}

		if (Line.IsEmpty())
		{
			continue;
		}

		FNodeScribeStatement Statement;
		Statement.LineNumber = LineNumber;
		Statement.Indent = Indent;
		Statement.RawLine = Original.TrimStartAndEnd();

		const int32 ParenIndex = FindArgsOpenParen(Line);

		// Rotulo de saida de execucao: termina em `:` e nao tem argumentos.
		if (ParenIndex == INDEX_NONE && Line.EndsWith(TEXT(":")))
		{
			Statement.bIsLabel = true;
			Statement.Label = Line.LeftChop(1);
			Statement.Label.TrimStartAndEndInline();

			if (Statement.Label.IsEmpty())
			{
				OutDiagnostics.Emplace(ENodeScribeSeverity::Error, LineNumber,
					TEXT("Rotulo vazio. Escreva algo como `verdadeiro:` ou `falso:`."));
				continue;
			}

			Statements.Add(MoveTemp(Statement));
			continue;
		}

		FString HeadPart = (ParenIndex == INDEX_NONE) ? Line : Line.Left(ParenIndex);
		FString ArgsPart;

		if (ParenIndex != INDEX_NONE)
		{
			const int32 CloseIndex = Line.FindLastCharByPredicate([](TCHAR C) { return C == TEXT(')'); });
			if (CloseIndex == INDEX_NONE || CloseIndex < ParenIndex)
			{
				OutDiagnostics.Emplace(ENodeScribeSeverity::Error, LineNumber,
					FString::Printf(TEXT("Faltou fechar o parenteses em: %s"), *Line));
				continue;
			}
			ArgsPart = Line.Mid(ParenIndex + 1, CloseIndex - ParenIndex - 1);
		}

		// `pc = Get Player Controller` -- so vale o `=` antes dos argumentos.
		{
			int32 AssignAt = INDEX_NONE;
			TCHAR OpenQuote = 0;
			for (int32 Index = 0; Index < HeadPart.Len(); ++Index)
			{
				const TCHAR C = HeadPart[Index];

				if (OpenQuote != 0)
				{
					if (C == OpenQuote)
					{
						OpenQuote = 0;
					}
					continue;
				}

				if (IsQuoteChar(C))
				{
					OpenQuote = C;
					continue;
				}

				if (C == TEXT('='))
				{
					// `==` e `=>` nao sao atribuicao.
					const bool bNextIsEquals = (Index + 1 < HeadPart.Len()) && (HeadPart[Index + 1] == TEXT('=') || HeadPart[Index + 1] == TEXT('>'));
					const bool bPrevIsOperator = (Index > 0) && (HeadPart[Index - 1] == TEXT('!') || HeadPart[Index - 1] == TEXT('<') || HeadPart[Index - 1] == TEXT('>'));
					if (!bNextIsEquals && !bPrevIsOperator)
					{
						AssignAt = Index;
					}
					break;
				}
			}

			if (AssignAt != INDEX_NONE)
			{
				Statement.OutputName = HeadPart.Left(AssignAt).TrimStartAndEnd();
				HeadPart = HeadPart.Mid(AssignAt + 1);
			}
		}

		HeadPart.TrimStartAndEndInline();
		Statement.NodeExpression = HeadPart;

		if (Statement.NodeExpression.IsEmpty())
		{
			OutDiagnostics.Emplace(ENodeScribeSeverity::Error, LineNumber,
				FString::Printf(TEXT("Nao achei o nome do node em: %s"), *Statement.RawLine));
			continue;
		}

		for (const FString& RawArg : SplitArgs(ArgsPart))
		{
			Statement.Args.Add(ParseArg(RawArg));
		}

		Statements.Add(MoveTemp(Statement));
	}

	return Statements;
}
