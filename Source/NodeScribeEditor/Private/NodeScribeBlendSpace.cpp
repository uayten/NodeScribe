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

/** Quantos eixos este BlendSpace usa: 1 num BlendSpace1D, 2 nos outros. */
int32 AxisCount(const UBlendSpace* BlendSpace)
{
	return BlendSpace && BlendSpace->IsA<UBlendSpace1D>() ? 1 : 2;
}

/** `X=0` ou `X=0, Y=300`, so' com os eixos que o BlendSpace tem. */
FString DescribePosition(const FVector& Position)
{
	return FString::Printf(TEXT("X=%g Y=%g"), Position.X, Position.Y);
}

/**
 * O sample que ja' ocupa esta posicao, se houver.
 *
 * A Engine tem `IsTooCloseToExistingSamplePoint`, que responde sim ou nao. Aqui
 * queremos *qual*, para poder dizer o nome dele na mensagem -- saber que ha'
 * algo no lugar sem saber o que e' deixa a pessoa exatamente onde estava.
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

		// Eixo sem nome e' eixo que ninguem configurou. Escrever `eixo Y : None`
		// sugeriria que ha' um eixo Y para preencher num BlendSpace 1D.
		if (Axis.DisplayName.IsEmpty() || Axis.DisplayName == TEXT("None"))
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("eixo %s : %s = %g .. %g"),
			Letters[Index], *Axis.DisplayName, Axis.Min, Axis.Max));
	}

	// A malha vazia e' o buraco silencioso deste asset: os samples estao todos
	// la', o editor desenha os pontos nos lugares certos, e o BlendSpace Player
	// devolve pose de referencia porque quem interpola e' a malha, nao a lista.
	// Nada mais na leitura denuncia isso -- e sem esta linha o texto de um
	// BlendSpace morto e' identico ao de um vivo.
	if (BlendSpace->GetBlendSamples().Num() > 0
		&& BlendSpace->GetBlendSpaceData().IsEmpty()
		&& BlendSpace->GetGridSamples().Num() == 0)
	{
		Lines.Add(TEXT("# [aviso]: a malha de interpolacao esta' vazia. Os samples abaixo existem, ")
			TEXT("mas este BlendSpace devolve pose de referencia. Chame write_blendspace com este ")
			TEXT("mesmo texto para reconstrui-la."));
	}

	for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
	{
		if (!Sample.Animation)
		{
			continue;
		}

		// O caminho completo, e nao o nome curto: e' o que Write aceita sem
		// perguntar, e um nome curto que hoje e' unico deixa de ser no dia em
		// que alguem duplicar a animacao -- e ai o texto lido para de colar.
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

		// A Engine recusa um sample por dois motivos, e devolve o mesmo
		// INDEX_NONE nos dois. Perguntar antes e' o que separa "a posicao nao
		// cabe no eixo" de "ja' tem um sample ai'" -- e a segunda, dita como se
		// fosse a primeira, manda conferir um intervalo que esta' certo. Foi
		// exatamente o que aconteceu: `MM_Idle = 0` recusado num BlendSpace cujo
		// eixo ia de 0 a 600, porque o sample ja' estava la'.
		if (!BlendSpace->IsSampleWithinBounds(Sample.Position))
		{
			const FBlendParameter& Axis = BlendSpace->GetBlendParameter(0);
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: %s esta' fora do intervalo dos eixos (o X vai de %g a %g)."),
				Sample.Line, *DescribePosition(Sample.Position), Axis.Min, Axis.Max));
			continue;
		}

		if (const FBlendSample* Occupant = FindSampleAt(BlendSpace, Sample.Position))
		{
			// Mesma animacao, mesmo lugar: ja' esta' como o texto pede. Repetir a
			// chamada nao deve virar erro, pelo mesmo motivo que `variavel X`
			// duas vezes nao vira -- colar o mesmo texto de novo e' rotina.
			if (Occupant->Animation == Sequence)
			{
				continue;
			}

			// Trocar por conta propria seria decidir. O sample que esta' la' foi
			// posto por alguem, e substituir calado troca a animacao de um
			// BlendSpace inteiro sem nada no retorno dizendo o que sumiu.
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: %s ja' tem `%s`. Nao troco por `%s` sozinho -- ")
				TEXT("apague o sample no editor, ou escreva outra posicao."),
				Sample.Line, *DescribePosition(Sample.Position),
				Occupant->Animation ? *Occupant->Animation->GetName() : TEXT("(sem animacao)"),
				*Sample.Asset));
			continue;
		}

		if (BlendSpace->AddSample(Sequence, Sample.Position) == INDEX_NONE)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("[erro] linha %d: a Engine recusou `%s` em %s, e nem a posicao nem o ")
				TEXT("esqueleto explicam. Confira o tipo da animacao (aditiva x normal)."),
				Sample.Line, *Sample.Asset, *DescribePosition(Sample.Position)));
			continue;
		}

		++Result.SamplesAdded;
	}

	// A malha de interpolacao e' reconstruida aqui, e quem reconstroi e'
	// `ResampleData`. `ValidateSampleData` sozinho nao serve, e o nome engana:
	// quando os samples mudam ele *apaga* a malha (`GridSamples.Empty()`) e para
	// ali. Quem a preenche de novo -- a malha e o `BlendSpaceData` da
	// triangulacao, que e' o que o BlendSpace Player le' ao rodar -- e'
	// `ResampleData`, que ja' chama `ValidateSampleData` no caminho.
	//
	// Sem isto o asset guarda os samples, abre no editor, mostra os pontos, e
	// nao interpola nada: o node entra no AnimGraph, o Blueprint compila sem um
	// aviso sequer, e o personagem fica na pose de referencia. E' o buraco mais
	// silencioso que este arquivo sabe abrir.
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();

	return Result;
}

} // namespace NodeScribeBlendSpace
