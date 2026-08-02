#include "NodeScribeObjectWriter.h"

#include "NodeScribeAIWriter.h"
#include "NodeScribeBuilder.h"
#include "NodeScribeCatalog.h"
#include "NodeScribeObjectTarget.h"
#include "NodeScribeParser.h"
#include "NodeScribePropertyText.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

using namespace NodeScribeObjectTarget;

namespace
{
	using namespace NodeScribePropertyText;

	/**
	 * Acha a propriedade pelo nome que a ficha escreve.
	 *
	 * A comparacao e' a mesma dos pinos: `max walk speed`, `MaxWalkSpeed` e
	 * `Max_Walk_Speed` dao no mesmo. Quem escreve a ficha de volta esta' copiando
	 * o que leu, e o que leu era o nome de tela.
	 */
	FProperty* FindProperty(UStruct* Owner, const FString& Name, TArray<FString>& OutNear)
	{
		const FString Wanted = FNodeScribeCatalog::Normalize(Name);

		for (TFieldIterator<FProperty> It(Owner, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!IsVisible(Property))
			{
				continue;
			}

			if (FNodeScribeCatalog::Normalize(DisplayName(Property)) == Wanted
				|| FNodeScribeCatalog::Normalize(Property->GetName()) == Wanted)
			{
				return Property;
			}
		}

		// Sem acerto exato, junta os parecidos: o erro comum e' nome quase certo,
		// e uma lista curta resolve mais rapido que "nao achei".
		for (TFieldIterator<FProperty> It(Owner, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsVisible(Property))
			{
				continue;
			}
			const FString Candidate = FNodeScribeCatalog::Normalize(DisplayName(Property));
			if (Candidate.Contains(Wanted) || Wanted.Contains(Candidate))
			{
				OutNear.Add(DisplayName(Property));
			}
		}

		return nullptr;
	}

	/** Quantos espacos abrem a linha. Dois por nivel, como no resto do formato. */
	int32 MeasureIndent(const FString& Line)
	{
		int32 Spaces = 0;
		while (Spaces < Line.Len() && Line[Spaces] == TEXT(' '))
		{
			++Spaces;
		}
		return Spaces / 2;
	}

	bool IsResetToDefault(const FString& Value)
	{
		return Value.Equals(TEXT("padrao"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("padrão"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("default"), ESearchCase::IgnoreCase);
	}
}

FNodeScribeObjectWriter::FResult FNodeScribeObjectWriter::WriteObject(
	UObject* Object, const FString& Text)
{
	FResult Result;

	if (!Object)
	{
		Result.Diagnostics.Add(TEXT("[erro]: nenhum objeto informado."));
		return Result;
	}

	// Asset de IA tem forma propria na ida e na volta, igual ao leitor.
	if (FNodeScribeAIWriter::Handles(Object))
	{
		const FNodeScribeAIWriter::FResult AIResult =
			FNodeScribeAIWriter::WriteAsset(Object, Text);

		Result.Applied = AIResult.Applied;
		Result.Diagnostics = AIResult.Diagnostics;
		return Result;
	}

	UObject* Root = ResolveTarget(Object);
	if (!Root)
	{
		Result.Diagnostics.Add(FString::Printf(
			TEXT("[erro]: `%s` nao tem classe compilada -- compile o Blueprint antes."),
			*Object->GetName()));
		return Result;
	}

	UBlueprint* Blueprint = FindBlueprint(Object, Root->GetClass());
	const TMap<FString, UObject*> Components = CollectComponents(Root, Blueprint);

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	const FScopedTransaction Transaction(
		LOCTEXT("WriteObjectTransaction", "NodeScribe: escrever ficha"));

	// O alvo de cada nivel de indentacao. [0] e' o objeto; um bloco de
	// componente troca o [0]; um bloco de struct empilha em [1].
	UObject* CurrentObject = Root;
	FString CurrentObjectLabel;

	// Bloco de struct aberto, quando ha' um.
	FStructProperty* OpenStruct = nullptr;
	void* OpenStructValue = nullptr;
	int32 OpenStructIndent = -1;

	TSet<UObject*> Touched;
	bool bTouchedBlueprint = false;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const int32 Indent = MeasureIndent(Lines[Index]);
		FString Line = FNodeScribeParser::StripComment(Lines[Index]).TrimStartAndEnd();

		if (Line.IsEmpty())
		{
			continue;
		}

		// Cabecalho da ficha: e' saida do leitor, nao instrucao.
		if (Line.StartsWith(TEXT("ficha ")) || Line.StartsWith(TEXT("//")))
		{
			continue;
		}

		if (OpenStruct && Indent <= OpenStructIndent)
		{
			OpenStruct = nullptr;
			OpenStructValue = nullptr;
		}

		// Indentacao zero fecha o bloco de componente. Sem isto o bloco nunca
		// terminava: uma linha do proprio ator, escrita depois de um componente,
		// continuava sendo aplicada no componente. Se ele tivesse uma
		// propriedade com aquele nome, gravava no lugar errado sem dizer nada --
		// e o erro so' apareceria rodando.
		if (Indent == 0)
		{
			CurrentObject = Root;
			CurrentObjectLabel.Empty();
			OpenStruct = nullptr;
			OpenStructValue = nullptr;
		}

		// `apagar variavel Nome`. Precisa de palavra propria: este escritor nao
		// apaga nada por principio, e apagar variavel derruba todo node que a
		// usava. Nao pode acontecer por descuido de formatacao.
		if (Line.StartsWith(TEXT("apagar variavel "), ESearchCase::IgnoreCase)
			|| Line.StartsWith(TEXT("remover variavel "), ESearchCase::IgnoreCase))
		{
			const FString VarName = Line.RightChop(Line.Find(TEXT("variavel "),
				ESearchCase::IgnoreCase) + 9).TrimStartAndEnd();

			if (!Blueprint)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("linha %d [erro]: `%s` nao e' um Blueprint; nao ha' variavel para apagar."),
					LineNumber, *Object->GetName()));
				continue;
			}

