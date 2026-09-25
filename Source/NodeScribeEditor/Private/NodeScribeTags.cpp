#include "NodeScribeTags.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"

namespace
{
	/** One tag per line, without comments and without empty lines. */
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

			// The header the reader emits (`tags (8)`) is not a tag. Without
			// skipping it, pasting back the output of ReadTags -- the round-trip
			// test, which is what catches the most defects here -- failed on a line
			// written by the plugin itself. There is no ambiguity: a tag has no
			// space in its name.
			if (Line.StartsWith(TEXT("tags "), ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Accepts the line as the reader writes it, with `tag ` in front.
			Line.RemoveFromStart(TEXT("tag "), ESearchCase::IgnoreCase);
			Line.TrimStartInline();

			if (!Line.IsEmpty())
			{
				Tags.Add(Line);
			}
		}

		return Tags;
	}

	/** The sources that can be written to, by the name shown in the editor. */
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
		: FString::Printf(TEXT("tags  ~ \"%s\" (%d of %d)"), *Wanted, Names.Num(), All.Num()));

	for (const FString& Name : Names)
	{
		Lines.Add(TEXT("tag ") + Name);
	}

	if (Names.Num() == 0)
	{
		Lines.Add(TEXT("# none"));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString FNodeScribeTags::WriteTags(const FString& Text, const FString& Source)
{
	const TArray<FString> Tags = ReadTagLines(Text);

	if (Tags.Num() == 0)
	{
		return TEXT("[error]: no tag in the text. One per line.");
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	// A source that does not exist is created by the Engine as a new file under
	// `Config/Tags/`. A typo would become an extra ini nobody asked for and
	// nobody notices -- so it refuses, listing the ones that exist, the way the
	// blackboard does with an unknown key type.
	const FString WantedSource = Source.TrimStartAndEnd();
	FName SourceName = NAME_None;

	if (!WantedSource.IsEmpty())
	{
		SourceName = FName(*WantedSource);

		if (!Manager.FindTagSource(SourceName))
		{
			return FString::Printf(TEXT("[error]: unknown source `%s`. The existing ones: %s"),
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

		// `IsDictionaryTag`, not `RequestGameplayTag`: the question is whether
		// the tag was *declared*, not whether it resolves. `Cooldown.Golem`
		// resolves because `Cooldown.Golem.Jump` exists, but it is not declared
		// in any ini -- and the reader, which only lists declared ones, would
		// never show it. Skipping it because it resolved left the requested tag
		// missing from the listing, silently.
		if (Manager.IsDictionaryTag(TagName))
		{
			Skipped.Add(Tag);
			continue;
		}

		// The Engine has the reason and even a corrected name to suggest.
		// Guessing the cause here already gave wrong advice: accents are accepted
		// -- a tag like `Facção.Inimigos` is valid -- and the old message said to
		// remove them.
		FText Error;
		FString Fixed;
		if (!Manager.IsValidGameplayTagString(Tag, &Error, &Fixed))
		{
			Failed.Add(FString::Printf(TEXT("%s (%s try `%s`)"),
				*Tag, *Error.ToString(), *Fixed));
			continue;
		}

		if (!TagsEditor.AddNewGameplayTagToINI(Tag, /*Comment*/ FString(), SourceName))
		{
			Failed.Add(Tag);
			continue;
		}

		++Created;

		// Which file it landed in comes from the Engine itself, after the fact,
		// instead of repeating its rule for choosing the source here. With an
		// empty `Source` the destination is `DefaultGameplayTags.ini`, which in a
		// project that keeps its tags elsewhere is **not** where the others live
		// -- so naming the file is not a detail.
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
		? FString::Printf(TEXT("%d tag(s) created."), Created)
		: FString::Printf(TEXT("%d tag(s) created in %s."), Created, *WroteTo.ToString()));

	if (Skipped.Num() > 0)
	{
		Report.Add(FString::Printf(TEXT("[note]: already declared: %s"),
			*FString::Join(Skipped, TEXT(", "))));
	}

	if (Failed.Num() > 0)
	{
		Report.Add(FString::Printf(TEXT("[error]: the Engine refused: %s"),
			*FString::Join(Failed, TEXT(", "))));
	}

	return FString::Join(Report, TEXT("\n"));
}
