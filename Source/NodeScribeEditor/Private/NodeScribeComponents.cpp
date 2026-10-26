#include "NodeScribeComponents.h"

#include "NodeScribeCatalog.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SubobjectData.h"
#include "SubobjectDataHandle.h"
#include "SubobjectDataSubsystem.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * The name the sheet writes a component with.
	 *
	 * The same one `CollectComponents` keys it by: an SCS template's name
	 * without the `_GEN_VARIABLE` suffix -- which is the variable name -- and a
	 * native component's object name (`CollisionCylinder`, not the
	 * `CapsuleComponent` property that points at it).
	 */
	FString SheetComponentName(const UActorComponent* Component)
	{
		FString Name = Component->GetName();
		Name.RemoveFromEnd(USimpleConstructionScript::ComponentTemplateNameSuffix);
		return Name;
	}

	/** A component class as the sheet prints it: `BoxComponent`, `BPC_Health`. */
	FString ComponentClassName(const UClass* Class)
	{
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}

	struct FComponentHandles
	{
		FSubobjectDataHandle Actor;
		TArray<FSubobjectDataHandle> Components;
	};

	FComponentHandles GatherComponentHandles(UBlueprint* Blueprint)
	{
		FComponentHandles Result;
		USubobjectDataSubsystem* Subobjects = USubobjectDataSubsystem::Get();
		if (!Blueprint || !Subobjects)
		{
			return Result;
		}

		TArray<FSubobjectDataHandle> Handles;
		Subobjects->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);

		for (const FSubobjectDataHandle& Handle : Handles)
		{
			const FSubobjectData* Data = Handle.GetData();
			if (!Data)
			{
				continue;
			}
			if (Data->IsActor())
			{
				Result.Actor = Handle;
			}
			else if (Data->IsComponent() && Data->GetComponentTemplate())
			{
				Result.Components.Add(Handle);
			}
		}
		return Result;
	}

	const FSubobjectDataHandle* FindComponentHandle(const FComponentHandles& Handles, const FString& Name)
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);
		for (const FSubobjectDataHandle& Handle : Handles.Components)
		{
			const UActorComponent* Component = Handle.GetData()->GetComponentTemplate();
			if (FNodeScribeCatalog::Normalize(SheetComponentName(Component)) == Wanted)
			{
				return &Handle;
			}
		}
		return nullptr;
	}

	/**
	 * The component class by the name on screen, the class name or a path.
	 *
	 * Only what the Components panel offers: a class that is not spawnable from
	 * a Blueprint is refused, not added anyway -- CharacterMovement would
	 * compile and fight the one the Character already has.
	 */
	UClass* FindComponentClass(const FString& Written, FString& OutError)
	{
		if (Written.StartsWith(TEXT("/")))
		{
			UClass* Class = LoadObject<UClass>(nullptr, *Written);
			if (!Class && !Written.EndsWith(TEXT("_C")))
			{
				// A Blueprint's asset path names the Blueprint, not its class.
				Class = LoadObject<UClass>(nullptr, *(Written + TEXT("_C")));
			}
			if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()))
			{
				OutError = FString::Printf(TEXT("`%s` is not a component class."), *Written);
				return nullptr;
			}
			if (!FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(Class))
			{
				OutError = FString::Printf(
					TEXT("`%s` is not offered by the Components panel (not Blueprint-spawnable)."), *Written);
				return nullptr;
			}
			return Class;
		}

		const FString Wanted = FNodeScribeCatalog::Normalize(Written);

		TArray<UClass*> Found;
		TArray<FString> Near;
		bool bFoundUnspawnable = false;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UActorComponent::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}

			const FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_"))
				|| ClassName.StartsWith(TEXT("TRASHCLASS_")))
			{
				continue;
			}

			const FString ByName = FNodeScribeCatalog::Normalize(ComponentClassName(Class));
			const FString ByScreen = FNodeScribeCatalog::Normalize(Class->GetDisplayNameText().ToString());

			if (ByName == Wanted || ByScreen == Wanted)
			{
				if (FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(Class))
				{
					Found.AddUnique(Class);
				}
				else
				{
					bFoundUnspawnable = true;
				}
			}
			else if (Near.Num() < 8 && FKismetEditorUtilities::IsClassABlueprintSpawnableComponent(Class)
				&& (ByName.Contains(Wanted) || FNodeScribeCatalog::IsNearMiss(ByName, Wanted)))
			{
				Near.AddUnique(ComponentClassName(Class));
			}
		}

		if (Found.Num() == 1)
		{
			return Found[0];
		}

		if (Found.Num() > 1)
		{
			TArray<FString> Paths;
			for (const UClass* Class : Found)
			{
				Paths.Add(Class->GetPathName());
			}
			OutError = FString::Printf(TEXT("`%s` is ambiguous; write the path: %s"),
				*Written, *FString::Join(Paths, TEXT(", ")));
			return nullptr;
		}

		if (bFoundUnspawnable)
		{
			OutError = FString::Printf(
				TEXT("`%s` is not offered by the Components panel (not Blueprint-spawnable)."), *Written);
			return nullptr;
		}

		OutError = FString::Printf(TEXT("did not find component class `%s`.%s"), *Written,
			Near.Num() > 0
				? *FString::Printf(TEXT(" Similar: %s"), *FString::Join(Near, TEXT(", ")))
				: TEXT(" A Blueprint component class is only found once loaded; use its path."));
		return nullptr;
	}
}

