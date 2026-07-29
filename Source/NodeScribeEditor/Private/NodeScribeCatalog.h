#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;
class UClass;
class UFunction;

/** Resultado de uma busca por nome de node no catalogo. */
struct FNodeScribeLookup
{
	/** A funcao escolhida, se houve escolha confiante. */
	UFunction* Function = nullptr;

	/**
	 * Candidatos quando a busca ficou ambigua. Nesse caso Function e' nulo de
	 * proposito: chutar entre duas funcoes parecidas e' o erro mais caro que
	 * este plugin pode cometer, porque compila e roda errado.
	 */
	TArray<FString> Candidates;

	bool IsConfident() const { return Function != nullptr; }
	bool IsAmbiguous() const { return Function == nullptr && Candidates.Num() > 0; }
};

/**
 * Indice de todas as UFunctions chamaveis por Blueprint, montado uma vez e
 * guardado em memoria. E' o que permite escrever `Print String` em vez de
 * `/Script/Engine.KismetSystemLibrary:PrintString`.
 */
class FNodeScribeCatalog
{
public:
	static FNodeScribeCatalog& Get();

	/**
	 * Procura uma funcao pelo nome escrito pelo usuario.
	 * @param SelfClass  classe do Blueprint de destino, para achar funcoes proprias dele.
	 * @param ContextClass  quando o alvo e' conhecido (ex.: `$pc.` era um PlayerController),
	 *                      funcoes dessa classe ganham prioridade.
	 */
	FNodeScribeLookup FindFunction(const FString& Query, UClass* SelfClass, UClass* ContextClass) const;

	/** Forca a reconstrucao do indice (util depois de compilar Blueprints novos). */
	void Invalidate();

	/** Normaliza para comparacao: minusculas, so letras e numeros. */
	static FString Normalize(const FString& In);

private:
	struct FEntry
	{
		TWeakObjectPtr<UFunction> Function;
		TWeakObjectPtr<UClass> OwnerClass;

		/** Nome cru normalizado, ex.: "k2_setactorlocation". */
		FString NormalizedName;

		/** Nome de exibicao normalizado, ex.: "setactorlocation". */
		FString NormalizedDisplay;

		/** true para bibliotecas estaticas (KismetSystemLibrary e afins). */
		bool bIsLibrary = false;
	};

	void EnsureBuilt() const;
	void Build() const;

	/** Pontuacao de um candidato. Maior e' melhor; <= 0 descarta. */
	static int32 ScoreEntry(const FEntry& Entry, const FString& NormalizedQuery, UClass* SelfClass, UClass* ContextClass);

	mutable TArray<FEntry> Entries;
	mutable bool bBuilt = false;
};
