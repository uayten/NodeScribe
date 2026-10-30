#include "NodeScribeCatalog.h"

#include "NodeScribeTypes.h"
#include "Engine/Blueprint.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Prefixes the Engine uses by convention and nobody writes in a chat. */
	const TCHAR* const IgnoredPrefixes[] = { TEXT("K2_"), TEXT("BP_"), TEXT("Receive") };

	FString StripEnginePrefix(const FString& In)
	{
		for (const TCHAR* Prefix : IgnoredPrefixes)
		{
			if (In.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return In.RightChop(FCString::Strlen(Prefix));
			}
		}
		return In;
	}

	bool IsUsableFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		const bool bCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure);
		if (!bCallable)
		{
			return false;
		}

		if (Function->HasAnyFunctionFlags(FUNC_Delegate))
		{
			return false;
		}

		// `BlueprintInternalUseOnly` marks what is not called directly -- and an
		// async action's factory is exactly that: what exposes it in the graph is
		// `UK2Node_AsyncAction`, not a call node. Discarding all of them left a
		// whole plugin built on `UBlueprintAsyncActionBase` out of the catalog,
		// and none of its graphs came back pasteable.
		if (Function->HasMetaData(TEXT("BlueprintInternalUseOnly"))
			&& !FNodeScribeCatalog::IsAsyncActionFactory(Function))
		{
			return false;
		}

		if (Function->HasMetaData(TEXT("DeprecatedFunction")))
		{
			return false;
		}

		return true;
	}

	bool IsUsableClass(const UClass* Class)
	{
		if (!Class)
		{
			return false;
		}

		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}

		// SKEL_/REINST_ classes are Blueprint compilation artifacts.
		const FString Name = Class->GetName();
		if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")))
		{
			return false;
		}

		return true;
	}
}

FNodeScribeCatalog& FNodeScribeCatalog::Get()
{
	static FNodeScribeCatalog Instance;
	return Instance;
}

FString FNodeScribeCatalog::Normalize(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());

	for (const TCHAR C : In)
	{
		if (FChar::IsAlnum(C))
		{
			Out.AppendChar(FoldAccent(FChar::ToLower(C)));
		}
	}

	return Out;
}

bool FNodeScribeCatalog::IsNearMiss(const FString& A, const FString& B)
{
	// Damerau-Levenshtein (optimal string alignment): a swapped pair of letters
	// is one typo, as a person counts it -- `txet` is one slip, not two.
	const int32 Allowed = FMath::Max(1, FMath::Min(A.Len(), B.Len()) / 4);
	if (A.IsEmpty() || B.IsEmpty() || FMath::Abs(A.Len() - B.Len()) > Allowed)
	{
		return false;
	}

	const int32 Width = B.Len() + 1;
	TArray<int32> D;
	D.SetNumZeroed((A.Len() + 1) * Width);

	for (int32 I = 0; I <= A.Len(); ++I)
	{
		D[I * Width] = I;
	}
	for (int32 J = 0; J <= B.Len(); ++J)
	{
		D[J] = J;
	}

	for (int32 I = 1; I <= A.Len(); ++I)
	{
		for (int32 J = 1; J <= B.Len(); ++J)
		{
			const int32 Cost = A[I - 1] == B[J - 1] ? 0 : 1;
			int32 Best = FMath::Min3(D[(I - 1) * Width + J] + 1, D[I * Width + J - 1] + 1,
				D[(I - 1) * Width + J - 1] + Cost);

			if (I > 1 && J > 1 && A[I - 1] == B[J - 2] && A[I - 2] == B[J - 1])
			{
				Best = FMath::Min(Best, D[(I - 2) * Width + J - 2] + 1);
			}
			D[I * Width + J] = Best;
		}
	}

	return D[A.Len() * Width + B.Len()] <= Allowed;
}

