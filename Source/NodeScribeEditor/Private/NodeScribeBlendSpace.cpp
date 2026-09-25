#include "NodeScribeBlendSpace.h"

#include "NodeScribeAnimGraph.h"
#include "NodeScribeParser.h"
#include "NodeScribeTypes.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"

namespace NodeScribeBlendSpace
{

namespace
{

/** `X` -> 0, `Y` -> 1, `Z` -> 2. INDEX_NONE for anything else. */
int32 AxisIndexFromLetter(const FString& Letter)
{
	const FString Upper = Letter.TrimStartAndEnd().ToUpper();

	if (Upper == TEXT("X")) { return 0; }
	if (Upper == TEXT("Y")) { return 1; }
	if (Upper == TEXT("Z")) { return 2; }

	return INDEX_NONE;
}

/**
 * The BlendSpace's axis N, through reflection.
 *
 * `BlendParameters` is protected, but it is a UPROPERTY -- and as a fixed
 * array of three, not a TArray, the index goes into `ContainerPtrToValuePtr`.
 * Going through reflection here costs three lines and avoids depending on the
 * Engine opening the member some day.
 */
FBlendParameter* FindAxis(UBlendSpace* BlendSpace, int32 Index)
{
	if (!BlendSpace || Index < 0 || Index > 2)
	{
		return nullptr;
	}

	FStructProperty* Property =
		FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters"));

	return Property ? Property->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, Index) : nullptr;
}

/** `axis X : Speed = 0 .. 600` or `axis Y : Direction = -180 .. 180`. */
bool ParseAxisLine(const FString& Line, int32& OutIndex, FString& OutName, float& OutMin, float& OutMax)
{
	if (!Line.StartsWith(TEXT("axis "), ESearchCase::IgnoreCase))
	{
		return false;
	}

	const FString Rest = Line.Mid(5);

	FString Letter, AfterColon;
	if (!Rest.Split(TEXT(":"), &Letter, &AfterColon))
	{
		return false;
	}

	OutIndex = AxisIndexFromLetter(Letter);
	if (OutIndex == INDEX_NONE)
	{
		return false;
	}

	FString Range;
	if (!AfterColon.Split(TEXT("="), &OutName, &Range))
	{
		return false;
	}

	OutName.TrimStartAndEndInline();

	FString MinText, MaxText;
	if (!Range.Split(TEXT(".."), &MinText, &MaxText))
	{
		return false;
	}

	OutMin = FCString::Atof(*MinText.TrimStartAndEnd());
	OutMax = FCString::Atof(*MaxText.TrimStartAndEnd());

	return true;
}

/** `MM_Idle = 0` or `MF_Unarmed_Walk_Fwd = 0, 300`. */
bool ParseSampleLine(const FString& Line, FString& OutAsset, FVector& OutPosition)
{
	FString Values;
	if (!Line.Split(TEXT("="), &OutAsset, &Values))
	{
		return false;
	}

	OutAsset.TrimStartAndEndInline();
	if (OutAsset.IsEmpty())
	{
		return false;
	}

	TArray<FString> Parts;
	Values.ParseIntoArray(Parts, TEXT(","), true);

	if (Parts.Num() == 0)
	{
		return false;
	}

	OutPosition = FVector::ZeroVector;
	for (int32 Index = 0; Index < Parts.Num() && Index < 3; ++Index)
	{
		OutPosition[Index] = FCString::Atof(*Parts[Index].TrimStartAndEnd());
	}

	return true;
}

/** How many axes this BlendSpace uses: 1 in a BlendSpace1D, 2 in the others. */
int32 AxisCount(const UBlendSpace* BlendSpace)
{
	return BlendSpace && BlendSpace->IsA<UBlendSpace1D>() ? 1 : 2;
}

/** `X=0` or `X=0 Y=300`, only with the axes the BlendSpace has. */
FString DescribePosition(const UBlendSpace* BlendSpace, const FVector& Position)
{
	return AxisCount(BlendSpace) > 1
		? FString::Printf(TEXT("X=%g Y=%g"), Position.X, Position.Y)
		: FString::Printf(TEXT("X=%g"), Position.X);
}

/**
 * The sample already taking this position, if any.
 *
 * The Engine has `IsTooCloseToExistingSamplePoint`, which answers yes or no.
 * Here we want *which one*, to be able to name it in the message -- knowing
 * there is something in the spot without knowing what leaves the person
 * exactly where they were.
 */
const FBlendSample* FindSampleAt(const UBlendSpace* BlendSpace, const FVector& Position)
{
	if (!BlendSpace || !BlendSpace->IsTooCloseToExistingSamplePoint(Position, INDEX_NONE))
	{
		return nullptr;
	}

	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();

	const FBlendSample* Closest = nullptr;
	double BestDistance = TNumericLimits<double>::Max();

	for (const FBlendSample& Sample : Samples)
	{
		const double Distance = FVector::DistSquared(Sample.SampleValue, Position);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Closest = &Sample;
		}
	}

