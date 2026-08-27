#include "NodeScribePropertyText.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Misc/StringOutputDevice.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace NodeScribePropertyText
{

// -- Aspas -------------------------------------------------------------------

bool NeedsQuotes(const FString& Value)
{
	if (Value.IsEmpty())
	{
		return true;
	}

	// Caminho de asset e' lido cru pelo builder e nao tem separador dentro.
	if (Value.StartsWith(TEXT("/")) && !Value.Contains(TEXT(" ")))
	{
		return false;
	}

	if (Value.StartsWith(TEXT("$")))
	{
		return true;
	}

	for (const TCHAR C : Value)
	{
		if (C == TEXT(' ') || C == TEXT(',') || C == TEXT('(') || C == TEXT(')')
			|| C == TEXT('#') || C == TEXT('=') || C == TEXT('"') || C == TEXT('\''))
		{
			return true;
		}
	}

	return Value.Contains(TEXT("//"));
}

bool TryQuote(const FString& Value, FString& OutQuoted)
{
	const bool bHasDouble = Value.Contains(TEXT("\""));
	const bool bHasSingle = Value.Contains(TEXT("'"));

	if (bHasDouble && bHasSingle)
	{
		return false;
	}

	const TCHAR* Quote = bHasDouble ? TEXT("'") : TEXT("\"");
	OutQuoted = Quote + Value + Quote;
	return true;
}

namespace
{
	/** Poe aspas so' quando a linha precisa delas. */
	bool QuoteIfNeeded(const FString& Value, FString& OutText)
	{
		if (!NeedsQuotes(Value))
		{
			OutText = Value;
			return true;
		}
		return TryQuote(Value, OutText);
	}

	/** Tira as aspas que QuoteIfNeeded poe. Sem aspas, devolve como veio. */
	FString Unquote(const FString& Text)
	{
		if (Text.Len() >= 2)
		{
			const TCHAR First = Text[0];
			if ((First == TEXT('"') || First == TEXT('\'')) && Text[Text.Len() - 1] == First)
			{
				return Text.Mid(1, Text.Len() - 2);
			}
		}
		return Text;
	}

	/** O enum como aparece na tela, sem o prefixo do tipo. */
	bool EnumValueToText(const UEnum* Enum, int64 Value, FString& OutText)
	{
		const int32 Index = Enum->GetIndexByValue(Value);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		// GetNameStringByIndex tira o `EPlayerMappableKeySlot::` e deixa `First`.
		OutText = Enum->GetNameStringByIndex(Index);
		return !OutText.IsEmpty();
	}

	bool TextToEnumValue(const UEnum* Enum, const FString& Text, int64& OutValue)
	{
		int64 Value = Enum->GetValueByNameString(Text);
		if (Value == INDEX_NONE)
		{
			// Aceita tambem o nome qualificado, que e' o que o ExportText da Engine
			// produz -- assim texto vindo de fora do plugin continua entrando.
			Value = Enum->GetValueByNameString(Enum->GenerateFullEnumName(*Text));
		}
		if (Value == INDEX_NONE)
		{
			return false;
		}
		OutValue = Value;
		return true;
	}
}

// -- Nomes -------------------------------------------------------------------

FString FormatFloat(double Value)
{
	return FString::SanitizeFloat(Value);
}

FString DescribePinType(const FEdGraphPinType& PinType)
{
	FString Base;

	const FName Category = PinType.PinCategory;
	UObject* SubCategory = PinType.PinSubCategoryObject.Get();

	if (Category == UEdGraphSchema_K2::PC_Boolean)      { Base = TEXT("Boolean"); }
	else if (Category == UEdGraphSchema_K2::PC_Int)     { Base = TEXT("Integer"); }
	else if (Category == UEdGraphSchema_K2::PC_Int64)   { Base = TEXT("Int64"); }
	else if (Category == UEdGraphSchema_K2::PC_Real)    { Base = TEXT("Float"); }
	else if (Category == UEdGraphSchema_K2::PC_String)  { Base = TEXT("String"); }
	else if (Category == UEdGraphSchema_K2::PC_Name)    { Base = TEXT("Name"); }
	else if (Category == UEdGraphSchema_K2::PC_Text)    { Base = TEXT("Text"); }
	else if (Category == UEdGraphSchema_K2::PC_Byte)
	{
		// Enum e byte compartilham categoria; o objeto e' quem distingue.
		Base = SubCategory ? SubCategory->GetName() : TEXT("Byte");
	}
	else if (SubCategory)
	{
		Base = SubCategory->GetName();

		// `BP_Golem_C` e' o nome da classe gerada pela compilacao. Ninguem digita
		// esse sufixo, ele nao aparece em lugar nenhum da interface, e a busca de
		// classe aceita as duas formas -- escrever `BP_Golem_C` na volta so'
		// ensinaria um segundo nome para a mesma coisa.
		Base.RemoveFromEnd(TEXT("_C"));

		// Referencia a classe, nao a instancia. Sem o sufixo, o texto lido volta
		// como referencia a objeto -- uma variavel `BP_Pedra Class` viraria
		// `BP_Pedra`, e o node de Spawn Actor deixaria de aceitar.
		if (Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			Base += TEXT(" Class");
		}
	}
	else
	{
		return FString();
	}

	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		return TEXT("Array de ") + Base;

	case EPinContainerType::Set:
		return TEXT("Conjunto de ") + Base;

	case EPinContainerType::Map:
	{
		// Sem isto um `TMap<int64, BP_Mirror>` saia como `Int64`: some que e'
		// mapa e some o tipo do valor. Quem le' entende "uma variavel int64", e
		// colar essa linha de volta cria exatamente isso -- uma variavel de
		// outro tipo, sem aviso nenhum.
		const FString ValueName = DescribePinType(
			FEdGraphPinType::GetPinTypeForTerminalType(PinType.PinValueType));

		// `?` no lugar do tipo que nao soubemos dizer: o builder recusa a linha e
		// diz qual e'. Melhor que devolver vazio, que sumiria com a variavel toda.
		return FString::Printf(TEXT("Mapa de %s para %s"),
			*Base, ValueName.IsEmpty() ? TEXT("?") : *ValueName);
	}

	default:
		return Base;
	}
}