			const bool bExists = Blueprint->NewVariables.ContainsByPredicate(
				[&VarName](const FBPVariableDescription& Variable)
				{
					return Variable.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase);
				});

			if (!bExists)
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("linha %d [erro]: `%s` nao e' variavel deste Blueprint."),
					LineNumber, *VarName));
				continue;
			}

			Blueprint->Modify();
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VarName));
			bTouchedBlueprint = true;
			++Result.Applied;
			continue;
		}

		// `variavel Nome : Tipo [editavel] [= valor]`.
		//
		// Cria quando nao existe, e ai' o tipo e' obrigatorio. Quando ja' existe,
		// o tipo e' ignorado -- trocar tipo de variavel usada quebra os nodes que
		// a consomem, e isso pede uma decisao, nao um efeito colateral.
		if (Line.StartsWith(TEXT("variavel "), ESearchCase::IgnoreCase))
		{
			Line = Line.RightChop(9).TrimStart();

			int32 Equals = INDEX_NONE;
			const bool bHasValue = Line.FindChar(TEXT('='), Equals);

			FString Declaration = bHasValue ? Line.Left(Equals).TrimEnd() : Line;
			const FString Value = bHasValue ? Line.RightChop(Equals + 1).TrimStart() : FString();

			// `editavel` fecha a declaracao, como `sincronizada` no blackboard.
			bool bInstanceEditable = false;
			for (const TCHAR* Word : { TEXT(" editavel"), TEXT(" editável"), TEXT(" instance editable") })
			{
				if (Declaration.EndsWith(Word, ESearchCase::IgnoreCase))
				{
					Declaration = Declaration.LeftChop(FCString::Strlen(Word)).TrimEnd();
					bInstanceEditable = true;
					break;
				}
			}

			int32 Colon = INDEX_NONE;
			const bool bHasType = Declaration.FindChar(TEXT(':'), Colon);

			const FString VarName = (bHasType ? Declaration.Left(Colon) : Declaration).TrimEnd();
			const FString TypeName = bHasType ? Declaration.RightChop(Colon + 1).TrimStart() : FString();

			const bool bExists = Blueprint && Blueprint->NewVariables.ContainsByPredicate(
				[&VarName](const FBPVariableDescription& Variable)
				{
					return Variable.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase);
				});

			if (!bExists)
			{
				if (!Blueprint)
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("linha %d [erro]: `%s` nao existe, e `%s` nao e' um Blueprint onde criar."),
						LineNumber, *VarName, *Object->GetName()));
					continue;
				}

				if (!bHasType)
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("linha %d [erro]: `%s` nao existe. Para criar, diga o tipo: `variavel %s : Float`."),
						LineNumber, *VarName, *VarName));
					continue;
				}

				FEdGraphPinType PinType;
				if (!NodeScribeTypeNames::ResolvePinTypeFromName(TypeName, PinType))
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("linha %d [erro]: nao reconheci o tipo `%s`. Use o nome que aparece na ")
						TEXT("interface, como Float, Name, Timer Handle, ou `Array de X`."),
						LineNumber, *TypeName));
					continue;
				}

				Blueprint->Modify();
				if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VarName), PinType, Value))
				{
					Result.Diagnostics.Add(FString::Printf(
						TEXT("linha %d [erro]: nao consegui criar `%s`."), LineNumber, *VarName));
					continue;
				}

				bTouchedBlueprint = true;
				++Result.Applied;

				// Criada com o valor junto: nao ha' o que aplicar depois. E o CDO
				// so' passa a ter a propriedade na proxima compilacao, entao
				// tentar escrever nele agora nao acharia nada.
				if (bInstanceEditable)
				{
					FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(
						Blueprint, FName(*VarName), /*bNewBlueprintOnly*/ false);
				}
				continue;
			}

			if (bInstanceEditable && Blueprint)
			{
				Blueprint->Modify();
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(
					Blueprint, FName(*VarName), /*bNewBlueprintOnly*/ false);
				bTouchedBlueprint = true;
			}

			if (!bHasValue)
			{
				// So' declaracao de variavel que ja' existe: nada a fazer.
				continue;
			}

			// Existe: cai no caminho comum de escrever valor de propriedade.
			Line = VarName + TEXT(" = ") + Value;
		}

		int32 Equals = INDEX_NONE;
		const bool bHasEquals = Line.FindChar(TEXT('='), Equals);

		// Sem `=` e terminando em `:` e' abertura de bloco.
		if (!bHasEquals)
		{
			FString Label = Line;
			Label.RemoveFromEnd(TEXT(":"));

			// `Comp : Tipo` -- o tipo e' informativo, o nome e' o que importa.
			int32 Colon = INDEX_NONE;
			if (Label.FindChar(TEXT(':'), Colon))
			{
				Label = Label.Left(Colon);
			}
			Label.TrimStartAndEndInline();

			if (UObject* const* Component = Components.Find(Label))
			{
				CurrentObject = *Component;
				CurrentObjectLabel = Label;
				OpenStruct = nullptr;
				continue;
			}

			// Nao e' componente: pode ser bloco de struct do objeto corrente.
			TArray<FString> Near;
			FProperty* Property = FindProperty(CurrentObject->GetClass(), Label, Near);
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				OpenStruct = StructProperty;
				OpenStructValue = StructProperty->ContainerPtrToValuePtr<void>(CurrentObject);
				OpenStructIndent = Indent;
				continue;
			}

			Result.Diagnostics.Add(FString::Printf(
				TEXT("linha %d [erro]: `%s` nao e' componente nem struct deste objeto.%s"),
				LineNumber, *Label,
				Near.Num() > 0
					? *FString::Printf(TEXT(" Parecidos: %s"), *FString::Join(Near, TEXT(", ")))
					: TEXT("")));
			continue;
		}

		const FString Name = Line.Left(Equals).TrimEnd();
		const FString Value = Line.RightChop(Equals + 1).TrimStart();

		// Dentro de bloco de struct, o dono da propriedade e' a struct.
		UStruct* Owner = OpenStruct ? static_cast<UStruct*>(OpenStruct->Struct)
			: static_cast<UStruct*>(CurrentObject->GetClass());
		void* Container = OpenStruct ? OpenStructValue : static_cast<void*>(CurrentObject);

		TArray<FString> Near;
		FProperty* Property = FindProperty(Owner, Name, Near);
		if (!Property)
		{
			Result.Diagnostics.Add(FString::Printf(
				TEXT("linha %d [erro]: `%s` nao existe em %s.%s"),
				LineNumber, *Name,
				CurrentObjectLabel.IsEmpty() ? *Root->GetClass()->GetName() : *CurrentObjectLabel,
				Near.Num() > 0
					? *FString::Printf(TEXT(" Parecidos: %s"), *FString::Join(Near, TEXT(", ")))
					: TEXT("")));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

		CurrentObject->Modify();
		Touched.Add(CurrentObject);

		if (IsResetToDefault(Value))
		{
			UObject* Archetype = CurrentObject->GetArchetype();
			const FProperty* DefaultProperty = Archetype
				? Archetype->GetClass()->FindPropertyByName(Property->GetFName()) : nullptr;

			if (!OpenStruct && DefaultProperty && DefaultProperty->SameType(Property))
			{
				Property->CopyCompleteValue(ValuePtr,
					DefaultProperty->ContainerPtrToValuePtr<void>(Archetype));
			}
			else
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("linha %d [erro]: nao achei valor de fabrica para `%s`."),
					LineNumber, *Name));
				continue;
			}
		}
		else
		{
			FString Error;
			if (!TextToValue(Property, ValuePtr, Value, Error))
			{
				Result.Diagnostics.Add(FString::Printf(
					TEXT("linha %d [erro]: %s"), LineNumber, *Error));
				continue;
			}
		}

		FPropertyChangedEvent Changed(Property);
		CurrentObject->PostEditChangeProperty(Changed);

		++Result.Applied;
	}

	if (Result.Applied > 0 && Blueprint)
	{
		// Mexer na lista de variaveis muda a classe, nao so' um valor: sem
		// recompilar, o CDO continua com a forma antiga e o painel de detalhes
		// mostra o que nao existe mais.
		if (bTouchedBlueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

			// E compila. `AddMemberVariable` mexe na lista do Blueprint, mas a
			// propriedade so' passa a existir na classe depois disto -- antes,
			// a variavel recem-criada nao aparece nem na ficha nem no painel de
			// detalhes, e quem chamou teria que pedir um clique em Compile.
			// Criar algo que nao da' para ver e' pior que nao criar.
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
		else
		{
			// Sem isto o asset fica com a mudanca em memoria e limpo em disco:
			// fecha o editor e o trabalho some sem ninguem avisar.
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
