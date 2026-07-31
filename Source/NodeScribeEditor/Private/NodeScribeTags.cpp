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

			// Aceita a linha como o leitor a escreve, com `tag ` na frente.
			Line.TrimStartAndEndInline();
			Line.RemoveFromStart(TEXT("tag "), ESearchCase::IgnoreCase);
			Line.TrimStartInline();

			if (!Line.IsEmpty())
			{
				Tags.Add(Line);
			}
		}

		return Tags;
	}
}

FString FNodeScribeTags::ListTags(const FString& Filter)
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

FString FNodeScribeTags::AddTags(const FString& Text)
{
	const TArray<FString> Tags = ReadTagLines(Text);

	if (Tags.Num() == 0)
	{
		return TEXT("[erro]: nenhuma tag no texto. Uma por linha.");
	}

	IGameplayTagsEditorModule& TagsEditor =
		FModuleManager::LoadModuleChecked<IGameplayTagsEditorModule>(TEXT("GameplayTagsEditor"));

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	int32 Created = 0;
	TArray<FString> Skipped;
	TArray<FString> Failed;

	for (const FString& Tag : Tags)
	{
		// Ja' existe nao e' erro: colar a mesma lista duas vezes nao deve doer,
		// e o resultado desejado -- a tag existir -- ja' esta' la'.
		if (Manager.RequestGameplayTag(FName(*Tag), /*ErrorIfNotFound*/ false).IsValid())
		{
			Skipped.Add(Tag);
			continue;
		}

		if (TagsEditor.AddNewGameplayTagToINI(Tag))
		{
			++Created;
		}
		else
		{
			Failed.Add(Tag);
		}
	}

	TArray<FString> Report;
	Report.Add(FString::Printf(TEXT("%d tag(s) criada(s)."), Created));

	if (Skipped.Num() > 0)
	{
		Report.Add(FString::Printf(TEXT("[nota]: ja' existiam: %s"),
			*FString::Join(Skipped, TEXT(", "))));
	}

	if (Failed.Num() > 0)
	{
		// Nome invalido e' o caso comum: a Engine recusa espaco e caractere que
		// nao seja letra, numero, ponto ou underscore.
		Report.Add(FString::Printf(
			TEXT("[erro]: a Engine recusou: %s. Tag aceita letras, numeros, ponto e underscore."),
			*FString::Join(Failed, TEXT(", "))));
	}

	return FString::Join(Report, TEXT("\n"));
}
