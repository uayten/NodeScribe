#include "NodeScribeObjectReader.h"

#include "NodeScribeAIReader.h"
#include "NodeScribeBlendSpace.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeObjectTarget.h"
#include "NodeScribePropertyText.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

using namespace NodeScribeObjectTarget;

namespace
{
	using namespace NodeScribePropertyText;

	/** One property line before it becomes aligned text. */
	struct FEntry
	{
		FString Left;      // `Max Walk Speed = 250`, or with the type in filtered mode
		FString Default;   // factory value, empty when it did not change or there is none
		int32 Depth = 0;   // a struct member goes indented under it
	};

	/** `BP_Golem_C` -> `BP_Golem`. The suffix belongs to compilation, not to the name. */
	FString CleanClassName(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}

	/**
	 * What to call the target in the header.
	 *
	 * What the user recognises is the asset's name -- `BT_Golem`, not
	 * `BehaviorTree`. When the target came as a Blueprint's CDO, the CDO is
	 * called `Default__BP_Golem_C`, so the name of what was asked for wins.
	 */
	FString DescribeTargetName(const UObject* Requested, const UObject* Target)
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Requested))
		{
			return Blueprint->GetName();
		}

		if (const UClass* Class = Cast<UClass>(Requested))
		{
			return CleanClassName(Class);
		}

		return Target->GetName();
	}

	/**
	 * The inheritance chain up to Object, without it.
	 *
	 * Everyone inherits from Object; saying so in every sheet is a line that
	 * never informs anything.
	 */
	FString DescribeAncestry(const UClass* Class)
	{
		TArray<FString> Names;

		for (const UClass* Super = Class->GetSuperClass(); Super; Super = Super->GetSuperClass())
		{
			if (Super == UObject::StaticClass())
			{
				break;
			}
			Names.Add(CleanClassName(Super));
		}

		return FString::Join(Names, TEXT(" < "));
	}

	/**
	 * This property's factory value, if there is something to compare with.
	 *
	 * The archetype may belong to a class that does not even know the property
	 * -- it is the case of the variables the Blueprint itself declares, which
	 * do not exist in the parent. Reading the same offset there would be reading
	 * the memory of something else.
	 */
	const void* FindDefaultValuePtr(const FProperty* Property, const UObject* Archetype)
	{
		if (!Archetype)
		{
			return nullptr;
		}

		const FProperty* DefaultProperty =
			Archetype->GetClass()->FindPropertyByName(Property->GetFName());

		if (!DefaultProperty || !DefaultProperty->SameType(Property))
		{
			return nullptr;
		}

		return DefaultProperty->ContainerPtrToValuePtr<void>(Archetype);
	}

	/**
	 * Aligns the comments in a single column.
	 *
	 * The point of the sheet is for you to glance and see what changed; with the
	 * `# default` comments in different positions, the column that matters gets
	 * jagged and disappears.
	 */
	void AppendAligned(TArray<FString>& OutLines, const TArray<FEntry>& Entries)
	{
		// A cap on the column. Without it, one wide line pushes the comment of
		// all the others to the same distance -- and a collision struct produces
		// lines thousands of characters long, so the neighbours got thousands of
		// spaces. Alignment is for reading; beyond that it gets in the way and costs.
		const int32 MaxColumn = 64;

		auto Indented = [](const FEntry& Entry)
		{
			return FString::ChrN(Entry.Depth * 2, TEXT(' ')) + Entry.Left;
		};

		int32 Widest = 0;
		for (const FEntry& Entry : Entries)
		{
			const int32 Length = Indented(Entry).Len();
			if (!Entry.Default.IsEmpty() && Length <= MaxColumn)
			{
				Widest = FMath::Max(Widest, Length);
			}
		}

		for (const FEntry& Entry : Entries)
		{
			const FString Left = Indented(Entry);

			if (Entry.Default.IsEmpty())
			{
				OutLines.Add(Left);
				continue;
			}

			const int32 Padding = FMath::Max(1, Widest - Left.Len() + 1);
			OutLines.Add(Left + FString::ChrN(Padding, TEXT(' '))
				+ TEXT("# default ") + Entry.Default);
		}
	}
}