TCHAR FNodeScribeCatalog::FoldAccent(TCHAR C)
{
	// Names are written by people, and people write accents: a variable called
	// `Duração` is found by `$Duracao`, and the other way round. The keywords of
	// the format are plain English and need none of this -- it is for the names
	// that come from the project: variables, pins, assets, events.
	//
	// It is the same kind of tolerance that already exists for case and
	// spacing. The theoretical risk -- two variables that only differ by an
	// accent becoming the same key -- is the same one already taken with
	// `MyVar` and `my var`.
	//
	// The codes go in as numbers, not literals: the file is pure ASCII like the
	// rest of the plugin, and an accented literal depends on the compiler
	// guessing the file's encoding.
	switch (static_cast<uint32>(C))
	{
	case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: // accented a
		return TEXT('a');
	case 0xE7:                                                       // c cedilla
		return TEXT('c');
	case 0xE8: case 0xE9: case 0xEA: case 0xEB:                      // accented e
		return TEXT('e');
	case 0xEC: case 0xED: case 0xEE: case 0xEF:                      // accented i
		return TEXT('i');
	case 0xF1:                                                       // n with tilde
		return TEXT('n');
	case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6:           // accented o
		return TEXT('o');
	case 0xF9: case 0xFA: case 0xFB: case 0xFC:                      // accented u
		return TEXT('u');
	case 0xFD: case 0xFF:                                            // accented y
		return TEXT('y');
	default:
		return C;
	}
}

bool FNodeScribeCatalog::IsAsyncActionFactory(const UFunction* Function)
{
	if (!Function || !Function->HasAnyFunctionFlags(FUNC_Static))
	{
		return false;
	}

	const FObjectProperty* ReturnProperty = CastField<FObjectProperty>(Function->GetReturnProperty());
	if (!ReturnProperty || !ReturnProperty->PropertyClass)
	{
		return false;
	}

	if (!ReturnProperty->PropertyClass->IsChildOf(UBlueprintAsyncActionBase::StaticClass()))
	{
		return false;
	}

	// A class that asks for a node of its own (the GAS tasks, for example) stays
	// out: there the right node is not `UK2Node_AsyncAction`, and creating that
	// one would give a similar, wrong node. It is the same check
	// `UK2Node_AsyncAction` makes when offering itself in the menu.
	const UClass* Owner = Function->GetOwnerClass();
	if (Owner && Owner->HasMetaData(TEXT("HasDedicatedAsyncNode")))
	{
		return false;
	}

	return true;
}

FString FNodeScribeCatalog::StripEventPrefix(const FString& FunctionName)
{
	if (FunctionName.StartsWith(TEXT("Receive"), ESearchCase::CaseSensitive))
	{
		return FunctionName.RightChop(7);
	}

	if (FunctionName.StartsWith(TEXT("K2_"), ESearchCase::CaseSensitive))
	{
		return FunctionName.RightChop(3);
	}

	return FunctionName;
}

void FNodeScribeCatalog::Invalidate()
{
	Entries.Reset();
	bBuilt = false;
}

void FNodeScribeCatalog::EnsureBuilt() const
{
	if (!bBuilt)
	{
		Build();
	}
}

void FNodeScribeCatalog::Build() const
{
	const double StartTime = FPlatformTime::Seconds();

	Entries.Reset();
	Entries.Reserve(32768);

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (!IsUsableClass(Class))
		{
			continue;
		}

		const bool bIsLibrary = Class->IsChildOf(UBlueprintFunctionLibrary::StaticClass());

		// ExcludeSuper: each function is indexed once, in the class that declares it.
		for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!IsUsableFunction(Function))
			{
				continue;
			}

			FEntry Entry;
			Entry.Function = Function;
			Entry.OwnerClass = Class;
			Entry.bIsLibrary = bIsLibrary;

			const FString RawName = Function->GetName();
			Entry.NormalizedName = Normalize(StripEnginePrefix(RawName));
			Entry.NormalizedRawName = Normalize(RawName);

			FString DisplayName = Function->HasMetaData(TEXT("DisplayName"))
				? Function->GetMetaData(TEXT("DisplayName"))
				: FName::NameToDisplayString(RawName, false);

			Entry.NormalizedDisplay = Normalize(DisplayName);

			Entries.Add(MoveTemp(Entry));
		}
	}

	bBuilt = true;

	UE_LOG(LogNodeScribe, Log, TEXT("Catalog built: %d functions in %.2fs"),
		Entries.Num(), FPlatformTime::Seconds() - StartTime);
}

