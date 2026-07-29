#include "NodeScribeCatalog.h"

#include "NodeScribeTypes.h"
#include "Engine/Blueprint.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** Prefixos que a Engine usa por convencao e que ninguem escreve num chat. */
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

		if (Function->HasMetaData(TEXT("BlueprintInternalUseOnly")))
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

		// Classes SKEL_/REINST_ sao artefatos de compilacao de Blueprint.
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
			Out.AppendChar(FChar::ToLower(C));
		}
	}

	return Out;
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

		// ExcludeSuper: cada funcao e' indexada uma vez, na classe que a declara.
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

			FString DisplayName = Function->HasMetaData(TEXT("DisplayName"))
				? Function->GetMetaData(TEXT("DisplayName"))
				: FName::NameToDisplayString(RawName, false);

			Entry.NormalizedDisplay = Normalize(DisplayName);

			Entries.Add(MoveTemp(Entry));
		}
	}

	bBuilt = true;

	UE_LOG(LogNodeScribe, Log, TEXT("Catalogo montado: %d funcoes em %.2fs"),
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
	else if (Entry.NormalizedName == NormalizedQuery)
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

	// A classe que o usuario esta editando ganha de qualquer coisa da Engine:
	// se ele escreveu o nome de uma funcao propria, e' essa que ele quer.
	if (SelfClass && SelfClass->IsChildOf(Owner))
	{
		Score += 300;
	}

	if (ContextClass && ContextClass->IsChildOf(Owner))
	{
		Score += 250;
	}

	// Bibliotecas estaticas sao o destino da maioria dos nodes "soltos"
	// (Print String, Get Player Controller, matematica...).
	if (Entry.bIsLibrary)
	{
		Score += 60;
	}

	// Nomes curtos casam melhor: entre "Get Player Controller" e
	// "Get Player Controller From Platform User Id", o primeiro e' o esperado.
	const int32 LengthPenalty = FMath::Min(Entry.NormalizedDisplay.Len(), 200) / 4;
	Score -= LengthPenalty;

	return Score;
}

FNodeScribeLookup FNodeScribeCatalog::FindFunction(const FString& Query, UClass* SelfClass, UClass* ContextClass) const
{
	EnsureBuilt();

	FNodeScribeLookup Result;

	const FString NormalizedQuery = Normalize(Query);
	if (NormalizedQuery.IsEmpty())
	{
		return Result;
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

		const int32 Score = ScoreEntry(Entry, NormalizedQuery, SelfClass, ContextClass);
		if (Score > 0)
		{
			Scored.Add({ &Entry, Score });
		}
	}

	if (Scored.Num() == 0)
	{
		return Result;
	}

	Scored.Sort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });

	const int32 BestScore = Scored[0].Score;

	// Um empate no topo significa que o texto nao foi suficiente para decidir.
	// Preferimos devolver a lista e deixar o usuario escolher.
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

	const int32 CandidateCount = FMath::Min(Scored.Num(), 6);
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const FEntry* Entry = Scored[Index].Entry;
		UClass* Owner = Entry->OwnerClass.Get();
		UFunction* Function = Entry->Function.Get();

		if (Owner && Function)
		{
			Result.Candidates.Add(FString::Printf(TEXT("%s (%s)"), *Function->GetName(), *Owner->GetName()));
		}
	}

	return Result;
}
