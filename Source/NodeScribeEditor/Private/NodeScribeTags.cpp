#include "NodeScribeTags.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"

namespace
{
	/** Uma tag por linha, sem comentario e sem linha vazia. */
	TArray<FString> ReadTagLines(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);

		TArray<FString> Tags;
		for (FString& Line : Lines)
		{
			int32 Hash = INDEX_NONE;
			if (Line.FindChar(TEXT('#'), Hash))
			{
				Line = Line.Left(Hash);
			}

			Line.TrimStartAndEndInline();

			// O cabecalho que o leitor emite (`tags (8)`) nao e' tag. Sem pular,
			// colar de volta a saida de ReadTags -- o teste de ida e volta, que
			// e' o que mais pega defeito aqui -- falhava numa linha escrita pelo
			// proprio plugin. Nao ha' ambiguidade: tag nao tem espaco no nome.
			if (Line.StartsWith(TEXT("tags "), ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Aceita a linha como o leitor a escreve, com `tag ` na frente.
			Line.RemoveFromStart(TEXT("tag "), ESearchCase::IgnoreCase);
			Line.TrimStartInline();

			if (!Line.IsEmpty())
			{
				Tags.Add(Line);
			}
		}

		return Tags;
	}

	/** As fontes onde da' para gravar, pelo nome que aparece no editor. */
	FString KnownSources(const UGameplayTagsManager& Manager)
	{
		const EGameplayTagSourceType Writable[] =
		{
			EGameplayTagSourceType::DefaultTagList,
			EGameplayTagSourceType::TagList
		};

		TArray<FString> Names;
		for (EGameplayTagSourceType Type : Writable)
		{
			TArray<const FGameplayTagSource*> Sources;
			Manager.FindTagSourcesWithType(Type, Sources);

			for (const FGameplayTagSource* Source : Sources)
			{
				Names.Add(Source->SourceName.ToString());
			}
		}

		Names.Sort();
		return FString::Join(Names, TEXT(", "));
	}
}

FString FNodeScribeTags::ReadTags(const FString& Filter)
{
	FGameplayTagContainer All;
	UGameplayTagsManager::Get().RequestAllGameplayTags(All, /*OnlyIncludeDictionaryTags*/ true);

	const FString Wanted = Filter.TrimStartAndEnd();

	TArray<FString> Names;
	for (const FGameplayTag& Tag : All)
	{
		const FString Name = Tag.ToString();
		if (Wanted.IsEmpty() || Name.Contains(Wanted, ESearchCase::IgnoreCase))
		{
			Names.Add(Name);
		}
	}

	Names.Sort();

	TArray<FString> Lines;
	Lines.Add(Wanted.IsEmpty()
		? FString::Printf(TEXT("tags (%d)"), Names.Num())
		: FString::Printf(TEXT("tags  ~ \"%s\" (%d de %d)"), *Wanted, Names.Num(), All.Num()));

	for (const FString& Name : Names)
	{
		Lines.Add(TEXT("tag ") + Name);
	}

	if (Names.Num() == 0)
	{
		Lines.Add(TEXT("# nenhuma"));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString FNodeScribeTags::WriteTags(const FString& Text, const FString& Source)
{
	const TArray<FString> Tags = ReadTagLines(Text);

	if (Tags.Num() == 0)
	{
		return TEXT("[erro]: nenhuma tag no texto. Uma por linha.");
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	// Fonte que nao existe a Engine cria como arquivo novo em `Config/Tags/`.
	// Um erro de digitacao viraria um ini a mais que ninguem pediu e ninguem
	// percebe -- entao recusa listando as que existem, como o blackboard faz
	// com tipo de chave desconhecido.
	const FString WantedSource = Source.TrimStartAndEnd();
	FName SourceName = NAME_None;

	if (!WantedSource.IsEmpty())
	{
		SourceName = FName(*WantedSource);

		if (!Manager.FindTagSource(SourceName))
		{
			return FString::Printf(TEXT("[erro]: nao conheco a fonte `%s`. As que existem: %s"),
				*WantedSource, *KnownSources(Manager));
		}
	}

	IGameplayTagsEditorModule& TagsEditor =
		FModuleManager::LoadModuleChecked<IGameplayTagsEditorModule>(TEXT("GameplayTagsEditor"));

	int32 Created = 0;
	TArray<FString> Skipped;
	TArray<FString> Failed;
	FName WroteTo = NAME_None;

	for (const FString& Tag : Tags)
	{
		const FName TagName(*Tag);

		// `IsDictionaryTag`, e nao `RequestGameplayTag`: a pergunta e' se a tag
		// foi *declarada*, nao se ela resolve. `Cooldown.Golem` resolve porque
		// `Cooldown.Golem.Salto` existe, mas nao esta' declarada em ini nenhum
		// -- e o leitor, que so' lista as declaradas, nunca a mostraria. Pular
		// por resolver deixava a tag pedida sem aparecer na listagem, calado.
		if (Manager.IsDictionaryTag(TagName))
		{
			Skipped.Add(Tag);
			continue;
		}

		// A Engine tem o motivo e ate' um nome corrigido para sugerir. Chutar a
		// causa aqui ja' deu conselho errado: acento e' aceito -- este projeto
		// tem `Facção.Inimigos` -- e a mensagem antiga mandava tirar.
		FText Error;
		FString Fixed;
		if (!Manager.IsValidGameplayTagString(Tag, &Error, &Fixed))
		{
			Failed.Add(FString::Printf(TEXT("%s (%s tente `%s`)"),
				*Tag, *Error.ToString(), *Fixed));
			continue;
		}

		if (!TagsEditor.AddNewGameplayTagToINI(Tag, /*Comment*/ FString(), SourceName))
		{
			Failed.Add(Tag);
			continue;
		}

		++Created;

		// Em que arquivo caiu sai da propria Engine, depois do fato, em vez de
		// repetir aqui a regra dela para escolher a fonte. Com `Source` vazio o
		// destino e' `DefaultGameplayTags.ini`, que neste projeto **nao** e'
		// onde moram as outras tags -- entao dizer o arquivo nao e' detalhe.
		if (WroteTo.IsNone())
		{
			FString Comment;
			bool bExplicit = false;
			bool bRestricted = false;
			bool bAllowsChildren = false;
			Manager.GetTagEditorData(TagName, Comment, WroteTo, bExplicit, bRestricted, bAllowsChildren);
		}
	}

	TArray<FString> Report;
	Report.Add(WroteTo.IsNone()
		? FString::Printf(TEXT("%d tag(s) criada(s)."), Created)
		: FString::Printf(TEXT("%d tag(s) criada(s) em %s."), Created, *WroteTo.ToString()));

	if (Skipped.Num() > 0)
	{
		Report.Add(FString::Printf(TEXT("[nota]: ja' declaradas: %s"),
			*FString::Join(Skipped, TEXT(", "))));
	}

	if (Failed.Num() > 0)
	{
		Report.Add(FString::Printf(TEXT("[erro]: a Engine recusou: %s"),
			*FString::Join(Failed, TEXT(", "))));
	}

	return FString::Join(Report, TEXT("\n"));
}
