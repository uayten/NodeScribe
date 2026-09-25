#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;
class UClass;
class UFunction;

/** Result of a lookup by node name in the catalog. */
struct FNodeScribeLookup
{
	/** The chosen function, if there was a confident choice. */
	UFunction* Function = nullptr;

	/**
	 * Candidates when the lookup was ambiguous. In that case Function is null
	 * on purpose: guessing between two similar functions is the most expensive
	 * mistake this plugin can make, because it compiles and runs wrong.
	 */
	TArray<FString> Candidates;

	bool IsConfident() const { return Function != nullptr; }
	bool IsAmbiguous() const { return Function == nullptr && Candidates.Num() > 0; }
};

/**
 * Index of every Blueprint-callable UFunction, built once and kept in memory.
 * It is what allows writing `Print String` instead of
 * `/Script/Engine.KismetSystemLibrary:PrintString`.
 */
class FNodeScribeCatalog
{
public:
	static FNodeScribeCatalog& Get();

	/**
	 * Looks up a function by the name the user wrote.
	 * @param SelfClass  class of the target Blueprint, to find its own functions.
	 * @param ContextClass  when the target is known (e.g. `$pc.` was a PlayerController),
	 *                      functions of that class get priority.
	 */
	FNodeScribeLookup FindFunction(const FString& Query, UClass* SelfClass, UClass* ContextClass) const;

	/** Forces the index to be rebuilt (useful after compiling new Blueprints). */
	void Invalidate();

	/**
	 * Normalises for comparison: lowercase, letters and digits only, no accents.
	 * `Jump Duration`, `jump_duration` and `JUMPDURATION` give the same key.
	 */
	static FString Normalize(const FString& In);

	/** `ã` -> `a`. One character; the caller is Normalize. */
	static TCHAR FoldAccent(TCHAR C);

	/**
	 * true when the function does not become a call, but an async action node
	 * (`UK2Node_AsyncAction`).
	 *
	 * It is the same rule the Engine uses to build the menu: a static function
	 * whose return is a `UBlueprintAsyncActionBase`. Those functions are marked
	 * `BlueprintInternalUseOnly` on purpose -- nobody calls them directly --, and
	 * that is why the catalog needs to make an exception for them instead of
	 * discarding them.
	 */
	static bool IsAsyncActionFactory(const UFunction* Function);

	/**
	 * The event's name as the user writes it: `ReceiveBeginPlay` -> `BeginPlay`.
	 * The Engine prefixes implementable events; nobody types the prefix.
	 */
	static FString StripEventPrefix(const FString& FunctionName);

private:
	struct FEntry
	{
		TWeakObjectPtr<UFunction> Function;
		TWeakObjectPtr<UClass> OwnerClass;

		/** Name without the Engine prefix, e.g. "setactorlocation". */
		FString NormalizedName;

		/**
		 * Name exactly as it is in the code, with the prefix:
		 * "k2_setactorlocation", "bp_applygameplayeffecttotarget".
		 *
		 * The reader writes the raw name when qualifying a function, so without
		 * this form the text it produces did not come back.
		 */
		FString NormalizedRawName;

		/** Normalised display name, e.g. "setactorlocation". */
		FString NormalizedDisplay;

		/** true for static libraries (KismetSystemLibrary and the like). */
		bool bIsLibrary = false;
	};

	void EnsureBuilt() const;
	void Build() const;

	/** A candidate's score. Higher is better; <= 0 discards. */
	static int32 ScoreEntry(const FEntry& Entry, const FString& NormalizedQuery, UClass* SelfClass, UClass* ContextClass);

	mutable TArray<FEntry> Entries;
	mutable bool bBuilt = false;
};