int32 FNodeScribeCatalog::ScoreEntry(const FEntry& Entry, const FString& NormalizedQuery, UClass* SelfClass, UClass* ContextClass)
{
	if (NormalizedQuery.IsEmpty())
	{
		return 0;
	}

	int32 Score = 0;

	if (Entry.NormalizedDisplay == NormalizedQuery)
	{
		Score = 1000;
	}
	else if (Entry.NormalizedName == NormalizedQuery || Entry.NormalizedRawName == NormalizedQuery)
	{
		Score = 900;
	}
	else if (Entry.NormalizedDisplay.StartsWith(NormalizedQuery) || Entry.NormalizedName.StartsWith(NormalizedQuery))
	{
		Score = 400;
	}
	else if (Entry.NormalizedDisplay.Contains(NormalizedQuery) || Entry.NormalizedName.Contains(NormalizedQuery))
	{
		Score = 200;
	}
	else
	{
		return 0;
	}

	UClass* Owner = Entry.OwnerClass.Get();
	if (!Owner)
	{
		return 0;
	}

	// The class the user is editing beats anything from the Engine: if they
	// wrote the name of one of their own functions, that is the one they want.
	//
	// Unless the line names a Target that is not one of those: the function
	// runs on the Target then, and a Widget Blueprint's own `SetVisibility`
	// would win over the SceneComponent the line is wired to.
	if (SelfClass && SelfClass->IsChildOf(Owner)
		&& (!ContextClass || ContextClass->IsChildOf(Owner)))
	{
		Score += 300;
	}

	if (ContextClass && ContextClass->IsChildOf(Owner))
	{
		Score += 250;
	}

	// Static libraries are the destination of most "loose" nodes
	// (Print String, Get Player Controller, math...).
	if (Entry.bIsLibrary)
	{
		Score += 60;
	}

	// Short names match better: between "Get Player Controller" and
	// "Get Player Controller From Platform User Id", the first is the expected one.
	const int32 LengthPenalty = FMath::Min(Entry.NormalizedDisplay.Len(), 200) / 4;
	Score -= LengthPenalty;

	return Score;
}

