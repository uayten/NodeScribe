#include "NodeScribeAnimGraph.h"

#include "NodeScribeCatalog.h"
#include "NodeScribeTypes.h"

#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateResult.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UObjectIterator.h"

namespace NodeScribeAnimGraph
{

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

bool IsAnimationGraph(const UEdGraph* Graph)
{
	if (!Graph || !Graph->GetSchema())
	{
		return false;
	}

	// The schema covers the AnimGraph and a state's interior at once -- both
	// accept anim nodes. Testing the graph's class would leave out the state's
	// interior, which is exactly where the state machine needs to write.
	//
	// **It does not cover the transition rule.** UAnimationTransitionSchema
	// descends from UEdGraphSchema_K2, not from here, because the rule has no
	// pose: it is a data chain ending in a bool. Whoever needs it tests
	// `IsA<UAnimationTransitionGraph>()`.
	return Graph->GetSchema()->IsA<UAnimationGraphSchema>();
}

bool IsStateMachineGraph(const UEdGraph* Graph)
{
	return Graph && Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationStateMachineSchema>();
}

// ---------------------------------------------------------------------------
// Pose pins
// ---------------------------------------------------------------------------

bool IsPosePin(const UEdGraphPin* Pin)
{
	return Pin && UAnimationGraphSchema::IsPosePin(Pin->PinType);
}

UEdGraphPin* FindPoseInput(UEdGraphNode* Node)
{
	if (!Node)
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && IsPosePin(Pin))
		{
			return Pin;
		}
	}
	return nullptr;
}

TArray<UEdGraphPin*> GetPoseInputs(UEdGraphNode* Node)
{
	TArray<UEdGraphPin*> Inputs;
	if (!Node)
	{
		return Inputs;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && IsPosePin(Pin))
		{
			Inputs.Add(Pin);
		}
	}
	return Inputs;
}

TArray<UEdGraphPin*> GetPoseOutputs(UEdGraphNode* Node)
{
	TArray<UEdGraphPin*> Outputs;
	if (!Node)
	{
		return Outputs;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && IsPosePin(Pin))
		{
			Outputs.Add(Pin);
		}
	}
	return Outputs;
}

UEdGraphNode* FindOutputPose(UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}

	// An AnimGraph ends at Root; a state's interior ends at StateResult. They
	// are two different nodes with the same role, and neither is created: both
	// are born together with the graph.
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && (Node->IsA<UAnimGraphNode_Root>() || Node->IsA<UAnimGraphNode_StateResult>()))
		{
			return Node;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Node class index
// ---------------------------------------------------------------------------

namespace
{

struct FEntry
{
	TWeakObjectPtr<UClass> NodeClass;

	/** Class name without the prefix: `UAnimGraphNode_BlendSpacePlayer` -> "blendspaceplayer". */
	FString NormalizedClassName;

	/** Normalised menu title: "blendposesbybool". */
	FString NormalizedTitle;

	/** Readable name to list in an ambiguity error. */
	FString Display;
};

TArray<FEntry> GEntries;
bool GBuilt = false;

/** The title the node shows in the graph's menu, read from the CDO. */
FString TitleForClass(UClass* NodeClass)
{
	const UAnimGraphNode_Base* CDO = Cast<UAnimGraphNode_Base>(NodeClass->GetDefaultObject(false));
	if (!CDO)
	{
		return FString();
	}

	// Some nodes build their title from the asset they wrap and return empty
	// on the CDO. For those the class name is the only stable name, and it is
	// already indexed separately.
	return CDO->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
}

void BuildIndex()
{
	GEntries.Reset();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UAnimGraphNode_Base::StaticClass()))
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (Class == UAnimGraphNode_Base::StaticClass())
		{
			continue;
		}

		FEntry Entry;
		Entry.NodeClass = Class;

		FString ClassName = Class->GetName();
		ClassName.RemoveFromStart(TEXT("AnimGraphNode_"));
		Entry.NormalizedClassName = FNodeScribeCatalog::Normalize(ClassName);

		const FString Title = TitleForClass(Class);
		Entry.NormalizedTitle = FNodeScribeCatalog::Normalize(Title);
		Entry.Display = Title.IsEmpty() ? ClassName : Title;

		GEntries.Add(MoveTemp(Entry));
	}

	GBuilt = true;
}