FString DescribeType(const FProperty* Property)
{
	if (!Property)
	{
		return FString();
	}

	// Passar pelo tipo de pino e' deliberado: garante que a ficha e o grafo
	// chamem o mesmo tipo pelo mesmo nome, sem precisar lembrar de mudar os
	// dois quando um deles mudar.
	//
	// GetDefault, nao construcao na pilha: classe UObject nao pode ser
	// instanciada assim -- o construtor chama FObjectInitializer::Get(), que
	// so' vale dentro de um construtor de UObject, e derruba o editor na hora.
	// Isto ja' derrubou o editor uma vez (cac90d8); compila sem reclamar.
	FEdGraphPinType PinType;
	if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
	{
		return FString();
	}

	return DescribePinType(PinType);
}

FString DisplayName(const FProperty* Property)
{
	if (!Property)
	{
		return FString();
	}

	const FString Display = Property->GetDisplayNameText().ToString();
	return Display.IsEmpty() ? Property->GetName() : Display;
}

// -- Pinos -------------------------------------------------------------------

bool IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

bool IsObjectLikePin(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return false;
	}

	const FName Category = Pin->PinType.PinCategory;
	return Category == UEdGraphSchema_K2::PC_Object
		|| Category == UEdGraphSchema_K2::PC_Class
		|| Category == UEdGraphSchema_K2::PC_SoftObject
		|| Category == UEdGraphSchema_K2::PC_SoftClass
		|| Category == UEdGraphSchema_K2::PC_Interface;
}

// -- Visibilidade ------------------------------------------------------------

bool IsVisible(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
	{
		return false;
	}

	return Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
}

