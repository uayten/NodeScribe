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

	/**
	 * Normaliza para comparacao: minusculas, so letras e numeros, sem acento.
	 * `Duração do Pulo`, `duracao_do_pulo` e `DURACAODOPULO` dao a mesma chave.
	 */
	static FString Normalize(const FString& In);

	/** `ã` -> `a`. Um caractere; quem chama e' a Normalize. */
	static TCHAR FoldAccent(TCHAR C);

	/**
	 * true quando a funcao nao vira uma chamada, e sim um node de acao
	 * assincrona (`UK2Node_AsyncAction`).
	 *
	 * E' a mesma regra que a Engine usa para montar o menu: funcao estatica cujo
	 * retorno e' um `UBlueprintAsyncActionBase`. Essas funcoes sao marcadas
	 * `BlueprintInternalUseOnly` de proposito -- ninguem as chama direto --, e por
	 * isso o catalogo precisa abrir uma excecao para elas em vez de descartar.
	 */
	static bool IsAsyncActionFactory(const UFunction* Function);

	/**
	 * Nome do evento como o usuario escreve: `ReceiveBeginPlay` -> `BeginPlay`.
	 * A Engine prefixa os eventos implementaveis; ninguem digita o prefixo.
	 */
	static FString StripEventPrefix(const FString& FunctionName);

private:
	struct FEntry
	{
		TWeakObjectPtr<UFunction> Function;
		TWeakObjectPtr<UClass> OwnerClass;

		/** Nome sem o prefixo da Engine, ex.: "setactorlocation". */
		FString NormalizedName;

		/**
		 * Nome exatamente como esta' no codigo, com prefixo:
		 * "k2_setactorlocation", "bp_applygameplayeffecttotarget".
		 *
		 * O leitor escreve o nome cru ao qualificar uma funcao, entao sem esta
		 * forma o texto que ele produz nao voltava.
		 */
		FString NormalizedRawName;

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