namespace
{
	/** What a pass over the properties produced, besides the lines. */
	struct FCollectStats
	{
		int32 AtDefault = 0;
		int32 Considered = 0;
		TArray<FString> Unreadable;
		TArray<FString> TooLong;
	};

	/**
	 * Above this the value stops informing and starts hiding.
	 *
	 * A capsule's `Body Instance` comes out with the whole collision response
	 * table: ~2,000 characters, plus another ~2,000 of the factory value next
	 * to it. On its own it was bigger than BP_Golem's whole sheet -- exactly the
	 * dump this format exists not to do.
	 *
	 * The cut only applies to the overview. Whoever filters by the property's
	 * name is asking for that, and then it comes out whole.
	 */
	const int32 MaxSurveyValueLength = 160;

	/** How far to open a struct inside a struct before giving up and just warning. */
	const int32 MaxStructDepth = 3;

	/**
	 * Opens a struct and emits only the members that changed.
	 *
	 * It is the sheet's principle, one level down: `Body Instance` differs from
	 * the default in three fields, not in the fifty the flat form dumps.
	 *
	 * It is only called when the flat form went over the cap. Always opening
	 * would cost readability on small structs -- `Relative Location` is worth
	 * more as one line than as three, and a lone `Z` would lose the company of
	 * the `X` and `Y` that say it is a position.
	 *
	 * @return how many members went in. Zero means opening did not help, and the
	 *         caller undoes it.
	 */
	int32 CollectStructMembers(const FStructProperty* StructProperty,
		const void* ValuePtr, const void* DefaultPtr, int32 Depth,
		const FString& Context, TArray<FEntry>& OutEntries, FCollectStats& Stats)
	{
		int32 Added = 0;

		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* Member = *It;
			if (!IsVisible(Member))
			{
				continue;
			}

			const void* MemberValue = Member->ContainerPtrToValuePtr<void>(ValuePtr);
			const void* MemberDefault = DefaultPtr
				? Member->ContainerPtrToValuePtr<void>(DefaultPtr) : nullptr;

			if (!DiffersFromDefault(Member, MemberValue, MemberDefault))
			{
				continue;
			}

			const FString Name = DisplayName(Member);
			const FString Qualified = Context + TEXT(".") + Name;

			FString Value;
			if (!ValueToText(Member, MemberValue, Value))
			{
				Stats.Unreadable.Add(Qualified);
				continue;
			}

			if (Value.Len() > MaxSurveyValueLength)
			{
				const FStructProperty* Inner = CastField<FStructProperty>(Member);

				if (Inner && Depth < MaxStructDepth)
				{
					const int32 Before = OutEntries.Num();
					const int32 TooLongBefore = Stats.TooLong.Num();
					const int32 UnreadableBefore = Stats.Unreadable.Num();

					FEntry Head;
					Head.Left = Name + TEXT(":");
					Head.Depth = Depth;
					OutEntries.Add(Head);

					const int32 InnerAdded = CollectStructMembers(Inner, MemberValue,
						MemberDefault, Depth + 1, Qualified, OutEntries, Stats);

					if (InnerAdded > 0)
					{
						Added += InnerAdded;
						continue;
					}

					// Opening did not help: undo everything, including what the
					// attempt recorded. Without this the note names the inner
					// member and the outer one, which are the same thing said twice.
					OutEntries.SetNum(Before);
					Stats.TooLong.SetNum(TooLongBefore);
					Stats.Unreadable.SetNum(UnreadableBefore);
				}

				Stats.TooLong.Add(Qualified);
				continue;
			}

			FEntry Entry;
			Entry.Left = FString::Printf(TEXT("%s = %s"), *Name, *Value);
			Entry.Depth = Depth;

			FString DefaultText;
			if (MemberDefault && ValueToText(Member, MemberDefault, DefaultText)
				&& DefaultText.Len() <= MaxSurveyValueLength)
			{
				Entry.Default = DefaultText;
			}

			OutEntries.Add(MoveTemp(Entry));
			++Added;
		}

