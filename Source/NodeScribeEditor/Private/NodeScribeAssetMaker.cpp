#include "NodeScribeAssetMaker.h"

#include "NodeScribeBuilder.h"
#include "NodeScribeObjectWriter.h"

#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * A factory que sabe criar este tipo.
	 *
	 * Varre em vez de manter tabela: e' o mesmo motivo dos tipos de chave de
	 * blackboard -- um plugin, ou o proprio projeto, pode trazer a sua, e uma
	 * tabela fixa responderia "nao sei criar" para algo que a Engine sabe.
	 */
	/**
	 * As propriedades que a factory deixa configurar.
	 *
	 * Serve para dizer, quando o asset sai capenga, o que faltava passar. Uma
	 * `UAnimBlueprintFactory` sem `TargetSkeleton` cria um AnimBlueprint sem
	 * esqueleto: a Engine nao reclama, e o problema so' aparece quando alguem
	 * abre o asset.
	 */
	TArray<FString> EditableFactoryProperties(const UFactory* Factory)
	{
		TArray<FString> Names;
		if (!Factory)
		{
			return Names;
		}

		for (TFieldIterator<FProperty> It(Factory->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;

			// Da UFactory para cima e' encanamento da Engine, nao configuracao
			// deste asset.
			if (Property->GetOwnerClass() == UFactory::StaticClass())
			{
				continue;
			}

			if (Property->HasAnyPropertyFlags(CPF_Edit))
			{
				Names.Add(Property->GetName());
			}
		}

		return Names;
	}

	UFactory* FindFactoryFor(UClass* AssetClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate->IsChildOf(UFactory::StaticClass())
				|| Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				continue;
			}

			UFactory* Factory = Candidate->GetDefaultObject<UFactory>();
			if (Factory && Factory->CanCreateNew() && Factory->GetSupportedClass() == AssetClass)
			{
				return NewObject<UFactory>(GetTransientPackage(), Candidate);
			}
		}

		return nullptr;
	}
}

FString FNodeScribeAssetMaker::CreateAsset(const FString& Path, const FString& Parent, const FString& Options)
{
	const FString Trimmed = Path.TrimStartAndEnd();

	// So' dentro do conteudo do projeto. Escrever em /Engine ou /Script a partir
	// de uma linha de texto e' o tipo de acidente que nao se desfaz.
	if (!Trimmed.StartsWith(TEXT("/Game/")))
	{
		return FString::Printf(
			TEXT("[erro]: `%s` esta' fora de /Game/. So' crio dentro do conteudo do projeto."),
			*Trimmed);
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Trimmed);
	const FString PackagePath = FPackageName::GetLongPackagePath(Trimmed);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		return FString::Printf(
			TEXT("[erro]: `%s` nao e' um caminho com nome de asset no fim."), *Trimmed);
	}

	// Nao sobrescreve. Um asset existente pode ter meio projeto pendurado nele,
	// e substituir a partir de uma linha de texto nao se desfaz.
	//
	// Duas checagens porque `DoesPackageExist` so' olha o disco, e asset criado
	// e ainda nao salvo mora so' na memoria -- que e' exatamente o estado em que
	// um recem-criado fica. Chamar duas vezes seguidas passava direto pela
	// guarda.
	if (FPackageName::DoesPackageExist(Trimmed) || FindPackage(nullptr, *Trimmed))
	{
		return FString::Printf(
			TEXT("[erro]: ja' existe algo em `%s`. Nao sobrescrevo."), *Trimmed);
	}

	UClass* ParentClass = NodeScribeTypeNames::FindClassByFriendlyName(Parent);
	if (!ParentClass)
	{
		return FString::Printf(
			TEXT("[erro]: nao achei a classe `%s`. Use o nome que aparece na tela, como ")
			TEXT("BTTask_BlueprintBase, GameplayEffect, BlackboardData, BehaviorTree."),
			*Parent);
	}

	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	UObject* Created = nullptr;
	UClass* AssetClass = nullptr;
	UFactory* Factory = nullptr;

	if (FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		// Blueprint filho da classe pedida. E' o caso de GameplayEffect,
		// BTTask_BlueprintBase e BTService_BlueprintBase: na tela sao "assets",
		// mas por dentro sao Blueprint com aquele pai.
		UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
		BlueprintFactory->ParentClass = ParentClass;

		Factory = BlueprintFactory;
		AssetClass = UBlueprint::StaticClass();
	}
	else
	{
		Factory = FindFactoryFor(ParentClass);
		if (!Factory)
		{
			return FString::Printf(
				TEXT("[erro]: `%s` nao e' Blueprintable e nao achei factory que a crie."),
				*ParentClass->GetName());
		}

		AssetClass = ParentClass;
	}

	const TArray<FString> Configurable = EditableFactoryProperties(Factory);

	// A factory antes de criar. Depois nao adianta: as propriedades que ela
	// carrega -- o esqueleto, sobretudo -- viram somente-leitura no asset
	// pronto, e a Engine recusa escrever nelas.
	const FString TrimmedOptions = Options.TrimStartAndEnd();
	if (!TrimmedOptions.IsEmpty())
	{
		const FNodeScribeObjectWriter::FResult Written =
			FNodeScribeObjectWriter::WriteObject(Factory, TrimmedOptions);

		// Aborta em vez de criar assim mesmo. Um asset criado com a factory meio
		// configurada e' pior que asset nenhum: ele existe, parece pronto, e so'
		// da' as caras quando alguem abre.
		if (Written.Diagnostics.Num() > 0)
		{
			return FString::Printf(
				TEXT("[erro]: nao criei nada -- as opcoes da factory `%s` nao foram aceitas:\n%s\n\nEla configura: %s"),
				*Factory->GetClass()->GetName(),
				*FString::Join(Written.Diagnostics, TEXT("\n")),
				Configurable.Num() > 0 ? *FString::Join(Configurable, TEXT(", ")) : TEXT("(nada)"));
		}
	}

	Created = AssetTools.CreateAsset(AssetName, PackagePath, AssetClass, Factory);

	if (!Created)
	{
		return FString::Printf(TEXT("[erro]: a Engine recusou criar `%s`."), *Trimmed);
	}

	// Nao salva. Fica sujo como qualquer asset recem-criado no editor, e o
	// `save_all_and_quit` grava -- assim quem criou por engano fecha sem salvar.
	FString Message = FString::Printf(TEXT("Criado: %s"), *Created->GetPathName());

	// Nota, e nao erro: ha' factory cuja configuracao e' toda opcional, e
	// bloquear ali atrapalharia o caso comum. Mas passar em branco quando havia
	// o que dizer e' como o AnimBlueprint sem esqueleto nasce.
	if (TrimmedOptions.IsEmpty() && Configurable.Num() > 0)
	{
		Message += FString::Printf(
			TEXT("\n[nota]: a factory `%s` configura %s, e nada foi passado em options. ")
			TEXT("Se o asset depende de alguma dessas, ele nasceu sem."),
			*Factory->GetClass()->GetName(), *FString::Join(Configurable, TEXT(", ")));
	}

	return Message;
}