// -- Valor -------------------------------------------------------------------

bool ValueToText(const FProperty* Property, const void* ValuePtr, FString& OutText)
{
	if (!Property || !ValuePtr)
	{
		return false;
	}

	// Bool primeiro: FBoolProperty tambem e' numerica por dentro, e cairia no
	// ramo de numero como `1` em vez de `true`.
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		OutText = BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("true") : TEXT("false");
		return true;
	}

	// Enum antes de numero pela mesma razao: por dentro e' um byte.
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
		const int64 Value = Underlying->GetSignedIntPropertyValue(ValuePtr);
		if (EnumValueToText(EnumProperty->GetEnum(), Value, OutText))
		{
			return true;
		}
		// Valor fora do enum acontece com dado velho. O numero cru volta igual,
		// entao dizer o numero e' melhor que recusar a linha inteira.
		OutText = LexToString(Value);
		return true;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		const int64 Value = ByteProperty->GetSignedIntPropertyValue(ValuePtr);
		if (ByteProperty->Enum && EnumValueToText(ByteProperty->Enum, Value, OutText))
		{
			return true;
		}
		OutText = LexToString(Value);
		return true;
	}

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			OutText = FormatFloat(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
		}
		else
		{
			OutText = LexToString(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		}
		return true;
	}

	if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
	{
		return QuoteIfNeeded(StrProperty->GetPropertyValue(ValuePtr), OutText);
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return QuoteIfNeeded(NameProperty->GetPropertyValue(ValuePtr).ToString(), OutText);
	}

	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return QuoteIfNeeded(TextProperty->GetPropertyValue(ValuePtr).ToString(), OutText);
	}

	// Referencia de asset sai por caminho completo, igual ao grafo. Um nome
	// solto seria mais curto e ambiguo -- duas pastas podem ter `SKM_Golem`, e
	// escolher a errada e' o erro que este plugin recusa cometer.
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		const UObject* Value = ObjectProperty->LoadObjectPropertyValue(ValuePtr);
		OutText = Value ? Value->GetPathName() : TEXT("None");
		return true;
	}

	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr& Value = SoftProperty->GetPropertyValue(ValuePtr);
		OutText = Value.IsNull() ? TEXT("None") : Value.ToString();
		return true;
	}

	// Struct, array, map e set saem inteiros na forma canonica da Engine.
	// Recursar por dentro deixaria mais bonito e abriria uma classe de erro de
	// ida e volta que hoje nao existe; fica para quando a ficha tiver rodagem.
	FString Exported;
	Property->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
	if (Exported.IsEmpty())
	{
		return false;
	}

	return QuoteIfNeeded(Exported, OutText);
}

