#include "NodeScribeBlendSpace.h"

#include "NodeScribeAnimGraph.h"
#include "NodeScribeParser.h"
#include "NodeScribeTypes.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

namespace NodeScribeBlendSpace
{

namespace
{

/** `X` -> 0, `Y` -> 1, `Z` -> 2. INDEX_NONE para qualquer outra coisa. */
int32 AxisIndexFromLetter(const FString& Letter)
{
	const FString Upper = Letter.TrimStartAndEnd().ToUpper();

	if (Upper == TEXT("X")) { return 0; }
	if (Upper == TEXT("Y")) { return 1; }
	if (Upper == TEXT("Z")) { return 2; }

	return INDEX_NONE;
}

/**
 * O eixo N do BlendSpace, por reflexao.
 *
 * `BlendParameters` e' protegido, mas e' UPROPERTY -- e como array fixo de tres,
 * nao TArray, o indice vai no `ContainerPtrToValuePtr`. Ir por reflexao aqui
 * custa tres linhas e evita depender de a Engine abrir o membro um dia.
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

/** `eixo X : Speed = 0 .. 600` ou `axis Y : Direction = -180 .. 180`. */
bool ParseAxisLine(const FString& Line, int32& OutIndex, FString& OutName, float& OutMin, float& OutMax)
{
	FString Rest = Line;

	bool bIsAxis = false;
	for (const TCHAR* Prefix : { TEXT("eixo "), TEXT("axis ") })
	{
		if (Rest.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			Rest = Rest.Mid(FCString::Strlen(Prefix));
			bIsAxis = true;
			break;
		}
	}

	if (!bIsAxis)
	{
		return false;
	}

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

/** `MM_Idle = 0` ou `MF_Unarmed_Walk_Fwd = 0, 300`. */
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

} // namespace

FResult Write(UBlendSpace* BlendSpace, const FString& Text)
{
	FResult Result;

	if (!BlendSpace)
	{
		Result.Diagnostics.Add(TEXT("[erro]: nenhum BlendSpace."));
		return Result;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	// Duas passagens, e nao uma: um sample fora do intervalo do eixo e' recusado
	// pela Engine, e definir o intervalo depois nao o traz de volta. Assim a
	// ordem em que as linhas aparecem no texto deixa de importar.
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
					TEXT("[erro] linha %d: nao consegui alcancar o eixo."), LineNumber));
				continue;
			}

			if (Max <= Min)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("[erro] linha %d: o intervalo `%g .. %g` esta' invertido ou vazio."),
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
				TEXT("[erro] linha %d: nao entendi `%s`. Esperava `eixo X : Nome = min .. max` ")
				TEXT("ou `Animacao = posicao`."),
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
				TEXT("[erro] linha %d: ha' mais de uma animacao chamada `%s`. Escreva o caminho: %s"),
				Sample.Line, *Sample.Asset, *FString::Join(Lookup.Candidates, TEXT(", "))));
			continue;
		}

		if (!Lookup.IsConfident())
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: nao achei a animacao `%s`."), Sample.Line, *Sample.Asset));
			continue;
		}

		// BlendSpace so' toca AnimSequence. Um BlendSpace, um Montage ou um
		// AimOffset aqui dentro nao existe -- e recusar dizendo o tipo poupa a
		// pergunta seguinte.
		UAnimSequence* Sequence = Cast<UAnimSequence>(Lookup.Asset);
		if (!Sequence)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: `%s` e' um %s. BlendSpace so' aceita AnimSequence."),
				Sample.Line, *Sample.Asset, *Lookup.Asset->GetClass()->GetName()));
			continue;
		}

		if (Sequence->GetSkeleton() != BlendSpace->GetSkeleton())
		{
			// Deixar a Engine recusar daria so' um indice invalido, sem dizer por
			// que -- e esqueleto trocado e' de longe a causa mais comum.
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: `%s` e' do esqueleto `%s`, e este BlendSpace e' do `%s`."),
				Sample.Line, *Sample.Asset,
				Sequence->GetSkeleton() ? *Sequence->GetSkeleton()->GetName() : TEXT("(nenhum)"),
				BlendSpace->GetSkeleton() ? *BlendSpace->GetSkeleton()->GetName() : TEXT("(nenhum)")));
			continue;
		}

		if (BlendSpace->AddSample(Sequence, Sample.Position) == INDEX_NONE)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: a Engine recusou `%s` em %s. ")
				TEXT("Confira se a posicao cabe no intervalo dos eixos."),
				Sample.Line, *Sample.Asset, *Sample.Position.ToString()));
			continue;
		}

		++Result.SamplesAdded;
	}

	// A malha de interpolacao e' recalculada aqui. Sem isto o asset guarda os
	// samples, abre, mostra os pontos -- e nao interpola nada, porque quem
	// interpola e' a malha, e ela ficou do tamanho antigo.
	BlendSpace->ValidateSampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();

	return Result;
}

} // namespace NodeScribeBlendSpace
