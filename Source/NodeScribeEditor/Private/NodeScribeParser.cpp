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

		// Anotacao de conversao que o leitor escreve: `$c.Device Id (Integer ->
		// Int64)`. Ela diz que a Unreal poe um node de conversao ali -- coisa que
		// o builder refaz sozinho ao ligar os pinos, entao aqui e' texto para
		// pessoa ler. Sem descartar, o `.Pino` sairia daqui com a anotacao colada
		// no nome e nao acharia pino nenhum.
		int32 NoteStart = INDEX_NONE;
		if (Arg.Value.EndsWith(TEXT(")")) && Arg.Value.FindLastChar(TEXT('('), NoteStart))
		{
			const FString Note = Arg.Value.RightChop(NoteStart);
			if (Note.Contains(TEXT("->")))
			{
				Arg.Value = Arg.Value.Left(NoteStart).TrimEnd();
			}
		}
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

		// Numeracao de passo, em todas as formas que um assistente costuma usar:
		// `1. Print String`, `1) Print String`, `[2] Print String`, `3.1 Print
		// String`. Sao rotulos de leitura humana; o node comeca depois deles.
		{
			int32 Cursor = 0;

			const bool bBracketed = Line.Len() > 0 && Line[0] == TEXT('[');
			if (bBracketed)
			{
				++Cursor;
			}

			const int32 NumberStart = Cursor;
			while (Cursor < Line.Len() && (FChar::IsDigit(Line[Cursor]) || Line[Cursor] == TEXT('.')))
			{
				++Cursor;
			}

			const bool bHasDigits = Cursor > NumberStart;

			if (bHasDigits && Cursor < Line.Len())
			{
				const TCHAR Terminator = Line[Cursor];

				const bool bValidEnd = bBracketed
					? (Terminator == TEXT(']'))
					: (Terminator == TEXT('.') || Terminator == TEXT(')') || Terminator == TEXT(' '));

				if (bValidEnd)
				{
					// O espaco nao faz parte do rotulo; os outros terminadores sim.
					Line = Line.Mid(Terminator == TEXT(' ') ? Cursor : Cursor + 1);
					Line.TrimStartAndEndInline();
				}
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

		// Declaracao de variavel: `variavel Vida : Float = 100`.
		//
		// Vem antes do rotulo porque `:` aparece nas duas formas -- aqui no meio
		// da linha, la' no fim. O prefixo desambigua sem depender disso.
		{
			static const TCHAR* const VariableKeywords[] = {
				TEXT("variavel "), TEXT("variável "), TEXT("variable "), TEXT("var ")
			};

			for (const TCHAR* Keyword : VariableKeywords)
			{
				if (!Line.StartsWith(Keyword, ESearchCase::IgnoreCase))
				{
					continue;
				}

				FString Rest = Line.RightChop(FCString::Strlen(Keyword));
				Rest.TrimStartAndEndInline();

				Statement.bIsVariable = true;

				FString NameAndType = Rest;
				if (Rest.Split(TEXT("="), &NameAndType, &Statement.VariableDefault))
				{
					Statement.VariableDefault = Unquote(Statement.VariableDefault.TrimStartAndEnd());
				}

				if (!NameAndType.Split(TEXT(":"), &Statement.VariableName, &Statement.VariableType))
				{
					OutDiagnostics.Emplace(ENodeScribeSeverity::Error, LineNumber,
						TEXT("Declaracao sem tipo. Escreva `variavel Nome : Tipo`."));
					Statement.bIsVariable = false;
				}

				Statement.VariableName.TrimStartAndEndInline();
				Statement.VariableType.TrimStartAndEndInline();
				break;
			}

			if (Statement.bIsVariable)
			{
				Statements.Add(MoveTemp(Statement));
				continue;
			}
		}

		const int32 ParenIndex = FindArgsOpenParen(Line);

		// Rotulo de bloco: termina em `:`.
		//
		// Com parenteses fechando logo antes dos dois pontos, o que esta' dentro
		// deles e' argumento, como em qualquer outra linha -- `estado Pulo
		// (Always Reset on Entry = true):`. Um estado e uma transicao tem opcao
		// de painel do mesmo jeito que um asset player tem `Loop Animation`, e
		// inventar outra sintaxe para elas daria duas gramaticas para a mesma
		// coisa. Sem o `)` na ponta nada muda: uma linha com parenteses no meio
		// que por acaso termine em `:` continua sendo lida como node, que e'
		// como sempre foi.
		const bool bLabelHasArgs = ParenIndex != INDEX_NONE && Line.EndsWith(TEXT("):"));

		if (Line.EndsWith(TEXT(":")) && (ParenIndex == INDEX_NONE || bLabelHasArgs))
		{
			Statement.bIsLabel = true;
			Statement.Label = Line.LeftChop(1);
			Statement.Label.TrimStartAndEndInline();

			if (bLabelHasArgs)
			{
				const FString Inner =
					Statement.Label.Mid(ParenIndex + 1, Statement.Label.Len() - ParenIndex - 2);

				for (const FString& Raw : SplitArgs(Inner))
				{
					Statement.Args.Add(ParseArg(Raw));
				}

				Statement.Label = Statement.Label.Left(ParenIndex).TrimStartAndEnd();
			}

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