bool TextToValue(const FProperty* Property, void* ValuePtr, const FString& Text, FString& OutError)
{
	if (!Property || !ValuePtr)
	{
		OutError = TEXT("propriedade ou destino nulo");
		return false;
	}

	const FString Bare = Unquote(Text).TrimStartAndEnd();

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		if (Bare.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| Bare.Equals(TEXT("verdadeiro"), ESearchCase::IgnoreCase))
		{
			BoolProperty->SetPropertyValue(ValuePtr, true);
			return true;
		}
		if (Bare.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Bare.Equals(TEXT("falso"), ESearchCase::IgnoreCase))
		{
			BoolProperty->SetPropertyValue(ValuePtr, false);
			return true;
		}
		OutError = FString::Printf(TEXT("`%s` nao e' true nem false"), *Bare);
		return false;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const UEnum* Enum = EnumProperty->GetEnum();
		int64 Value = 0;
		if (!Enum || !TextToEnumValue(Enum, Bare, Value))
		{
			OutError = FString::Printf(TEXT("`%s` nao e' valor de %s"),
				*Bare, Enum ? *Enum->GetName() : TEXT("enum"));
			return false;
		}
		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (ByteProperty->Enum)
		{
			int64 Value = 0;
			if (TextToEnumValue(ByteProperty->Enum, Bare, Value))
			{
				ByteProperty->SetIntPropertyValue(ValuePtr, Value);
				return true;
			}
		}
		// Sem enum, ou nome que nao bate: cai no numero, tratado abaixo.
	}

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		if (NumericProperty->IsFloatingPoint())
		{
			if (!Bare.IsNumeric())
			{
				OutError = FString::Printf(TEXT("`%s` nao e' numero"), *Bare);
				return false;
			}
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, FCString::Atod(*Bare));
			return true;
		}

		if (!Bare.IsNumeric())
		{
			OutError = FString::Printf(TEXT("`%s` nao e' numero inteiro"), *Bare);
			return false;
		}
		NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FCString::Atoi64(*Bare)));
		return true;
	}

	if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
	{
		StrProperty->SetPropertyValue(ValuePtr, Bare);
		return true;
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		NameProperty->SetPropertyValue(ValuePtr, FName(*Bare));
		return true;
	}

	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		TextProperty->SetPropertyValue(ValuePtr, FText::FromString(Bare));
		return true;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (Bare.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
			return true;
		}

		// So' caminho. Um nome solto que nao seja caminho nao e' aceito -- a
		// mesma regra do grafo, e pela mesma razao: adivinhar qual asset era
		// produz um valor plausivel e errado.
		if (!Bare.StartsWith(TEXT("/")))
		{
			OutError = FString::Printf(
				TEXT("`%s` nao e' caminho de asset; use o caminho completo"), *Bare);
			return false;
		}

		UObject* Loaded = StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *Bare);
		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("nao achei `%s`"), *Bare);
			return false;
		}
		ObjectProperty->SetObjectPropertyValue(ValuePtr, Loaded);
		return true;
	}

	// Struct, array, map e set voltam pela forma canonica da Engine, que e'
	// exatamente o que ValueToText emitiu para eles.
	FStringOutputDevice ImportError;
	const TCHAR* Result = Property->ImportText_Direct(*Bare, ValuePtr, nullptr, PPF_None, &ImportError);
	if (!Result || !ImportError.IsEmpty())
	{
		OutError = ImportError.IsEmpty()
			? FString::Printf(TEXT("`%s` nao entra em %s"), *Bare, *Property->GetName())
			: FString(ImportError).TrimStartAndEnd();
		return false;
	}

	return true;
}

bool DiffersFromDefault(const FProperty* Property, const void* ValuePtr, const void* DefaultPtr)
{
	if (!Property || !ValuePtr)
	{
		return false;
	}

	// Sem arquetipo nao ha' com o que comparar, e dizer "nao mudou" esconderia
	// tudo. Nesse caso toda propriedade e' novidade.
	if (!DefaultPtr)
	{
		return true;
	}

	// Componente proprio nao conta como mudanca.
	//
	// `Mesh` do BP_Golem e `Mesh` do Character apontam para objetos diferentes
	// -- cada classe tem a sua instancia -- entao o ponteiro sempre difere e a
	// comparacao crua marcaria todo componente como alterado. Numa ficha isso
	// e' ruido puro, e ruido nas linhas mais longas: o caminho de um subobjeto
	// e' enorme e nao diz nada. O painel de detalhes tambem nao mostra esses
	// como sobrescritos.
	//
	// Mesmo nome de subobjeto dos dois lados = e' o mesmo componente, visto de
	// duas classes. Trocar o componente por outro muda o nome, e ai' aparece.
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		const UObject* Value = ObjectProperty->LoadObjectPropertyValue(ValuePtr);
		const UObject* Default = ObjectProperty->LoadObjectPropertyValue(DefaultPtr);

		if (Value && Default
			&& Value->IsDefaultSubobject() && Default->IsDefaultSubobject()
			&& Value->GetFName() == Default->GetFName())
		{
			return false;
		}
	}

	return !Property->Identical(ValuePtr, DefaultPtr, PPF_DeepComparison);
}

}