	return Closest;
}

} // namespace

FString Read(const UBlendSpace* BlendSpace)
{
	if (!BlendSpace)
	{
		return FString();
	}

	TArray<FString> Lines;

	static const TCHAR* const Letters[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

	for (int32 Index = 0; Index < AxisCount(BlendSpace); ++Index)
	{
		const FBlendParameter& Axis = BlendSpace->GetBlendParameter(Index);

		// An axis without a name is an axis nobody configured. Writing
		// `axis Y : None` would suggest there is a Y axis to fill in a 1D BlendSpace.
		if (Axis.DisplayName.IsEmpty() || Axis.DisplayName == TEXT("None"))
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("axis %s : %s = %g .. %g"),
			Letters[Index], *Axis.DisplayName, Axis.Min, Axis.Max));
	}

	// An empty grid is this asset's silent hole: the samples are all there, the
	// editor draws the points in the right places, and the BlendSpace Player
	// returns the reference pose because what interpolates is the grid, not the
	// list. Nothing else in the reading gives that away -- and without this line
	// the text of a dead BlendSpace is identical to that of a live one.
	if (BlendSpace->GetBlendSamples().Num() > 0
		&& BlendSpace->GetBlendSpaceData().IsEmpty()
		&& BlendSpace->GetGridSamples().Num() == 0)
	{
		Lines.Add(TEXT("# [warning]: the interpolation grid is empty. The samples below exist, ")
			TEXT("but this BlendSpace returns the reference pose. Call write_blendspace with this ")
			TEXT("same text to rebuild it."));
	}

	for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
	{
		if (!Sample.Animation)
		{
			continue;
		}

		// The full path, not the short name: it is what Write accepts without
		// asking, and a short name that is unique today stops being so the day
		// someone duplicates the animation -- and then the text read stops pasting.
		FString Position = FString::Printf(TEXT("%g"), Sample.SampleValue.X);
		if (AxisCount(BlendSpace) > 1)
		{
			Position += FString::Printf(TEXT(", %g"), Sample.SampleValue.Y);
		}

		Lines.Add(FString::Printf(TEXT("%s = %s"),
			*Sample.Animation->GetPathName(), *Position));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FResult Write(UBlendSpace* BlendSpace, const FString& Text)
{
	FResult Result;

	if (!BlendSpace)
	{
		Result.Diagnostics.Add(TEXT("[error]: no BlendSpace."));
		return Result;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	// Two passes, not one: a sample outside the axis range is refused by the
	// Engine, and setting the range afterwards does not bring it back. That way
	// the order in which the lines appear in the text stops mattering.
	struct FPendingSample
	{
		int32 Line = 0;
		FString Asset;
		FVector Position = FVector::ZeroVector;
	};
	TArray<FPendingSample> Pending;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			continue;
		}

		const int32 LineNumber = Index + 1;

		int32 AxisIndex = INDEX_NONE;
		FString AxisName;
		float Min = 0.f;
		float Max = 0.f;

		if (ParseAxisLine(Line, AxisIndex, AxisName, Min, Max))
		{
			FBlendParameter* Axis = FindAxis(BlendSpace, AxisIndex);
			if (!Axis)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("[error] line %d: could not reach the axis."), LineNumber));
				continue;
			}

			if (Max <= Min)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("[error] line %d: the range `%g .. %g` is inverted or empty."),
					LineNumber, Min, Max));
				continue;
			}

			Axis->DisplayName = AxisName;
			Axis->Min = Min;
			Axis->Max = Max;
			++Result.AxesSet;
			continue;
		}

		FPendingSample Sample;
		Sample.Line = LineNumber;

		if (!ParseSampleLine(Line, Sample.Asset, Sample.Position))
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: did not understand `%s`. Expected `axis X : Name = min .. max` ")
				TEXT("or `Animation = position`."),
				LineNumber, *Line));
			continue;
		}

		Pending.Add(MoveTemp(Sample));
	}

	for (const FPendingSample& Sample : Pending)
	{
		const NodeScribeAnimGraph::FAssetLookup Lookup =
			NodeScribeAnimGraph::FindAnimationAsset(Sample.Asset);

		if (Lookup.IsAmbiguous())
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: there is more than one animation called `%s`. Write the path: %s"),
				Sample.Line, *Sample.Asset, *FString::Join(Lookup.Candidates, TEXT(", "))));
			continue;
		}

		if (!Lookup.IsConfident())
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: could not find animation `%s`."), Sample.Line, *Sample.Asset));
			continue;
		}

		// A BlendSpace only plays AnimSequences. A BlendSpace, a Montage or an
		// AimOffset in here does not exist -- and refusing while naming the type
		// spares the next question.
		UAnimSequence* Sequence = Cast<UAnimSequence>(Lookup.Asset);
		if (!Sequence)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: `%s` is a %s. A BlendSpace only accepts AnimSequence."),
				Sample.Line, *Sample.Asset, *Lookup.Asset->GetClass()->GetName()));
			continue;
		}

		if (Sequence->GetSkeleton() != BlendSpace->GetSkeleton())
		{
			// Letting the Engine refuse would give only an invalid index, without
			// saying why -- and a swapped skeleton is by far the most common cause.
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: `%s` belongs to skeleton `%s`, and this BlendSpace to `%s`."),
				Sample.Line, *Sample.Asset,
				Sequence->GetSkeleton() ? *Sequence->GetSkeleton()->GetName() : TEXT("(none)"),
				BlendSpace->GetSkeleton() ? *BlendSpace->GetSkeleton()->GetName() : TEXT("(none)")));
			continue;
		}

		// The Engine refuses a sample for two reasons, and returns the same
		// INDEX_NONE for both. Asking first is what separates "the position does
		// not fit the axis" from "there is already a sample there" -- and the
		// second, told as if it were the first, sends you to check a range that
		// is right. That is exactly what happened: `MM_Idle = 0` refused in a
		// BlendSpace whose axis went from 0 to 600, because the sample was
		// already there.
		if (!BlendSpace->IsSampleWithinBounds(Sample.Position))
		{
			const FBlendParameter& Axis = BlendSpace->GetBlendParameter(0);
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: %s is outside the axes' range (X goes from %g to %g)."),
				Sample.Line, *DescribePosition(BlendSpace, Sample.Position), Axis.Min, Axis.Max));
			continue;
		}

		if (const FBlendSample* Occupant = FindSampleAt(BlendSpace, Sample.Position))
		{
			// Same animation, same place: it already is as the text asks.
			// Repeating the call should not become an error, for the same reason
			// `variable X` twice does not -- pasting the same text again is routine.
			if (Occupant->Animation == Sequence)
			{
				continue;
			}

			// Swapping on its own would be deciding. The sample that is there was
			// put there by someone, and replacing it silently swaps the animation
			// of a whole BlendSpace with nothing in the return saying what vanished.
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: %s already has `%s`. I will not replace it with `%s` on my own -- ")
				TEXT("delete the sample in the editor, or write another position."),
				Sample.Line, *DescribePosition(BlendSpace, Sample.Position),
				Occupant->Animation ? *Occupant->Animation->GetName() : TEXT("(no animation)"),
				*Sample.Asset));
			continue;
		}

		if (BlendSpace->AddSample(Sequence, Sample.Position) == INDEX_NONE)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[error] line %d: the Engine refused `%s` at %s, and neither the position nor the ")
				TEXT("skeleton explain it. Check the animation's type (additive vs normal)."),
				Sample.Line, *Sample.Asset, *DescribePosition(BlendSpace, Sample.Position)));
			continue;
		}

		++Result.SamplesAdded;
	}

	// The interpolation grid is rebuilt here, and what rebuilds it is
	// `ResampleData`. `ValidateSampleData` alone does not work, and the name is
	// misleading: when the samples change it *erases* the grid
	// (`GridSamples.Empty()`) and stops there. What fills it again -- the grid
	// and the triangulation's `BlendSpaceData`, which is what the BlendSpace
	// Player reads at runtime -- is `ResampleData`, which already calls
	// `ValidateSampleData` on the way.
	//
	// Without this the asset keeps the samples, opens in the editor, shows the
	// points, and interpolates nothing: the node goes into the AnimGraph, the
	// Blueprint compiles without a single warning, and the character stays in
	// the reference pose. It is the most silent hole this file knows how to open.
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();

	return Result;
}

} // namespace NodeScribeBlendSpace
