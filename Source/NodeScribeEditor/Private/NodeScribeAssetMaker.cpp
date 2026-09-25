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
	 * The properties the factory lets you configure.
	 *
	 * It serves to say, when the asset comes out lame, what was missing. A
	 * `UAnimBlueprintFactory` without `TargetSkeleton` creates an AnimBlueprint
	 * without a skeleton: the Engine does not complain, and the problem only
	 * shows up when someone opens the asset.
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

			// From UFactory up it is Engine plumbing, not configuration of this
			// asset.
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

	/**
	 * The factory that knows how to create this type.
	 *
	 * It sweeps instead of keeping a table: it is the same reason as the
	 * blackboard key types -- a plugin, or the project itself, may bring its
	 * own, and a fixed table would answer "cannot create" for something the
	 * Engine can.
	 */
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

	// Only inside the project's content. Writing into /Engine or /Script from a
	// line of text is the kind of accident that cannot be undone.
	if (!Trimmed.StartsWith(TEXT("/Game/")))
	{
		return FString::Printf(
			TEXT("[error]: `%s` is outside /Game/. I only create inside the project's content."),
			*Trimmed);
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Trimmed);
	const FString PackagePath = FPackageName::GetLongPackagePath(Trimmed);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		return FString::Printf(
			TEXT("[error]: `%s` is not a path with an asset name at the end."), *Trimmed);
	}

	// Never overwrites. An existing asset may have half the project hanging
	// from it, and replacing it from a line of text cannot be undone.
	//
	// Two checks because `DoesPackageExist` only looks at the disk, and an asset
	// created and not saved yet lives only in memory -- which is exactly the
	// state a freshly created one stays in. Calling twice in a row went straight
	// past the guard.
	if (FPackageName::DoesPackageExist(Trimmed) || FindPackage(nullptr, *Trimmed))
	{
		return FString::Printf(
			TEXT("[error]: something already exists at `%s`. I do not overwrite."), *Trimmed);
	}

	UClass* ParentClass = NodeScribeTypeNames::FindClassByFriendlyName(Parent);
	if (!ParentClass)
	{
		return FString::Printf(
			TEXT("[error]: could not find class `%s`. Use the name shown on screen, such as ")
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
		// A Blueprint child of the requested class. It is the case of
		// GameplayEffect, BTTask_BlueprintBase and BTService_BlueprintBase: on
		// screen they are "assets", but inside they are Blueprints with that parent.
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
				TEXT("[error]: `%s` is not Blueprintable and I found no factory that creates it."),
				*ParentClass->GetName());
		}

		AssetClass = ParentClass;
	}

	const TArray<FString> Configurable = EditableFactoryProperties(Factory);

	// The factory before creating. Afterwards it is no use: the properties it
	// carries -- the skeleton, above all -- become read-only on the finished
	// asset, and the Engine refuses to write into them.
	const FString TrimmedOptions = Options.TrimStartAndEnd();
	if (!TrimmedOptions.IsEmpty())
	{
		const FNodeScribeObjectWriter::FResult Written =
			FNodeScribeObjectWriter::WriteObject(Factory, TrimmedOptions);

		// Abort instead of creating anyway. An asset created with a half
		// configured factory is worse than no asset: it exists, looks ready, and
		// only shows itself when someone opens it.
		if (Written.Diagnostics.Num() > 0)
		{
			return FString::Printf(
				TEXT("[error]: created nothing -- the options of factory `%s` were not accepted:\n%s\n\nIt configures: %s"),
				*Factory->GetClass()->GetName(),
				*FString::Join(Written.Diagnostics, TEXT("\n")),
				Configurable.Num() > 0 ? *FString::Join(Configurable, TEXT(", ")) : TEXT("(nothing)"));
		}
	}

	Created = AssetTools.CreateAsset(AssetName, PackagePath, AssetClass, Factory);

	if (!Created)
	{
		return FString::Printf(TEXT("[error]: the Engine refused to create `%s`."), *Trimmed);
	}

	// Not saved. It stays dirty like any asset freshly created in the editor,
	// and `save_all_and_quit` writes it -- that way whoever created it by
	// mistake closes without saving.
	FString Message = FString::Printf(TEXT("Created: %s"), *Created->GetPathName());

	// A note, not an error: some factories have all-optional configuration, and
	// blocking there would get in the way of the common case. But staying quiet
	// when there was something to say is how the AnimBlueprint without a
	// skeleton is born.
	if (TrimmedOptions.IsEmpty() && Configurable.Num() > 0)
	{
		Message += FString::Printf(
			TEXT("\n[note]: factory `%s` configures %s, and nothing was passed in options. ")
			TEXT("If the asset depends on any of those, it was born without."),
			*Factory->GetClass()->GetName(), *FString::Join(Configurable, TEXT(", ")));
	}

	return Message;
}
