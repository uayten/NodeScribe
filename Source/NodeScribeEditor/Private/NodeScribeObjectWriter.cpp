#include "NodeScribeObjectWriter.h"

#include "NodeScribeAIWriter.h"
#include "NodeScribeCatalog.h"
#include "NodeScribePropertyText.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

namespace
{
	using namespace NodeScribePropertyText;

	/** O objeto que carrega os valores: Blueprint e classe viram o CDO delas. */
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

	/** Os componentes do alvo, pelo nome com que a ficha os escreve. */
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

	FString StripComment(const FString& Line)
	{
		int32 Hash = INDEX_NONE;

		// Um `#` dentro de aspas e' valor, nao comentario.
		bool bInQuote = false;
		TCHAR QuoteChar = TEXT('\0');
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR C = Line[Index];
			if (bInQuote)
			{
				if (C == QuoteChar) { bInQuote = false; }
			}
			else if (C == TEXT('"') || C == TEXT('\''))
			{
				bInQuote = true;
				QuoteChar = C;
			}
			else if (C == TEXT('#'))
			{
				Hash = Index;
				break;
			}
		}

		return Hash == INDEX_NONE ? Line : Line.Left(Hash);
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

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const int32 Indent = MeasureIndent(Lines[Index]);
		FString Line = StripComment(Lines[Index]).TrimStartAndEnd();

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

		// `variavel Nome : Tipo = valor` -- so' o valor e' aplicado. Criar
		// variavel e' outra coisa, e nao e' isto que a linha esta' pedindo.
		if (Line.StartsWith(TEXT("variavel "), ESearchCase::IgnoreCase))
		{
			Line = Line.RightChop(9).TrimStart();
			int32 Colon = INDEX_NONE;
			int32 Equals = INDEX_NONE;
			Line.FindChar(TEXT(':'), Colon);
			Line.FindChar(TEXT('='), Equals);

			if (Equals == INDEX_NONE)
			{
				// So' declaracao, sem valor: nada a fazer, e nao e' erro.
				continue;
			}
			if (Colon != INDEX_NONE && Colon < Equals)
			{
				Line = Line.Left(Colon).TrimEnd() + TEXT(" = ") + Line.RightChop(Equals + 1).TrimStart();
			}
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
		// Sem isto o asset fica com a mudanca em memoria e limpo em disco: fecha
		// o editor e o trabalho some sem ninguem avisar.
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