namespace
{
	/**
	 * A function or Custom Event declared in the Blueprint itself.
	 *
	 * The catalog is built once per session and does not know what was born
	 * afterwards -- and creating a Custom Event and using it on the next line is
	 * the normal flow of someone building a graph. Looking at the class itself
	 * costs almost nothing and covers exactly that case.
	 */
	UFunction* FindOwnFunction(UClass* SelfClass, const FString& NormalizedQuery)
	{
		if (!SelfClass || NormalizedQuery.IsEmpty())
		{
			return nullptr;
		}

		for (TFieldIterator<UFunction> FuncIt(SelfClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			const FString RawName = Function->GetName();

			if (FNodeScribeCatalog::Normalize(RawName) == NormalizedQuery
				|| FNodeScribeCatalog::Normalize(FName::NameToDisplayString(RawName, false)) == NormalizedQuery)
			{
				return Function;
			}
		}

		return nullptr;
	}
}

namespace
{
	/**
	 * true when `Function` is a class's implementation of a function of an
	 * interface it implements. `BossRushPlayer_C.Obter Facção` is that: the
	 * same call as `BPI_Facção.Obter Facção`, which reaches every implementer.
	 */
	bool IsInterfaceImplementationInCatalog(const UClass* Owner, const UFunction* Function)
	{
		if (!Owner || !Function || Owner->HasAnyClassFlags(CLASS_Interface))
		{
			return false;
		}

		for (const UClass* Class = Owner; Class; Class = Class->GetSuperClass())
		{
			for (const FImplementedInterface& Implemented : Class->Interfaces)
			{
				if (Implemented.Class && Implemented.Class->FindFunctionByName(Function->GetFName()))
				{
					return true;
				}
			}
		}

		return false;
	}
}

FNodeScribeLookup FNodeScribeCatalog::FindFunction(const FString& Query, UClass* SelfClass, UClass* ContextClass) const
{
	EnsureBuilt();

	FNodeScribeLookup Result;

	// `Class.Function` restricts the lookup to one class. The separator cannot
	// be parentheses: those already are the argument list, and the old form
	// suggested in the candidates (`ApplySettings (GameUserSettings)`) never worked.
	FString OwnerQuery;
	FString NameQuery = Query;
	{
		FString OwnerPart;
		FString NamePart;
		if (Query.Split(TEXT("."), &OwnerPart, &NamePart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			// `BP_Golem.Jump` and `BP_Golem_C.Jump` are the same class: nobody
			// types the generated suffix, the same rule as everywhere else.
			OwnerPart.RemoveFromEnd(TEXT("_C"));
			OwnerQuery = Normalize(OwnerPart);
			NameQuery = NamePart;
		}
	}

	const FString NormalizedQuery = Normalize(NameQuery);
	if (NormalizedQuery.IsEmpty())
	{
		return Result;
	}

	// No Target is a call on self, and on self the Blueprint's own function is
	// the only one that compiles. Before, another Blueprint's function with the
	// same name won the score: `0 Corações` became Vida's, and the graph failed
	// with "this blueprint (self) is not a Vida_C".
	if (OwnerQuery.IsEmpty() && !ContextClass)
	{
		if (UFunction* Own = FindOwnFunction(SelfClass, NormalizedQuery))
		{
			Result.Function = Own;
			return Result;
		}
	}

	struct FScored
	{
		const FEntry* Entry;
		int32 Score;
	};

	TArray<FScored> Scored;

	for (const FEntry& Entry : Entries)
	{
		if (!Entry.Function.IsValid())
		{
			continue;
		}

		if (!OwnerQuery.IsEmpty())
		{
			const UClass* Owner = Entry.OwnerClass.Get();
			if (!Owner)
			{
				continue;
			}

			FString OwnerName = Owner->GetName();
			OwnerName.RemoveFromEnd(TEXT("_C"));
			if (Normalize(OwnerName) != OwnerQuery)
			{
				continue;
			}
		}

		// An implementation is not a second candidate: it tied with its own
		// interface, and the call became ambiguous in every Blueprint that used
		// it. Named on purpose (`BossRushPlayer.Obter Facção`), it still resolves.
		if (OwnerQuery.IsEmpty() && IsInterfaceImplementationInCatalog(Entry.OwnerClass.Get(), Entry.Function.Get()))
		{
			continue;
		}

		const int32 Score = ScoreEntry(Entry, NormalizedQuery, SelfClass, ContextClass);
		if (Score > 0)
		{
			Scored.Add({ &Entry, Score });
		}
	}

	if (Scored.Num() == 0)
	{
		Result.Function = FindOwnFunction(SelfClass, NormalizedQuery);
		return Result;
	}

	Scored.Sort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });

	const int32 BestScore = Scored[0].Score;

	// A tie at the top means the text was not enough to decide. We prefer to
	// return the list and let the user choose.
	int32 TiedCount = 0;
	for (const FScored& Candidate : Scored)
	{
		if (Candidate.Score == BestScore)
		{
			++TiedCount;
		}
		else
		{
			break;
		}
	}

	const bool bConfident = (TiedCount == 1) && (BestScore >= 400);

	if (bConfident)
	{
		Result.Function = Scored[0].Entry->Function.Get();
		return Result;
	}

	// A tie between Engine functions is not ambiguity when the Blueprint itself
	// has one with that name: there the intent is clear.
	if (UFunction* Own = FindOwnFunction(SelfClass, NormalizedQuery))
	{
		Result.Function = Own;
		return Result;
	}

	const int32 CandidateCount = FMath::Min(Scored.Num(), 6);
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const FEntry* Entry = Scored[Index].Entry;
		UClass* Owner = Entry->OwnerClass.Get();
		UFunction* Function = Entry->Function.Get();

		if (Owner && Function)
		{
			// A copyable form: it is exactly what the user can type back.
			Result.Candidates.Add(FString::Printf(TEXT("%s.%s"), *Owner->GetName(), *Function->GetName()));
		}
	}

	return Result;
}