		return Added;
	}

	/**
	 * Walks an object's properties and returns the lines.
	 *
	 * A single function for object, component and variable: all three ask the
	 * same thing -- what here departs from the default -- and answering
	 * differently in each would be three formats for the reader to learn.
	 */
	void CollectEntries(const UObject* Target, const UObject* Archetype,
		const FString& NormalizedFilter, const TSet<FName>& Skip,
		const FString& Context, TArray<FEntry>& OutEntries, FCollectStats& Stats)
	{
		const bool bFiltering = !NormalizedFilter.IsEmpty();

		// `Body Instance, Body Instance` in a footer does not say whose they are.
		// Every capsule and every mesh has one, so without the component in front
		// the note only confuses.
		auto Qualify = [&Context](const FString& Name)
		{
			return Context.IsEmpty() ? Name : Context + TEXT(".") + Name;
		};

		for (TFieldIterator<FProperty> It(Target->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsVisible(Property) || Skip.Contains(Property->GetFName()))
			{
				continue;
			}

			++Stats.Considered;

			const FString Name = DisplayName(Property);

			if (bFiltering && !FNodeScribeCatalog::Normalize(Name).Contains(NormalizedFilter))
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
			const void* DefaultPtr = FindDefaultValuePtr(Property, Archetype);
			const bool bChanged = DiffersFromDefault(Property, ValuePtr, DefaultPtr);

			if (!bFiltering && !bChanged)
			{
				++Stats.AtDefault;
				continue;
			}

			FString Value;
			if (!ValueToText(Property, ValuePtr, Value))
			{
				// Do not invent a line that looks right and comes back different. It
				// leaves the sheet and shows up in the warning, by name, so its
				// absence is visible.
				Stats.Unreadable.Add(Qualify(Name));
				continue;
			}

			if (!bFiltering && Value.Len() > MaxSurveyValueLength)
			{
				// Too long flat: try opening it and showing only what changed.
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					const int32 Before = OutEntries.Num();
					const int32 TooLongBefore = Stats.TooLong.Num();
					const int32 UnreadableBefore = Stats.Unreadable.Num();

					FEntry Head;
					Head.Left = Name + TEXT(":");
					OutEntries.Add(Head);

					if (CollectStructMembers(StructProperty, ValuePtr, DefaultPtr, 1,
						Qualify(Name), OutEntries, Stats) > 0)
					{
						continue;
					}

					OutEntries.SetNum(Before);
					Stats.TooLong.SetNum(TooLongBefore);
					Stats.Unreadable.SetNum(UnreadableBefore);
				}

				// Named, not vanished: the reader learns that it changed and that
				// it can be asked for. Vanishing silently would be lying by omission.
				Stats.TooLong.Add(Qualify(Name));
				continue;
			}

			FEntry Entry;
			Entry.Left = bFiltering
				? FString::Printf(TEXT("%s : %s = %s"), *Name, *DescribeType(Property), *Value)
				: FString::Printf(TEXT("%s = %s"), *Name, *Value);

			// The factory value only goes in where there was a change. On the other
			// lines it would repeat what is already written.
			FString DefaultText;
			if (bChanged && DefaultPtr && ValueToText(Property, DefaultPtr, DefaultText)
				&& DefaultText.Len() <= MaxSurveyValueLength)
			{
				Entry.Default = DefaultText;
			}

			OutEntries.Add(MoveTemp(Entry));
		}
	}

	/** The names of the variables the Blueprint itself declares. */
	TSet<FName> CollectOwnVariableNames(const UBlueprint* Blueprint)
	{
		TSet<FName> Names;
		if (Blueprint)
		{
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				Names.Add(Variable.VarName);
			}
		}
		return Names;
	}

}