namespace NodeScribeComponents
{

FString Describe(UBlueprint* Blueprint)
{
	const FComponentHandles Handles = GatherComponentHandles(Blueprint);

	TArray<FString> Parts;
	for (const FSubobjectDataHandle& Handle : Handles.Components)
	{
		const FSubobjectData* Data = Handle.GetData();
		FString Part = SheetComponentName(Data->GetComponentTemplate());

		const FSubobjectData* Parent = Data->GetParentData();
		if (Parent && Parent->IsComponent() && Parent->GetComponentTemplate())
		{
			Part += TEXT(" in ") + SheetComponentName(Parent->GetComponentTemplate());
		}
		Parts.Add(Part);
	}

	return Parts.Num() > 0
		? TEXT("# components: ") + FString::Join(Parts, TEXT(", "))
		: FString();
}

UObject* Add(UBlueprint* Blueprint, const FString& Name, const FString& ClassText,
	const FString& ParentName, FString& OutError)
{
	USubobjectDataSubsystem* Subobjects = USubobjectDataSubsystem::Get();
	if (!Blueprint || !Blueprint->SimpleConstructionScript || !Subobjects)
	{
		OutError = TEXT("only an actor Blueprint has components to add to.");
		return nullptr;
	}

	UClass* Class = FindComponentClass(ClassText, OutError);
	if (!Class)
	{
		return nullptr;
	}

	const FComponentHandles Handles = GatherComponentHandles(Blueprint);
	if (!Handles.Actor.IsValid())
	{
		OutError = TEXT("could not find the actor's root in the Components tree.");
		return nullptr;
	}

	FSubobjectDataHandle ParentHandle = Handles.Actor;
	if (!ParentName.IsEmpty())
	{
		const FSubobjectDataHandle* Parent = FindComponentHandle(Handles, ParentName);
		if (!Parent)
		{
			OutError = FString::Printf(TEXT("`%s` is not a component of this Blueprint."), *ParentName);
			return nullptr;
		}
		if (!Parent->GetData()->IsSceneComponent() || !Class->IsChildOf(USceneComponent::StaticClass()))
		{
			OutError = FString::Printf(
				TEXT("`in %s`: only a scene component goes under another scene component."), *ParentName);
			return nullptr;
		}
		ParentHandle = *Parent;
	}

	FAddNewSubobjectParams Params;
	Params.ParentHandle = ParentHandle;
	Params.NewClass = Class;
	Params.BlueprintContext = Blueprint;

	FText FailReason;
	const FSubobjectDataHandle NewHandle = Subobjects->AddNewSubobject(Params, FailReason);
	if (!NewHandle.IsValid())
	{
		OutError = FailReason.IsEmpty()
			? FString::Printf(TEXT("the Engine refused to add a `%s`."), *ComponentClassName(Class))
			: FailReason.ToString();
		return nullptr;
	}

	// Created under the class's default name, then renamed, as on screen. A
	// rename that fails takes the component back out: one named `BoxComponent`
	// where the text said `Hitbox` would leave every later line of the text
	// pointing at nothing.
	const FText NewName = FText::FromString(Name);
	FText RenameError;
	if (!Subobjects->IsValidRename(NewHandle, NewName, RenameError)
		|| !Subobjects->RenameSubobject(NewHandle, NewName))
	{
		Subobjects->DeleteSubobject(Handles.Actor, NewHandle, Blueprint);
		OutError = RenameError.IsEmpty()
			? FString::Printf(TEXT("could not name the component `%s`."), *Name)
			: FString::Printf(TEXT("`%s`: %s"), *Name, *RenameError.ToString());
		return nullptr;
	}

	// The panel itself edits this template: the handle only hands it out const.
	return const_cast<UActorComponent*>(NewHandle.GetData()->GetComponentTemplate());
}

bool Remove(UBlueprint* Blueprint, const FString& Name, FString& OutError)
{
	USubobjectDataSubsystem* Subobjects = USubobjectDataSubsystem::Get();
	const FComponentHandles Handles = GatherComponentHandles(Blueprint);

	const FSubobjectDataHandle* Handle = FindComponentHandle(Handles, Name);
	if (!Handle || !Subobjects)
	{
		OutError = FString::Printf(TEXT("`%s` is not a component of this Blueprint."), *Name);
		return false;
	}

	const FSubobjectData* Data = Handle->GetData();
	if (Data->IsInheritedComponent() || Data->IsNativeComponent() || !Data->CanDelete())
	{
		OutError = FString::Printf(
			TEXT("`%s` comes from the parent class; only a component this Blueprint added can be deleted."), *Name);
		return false;
	}

	if (Subobjects->DeleteSubobject(Handles.Actor, *Handle, Blueprint) == 0)
	{
		OutError = FString::Printf(TEXT("the Engine refused to delete `%s`."), *Name);
		return false;
	}
	return true;
}

}