void EnsureBuilt()
{
	if (!GBuilt)
	{
		BuildIndex();
	}
}

} // namespace

FLookup FindNodeClass(const FString& Query)
{
	EnsureBuilt();

	FLookup Result;
	const FString Normalized = FNodeScribeCatalog::Normalize(Query);
	if (Normalized.IsEmpty())
	{
		return Result;
	}

	TArray<const FEntry*> Exact;
	for (const FEntry& Entry : GEntries)
	{
		if (!Entry.NodeClass.IsValid())
		{
			continue;
		}
		if (Entry.NormalizedClassName == Normalized || Entry.NormalizedTitle == Normalized)
		{
			Exact.Add(&Entry);
		}
	}

	if (Exact.Num() == 1)
	{
		Result.NodeClass = Exact[0]->NodeClass.Get();
		return Result;
	}

	if (Exact.Num() > 1)
	{
		// The same name on two classes: list instead of choosing, for the same
		// reason as the function catalog -- guessing compiles and runs wrong.
		for (const FEntry* Entry : Exact)
		{
			Result.Candidates.Add(Entry->Display);
		}
		return Result;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------

namespace
{

/** Normalised short name -> every asset that answers to it. */
TMultiMap<FString, FSoftObjectPath> GAssetsByName;
bool GAssetsBuilt = false;

void BuildAssetIndex()
{
	GAssetsByName.Reset();

	const FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UAnimationAsset::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(Filter, Assets);

	// We keep the path, not the asset: sweeping the registry is already
	// expensive once, and loading every animation in the project to build an
	// index would be worse than the problem. Only the one a line asks for is
	// loaded.
	for (const FAssetData& Data : Assets)
	{
		GAssetsByName.Add(FNodeScribeCatalog::Normalize(Data.AssetName.ToString()), Data.GetSoftObjectPath());
	}

	GAssetsBuilt = true;
}

} // namespace

FAssetLookup FindAnimationAsset(const FString& Query)
{
	FAssetLookup Result;

	const FString Trimmed = Query.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return Result;
	}

	// Full path: load directly, without asking the registry.
	if (Trimmed.StartsWith(TEXT("/")))
	{
		Result.Asset = LoadObject<UAnimationAsset>(nullptr, *Trimmed);
		return Result;
	}

	if (!GAssetsBuilt)
	{
		BuildAssetIndex();
	}

	TArray<FSoftObjectPath> Matches;
	GAssetsByName.MultiFind(FNodeScribeCatalog::Normalize(Trimmed), Matches);

	// Zero results means two different things: the asset does not exist, or it
	// exists and is newer than the index. The index is built once per session,
	// so a freshly created BlendSpace -- by `create_asset`, or by someone
	// clicking in the editor -- stayed out until the editor reopened, and the
	// answer was "no node called X" for an asset that was right there on
	// screen. Rebuilding the index costs one registry sweep, and only on the
	// path that was about to fail anyway.
	if (Matches.Num() == 0)
	{
		BuildAssetIndex();
		GAssetsByName.MultiFind(FNodeScribeCatalog::Normalize(Trimmed), Matches);
	}

	if (Matches.Num() == 1)
	{
		Result.Asset = Cast<UAnimationAsset>(Matches[0].TryLoad());
		return Result;
	}

	for (const FSoftObjectPath& Path : Matches)
	{
		Result.Candidates.Add(Path.ToString());
	}
	return Result;
}

UClass* NodeClassForAsset(const UAnimationAsset* Asset)
{
	if (!Asset)
	{
		return nullptr;
	}

	// The same mapping dragging the asset into the graph uses.
	return GetNodeClassForAsset(Asset->GetClass());
}

void Invalidate()
{
	GEntries.Reset();
	GBuilt = false;
	GAssetsBuilt = false;
}

} // namespace NodeScribeAnimGraph