FString FNodeScribeObjectReader::ReadObject(UObject* Object, const FString& Filter)
{
	if (!Object)
	{
		return TEXT("[error]: no object given.");
	}

	// AI assets have a shape of their own: their information is not in the top
	// object's properties, but in the structure hanging from it. A generic sheet
	// of a blackboard says `Keys` and nothing more.
	//
	// It stays in the same tool on purpose: two similar doors make the caller
	// choose wrong and spend a turn finding out.
	if (FNodeScribeAIReader::Handles(Object))
	{
		return FNodeScribeAIReader::ReadAsset(Object);
	}

	UObject* Target = ResolveTarget(Object);
	if (!Target)
	{
		return FString::Printf(
			TEXT("[error]: `%s` has no compiled class -- compile the Blueprint first."),
			*Object->GetName());
	}

	UClass* Class = Target->GetClass();
	UObject* Archetype = Target->GetArchetype();

	const bool bFiltering = !Filter.IsEmpty();
	const FString NormalizedFilter = bFiltering
		? FNodeScribeCatalog::Normalize(Filter) : FString();

	UBlueprint* Blueprint = FindBlueprint(Object, Class);
	const TSet<FName> OwnVariables = CollectOwnVariableNames(Blueprint);

	FCollectStats Stats;

	// The Blueprint's own variables come out in a block of their own, with
	// `variable` and the type, so they stay out of the regular pass.
	TArray<FEntry> Entries;
	CollectEntries(Target, Archetype, NormalizedFilter, OwnVariables,
		FString(), Entries, Stats);

	TArray<FEntry> VariableEntries;
	for (const FBPVariableDescription& Variable : Blueprint
		? Blueprint->NewVariables : TArray<FBPVariableDescription>())
	{
		const FProperty* Property = Class->FindPropertyByName(Variable.VarName);
		if (!Property || !IsVisible(Property))
		{
			continue;
		}

		++Stats.Considered;

		const FString Name = DisplayName(Property);
		if (bFiltering && !FNodeScribeCatalog::Normalize(Name).Contains(NormalizedFilter))
		{
			continue;
		}

		// A variable declared here always shows up, even at its factory value: it
		// does not exist in the parent class, so its existence already is the
		// information.
		FString Value;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
		const bool bHasValue = ValueToText(Property, ValuePtr, Value);

		FEntry Entry;
		Entry.Left = FString::Printf(TEXT("variable %s : %s"), *Name, *DescribeType(Property));

		// Instance Editable is what makes the variable show up in the panel of
		// whoever uses the Blueprint -- it is how a BTTask gets a per-node
		// parameter. Without it coming out here, a round trip would silently
		// erase the flag.
		if (Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
		{
			Entry.Left += TEXT(" editable");
		}

		if (bHasValue && Value != TEXT("None") && Value != TEXT("0") && Value != TEXT("false"))
		{
			Entry.Left += TEXT(" = ") + Value;
		}

		VariableEntries.Add(MoveTemp(Entry));
	}

	TArray<FString> Lines;

	// The asset's name, and the class in parentheses -- `sheet BT_Golem
	// (BehaviorTree)`. In a Blueprint both are the same word, and repeating it
	// says nothing; there the parent class wins, which is the missing
	// information: `sheet BP_Golem (Character)`.
	const FString TargetName = DescribeTargetName(Object, Target);
	FString ClassName = CleanClassName(Class);
	if (ClassName == TargetName && Class->GetSuperClass())
	{
		ClassName = CleanClassName(Class->GetSuperClass());
	}

	Lines.Add(FString::Printf(TEXT("sheet %s (%s)"), *TargetName, *ClassName));

	const FString Ancestry = DescribeAncestry(Class);
	if (!Ancestry.IsEmpty())
	{
		Lines.Add(TEXT("# inherits: ") + Ancestry);
	}

	// The graphs, by the name `read_graph` finds them with.
	//
	// Without this line the graph's name is guesswork: `EventGraph` works almost
	// always and fails without saying why, and there was no way to find the
	// right one -- people kept trying names until they got it, or gave up on
	// the asset.
	if (Blueprint && !bFiltering)
	{
		TArray<FString> GraphNames;

		auto Collect = [&GraphNames](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (const UEdGraph* Graph : Graphs)
			{
				if (Graph)
				{
					GraphNames.Add(Graph->GetName());
				}
			}
		};

		Collect(Blueprint->UbergraphPages);
		Collect(Blueprint->FunctionGraphs);
		Collect(Blueprint->MacroGraphs);

		if (GraphNames.Num() > 0)
		{
			Lines.Add(TEXT("# graphs: ") + FString::Join(GraphNames, TEXT(", ")));
		}
	}

	// An AnimBlueprint's skeleton does not show up in the sheet by itself: the
	// sheet reads the class's CDO, and `TargetSkeleton` lives in the asset, one
	// level up. Without this line, `read_object` on an AnimBlueprint filtering by
	// "Skeleton" returns zero properties -- which reads as "it has none", not as
	// "it is in another object". It is the first thing you want to check when a
	// character shows up in the reference pose, because a swapped skeleton gives
	// exactly that.
	if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
	{
		const USkeleton* Skeleton = AnimBlueprint->TargetSkeleton;

		Lines.Add(FString::Printf(TEXT("# skeleton: %s"),
			Skeleton ? *Skeleton->GetPathName()
				: (AnimBlueprint->bIsTemplate ? TEXT("none (it is a template)") : TEXT("none"))));
	}

	const int32 HeaderIndex = 0;

	AppendAligned(Lines, VariableEntries);
	AppendAligned(Lines, Entries);

	int32 Shown = VariableEntries.Num() + Entries.Num();

	// Components are where half of what gets asked about an actor lives: `Max
	// Walk Speed` is not on the Character, it is on its CharacterMovement.
	for (const TPair<FString, UObject*>& Pair : CollectComponents(Target, Blueprint))
	{
		UObject* Component = Pair.Value;

		TArray<FEntry> ComponentEntries;
		CollectEntries(Component, Component->GetArchetype(),
			NormalizedFilter, TSet<FName>(), Pair.Key, ComponentEntries, Stats);

		if (ComponentEntries.Num() == 0)
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("%s : %s"),
			*Pair.Key, *CleanClassName(Component->GetClass())));

		TArray<FString> ComponentLines;
		AppendAligned(ComponentLines, ComponentEntries);
		for (const FString& Line : ComponentLines)
		{
			Lines.Add(TEXT("  ") + Line);
		}

		Shown += ComponentEntries.Num();
	}

	// The BlendSpace keeps what it is in two struct arrays the generic formatter
	// does not open -- and the sheet said `I do not know how to write the value
	// of: Sample Data`, which is the same as saying nothing. The lines come out
	// in the format `write_blendspace` accepts, so reading and writing speak the
	// same language.
	if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(Target))
	{
		const FString Samples = NodeScribeBlendSpace::Read(BlendSpace);
		if (!Samples.IsEmpty())
		{
			TArray<FString> SampleLines;
			Samples.ParseIntoArrayLines(SampleLines);
			Lines.Append(SampleLines);

			Shown += SampleLines.Num();

			// It already came out, and better: it should not come out again as a
			// pending item.
			Stats.Unreadable.Remove(TEXT("Sample Data"));
			Stats.Unreadable.Remove(TEXT("Blend Parameters"));
		}
	}

	if (bFiltering)
	{
		Lines[HeaderIndex] += FString::Printf(TEXT("  ~ \"%s\" (%d of %d)"),
			*Filter, Shown, Stats.Considered);
	}
	else
	{
		// The silence needs to be explicit: without this line, "did not show up"
		// is ambiguous between being at the default and the plugin not knowing
		// how to read it.
		Lines.Add(FString::Printf(TEXT("~ %d properties at default"), Stats.AtDefault));
	}

	if (Stats.TooLong.Num() > 0)
	{
		Lines.Add(FString::Printf(
			TEXT("# [note]: changed, but the value is too long for the overview -- ")
			TEXT("ask for it by name to see it: %s"),
			*FString::Join(Stats.TooLong, TEXT(", "))));
	}

	if (Stats.Unreadable.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("# [warning]: I do not know how to write the value of: %s"),
			*FString::Join(Stats.Unreadable, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}
