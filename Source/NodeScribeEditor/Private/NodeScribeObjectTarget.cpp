#include "NodeScribeObjectTarget.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"

namespace NodeScribeObjectTarget
{

UObject* ResolveTarget(UObject* Object)
{
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		UClass* Generated = Blueprint->GeneratedClass.Get();
		return Generated ? Generated->GetDefaultObject() : nullptr;
	}

	if (UClass* Class = Cast<UClass>(Object))
	{
		return Class->GetDefaultObject();
	}

	return Object;
}

UBlueprint* FindBlueprint(const UObject* Requested, const UClass* Class)
{
	if (UBlueprint* Direct = const_cast<UBlueprint*>(Cast<UBlueprint>(Requested)))
	{
		return Direct;
	}
	return Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
}

TMap<FString, UObject*> CollectComponents(UObject* Target, UBlueprint* Blueprint)
{
	TMap<FString, UObject*> Components;

	if (const AActor* Actor = Cast<AActor>(Target))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				Components.Add(Component->GetName(), Component);
			}
		}
	}

	// Walks up the chain: a component the parent Blueprint created belongs to the child too.
	for (const UBlueprint* Current = Blueprint; Current; )
	{
		if (const USimpleConstructionScript* SCS = Current->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : SCS->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate)
				{
					Components.Add(Node->GetVariableName().ToString(), Node->ComponentTemplate);
				}
			}
		}

		const UClass* ParentClass = Current->ParentClass;
		Current = ParentClass ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
	}

	return Components;
}

}
