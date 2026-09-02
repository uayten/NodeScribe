#include "NodeScribeMcpSetup.h"

#include "NodeScribeTypes.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Framework/Commands/UIAction.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeScribe"

FTSTicker::FDelegateHandle FNodeScribeMcpSetup::TickHandle;
FDelegateHandle FNodeScribeMcpSetup::StartupCallbackHandle;

namespace
{
	/** A classe de settings do plugin da Epic, por caminho -- nao por include. */
	const TCHAR* CaminhoDaSettings =
		TEXT("/Script/ModelContextProtocolEngine.ModelContextProtocolSettings");

	const TCHAR* PropAutoStart = TEXT("bAutoStartServer");
	const TCHAR* PropPorta     = TEXT("ServerPortNumber");
	const TCHAR* PropCaminho   = TEXT("ServerUrlPath");

	/**
	 * O nome da entrada no `.mcp.json`.
	 *
	 * E' o servidor da Engine que esta' sendo apontado, nao o NodeScribe -- ele
	 * serve todos os toolsets do projeto, e o nosso e' um deles. Chamar de
	 * "nodescribe" faria parecer que ha' um servidor por plugin.
	 */
	const TCHAR* NomeDaEntrada = TEXT("unreal-mcp");

	/**
	 * Um segundo.
	 *
	 * A ordem importa: o proprio ModelContextProtocol decide se sobe o servidor
	 * no PostEngineInit dele. Se a gente ligasse o `bAutoStartServer` no meio
	 * dessa mesma leva, o resultado dependeria de qual modulo carregou primeiro
	 * -- e nos dois desfechos ruins ou ninguem sobe, ou os dois sobem e um
	 * derruba o outro. Esperar o primeiro quadro tira a duvida: se o servidor
	 * fosse subir sozinho, ja' subiu.
	 */
	const float EsperaSegundos = 1.0f;

	/** A classe de settings, ou nulo se o plugin da Epic nao estiver aqui. */
	UClass* AcharSettings()
	{
		return FindObject<UClass>(nullptr, CaminhoDaSettings);
	}
}

FString FNodeScribeMcpSetup::GetMcpJsonPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json"));
}

void FNodeScribeMcpSetup::Start()
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&FNodeScribeMcpSetup::Conferir), EsperaSegundos);
}

void FNodeScribeMcpSetup::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FNodeScribeMcpSetup::Conferir(float)
{
	UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *GarantirServidor());

	// Sem forcar: na partida, entrada que a pessoa escreveu manda mais que a
	// nossa. Corrigir e' o que o item de menu faz, quando ela pede.
	UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *GarantirEntradaNoMcpJson(/*bForcar*/ false));

	// Uma vez so'.
	return false;
}

bool FNodeScribeMcpSetup::LerEnderecoDoServidor(int32& OutPorta, FString& OutCaminho)
{
	UClass* Classe = AcharSettings();
	if (Classe == nullptr)
	{
		return false;
	}

	const UObject* Cdo = Classe->GetDefaultObject();
	const FNumericProperty* Porta = FindFProperty<FNumericProperty>(Classe, PropPorta);
	const FStrProperty* Caminho = FindFProperty<FStrProperty>(Classe, PropCaminho);

	if (Cdo == nullptr || Porta == nullptr || Caminho == nullptr)
	{
		return false;
	}

	OutPorta = static_cast<int32>(Porta->GetUnsignedIntPropertyValue(Porta->ContainerPtrToValuePtr<void>(Cdo)));
	OutCaminho = Caminho->GetPropertyValue_InContainer(Cdo);

	return true;
}

FString FNodeScribeMcpSetup::GarantirServidor()
{
	UClass* Classe = AcharSettings();
	if (Classe == nullptr)
	{
		// Nao e' erro: a dependencia e' opcional, e todo o resto do plugin --
		// botoes, Message Log, comando.txt -- funciona sem ela.
		return TEXT("o plugin ModelContextProtocol nao esta neste projeto, entao nao ha servidor para ligar.");
	}

	UObject* Cdo = Classe->GetDefaultObject();
	const FBoolProperty* AutoStart = FindFProperty<FBoolProperty>(Classe, PropAutoStart);

	if (Cdo == nullptr || AutoStart == nullptr)
	{
		return FString::Printf(
			TEXT("[erro]: achei a settings do MCP mas nao a propriedade `%s`. A Engine renomeou, e o auto-start ")
			TEXT("tem que voltar a ser ligado na mao em Project Settings -> Plugins -> Model Context Protocol."),
			PropAutoStart);
	}

	if (AutoStart->GetPropertyValue_InContainer(Cdo))
	{
		return TEXT("servidor ja estava configurado para subir sozinho.");
	}

	AutoStart->SetPropertyValue_InContainer(Cdo, true);

	// SaveConfig, e nao TryUpdateDefaultConfigFile: isto grava no ini por
	// usuario, dentro de Saved/. Escrever no `Config/` do projeto sujaria um
	// arquivo versionado sem ninguem ter pedido, e nao e' preciso -- se o valor
	// sumir, a proxima abertura passa por aqui de novo e liga outra vez.
	Cdo->SaveConfig();

	// Ligar a settings so' vale para a proxima abertura: o momento em que o
	// plugin da Epic olha para ela ja' passou. Para esta sessao, o comando de
	// console -- que so' existe se o plugin estiver carregado, e some junto com
	// ele se nao estiver.
	bool bSubiu = false;
	if (GEngine != nullptr)
	{
		bSubiu = GEngine->Exec(nullptr, TEXT("ModelContextProtocol.StartServer"));
	}

	int32 Porta = 0;
	FString Caminho;
	const bool bTemEndereco = LerEnderecoDoServidor(Porta, Caminho);

	if (!bSubiu)
	{
		return TEXT("liguei o auto-start para as proximas aberturas, mas o comando ")
			TEXT("`ModelContextProtocol.StartServer` nao respondeu -- nesta sessao o servidor continua fora do ar.");
	}

	return bTemEndereco
		? FString::Printf(TEXT("auto-start estava desligado; liguei e subi o servidor em http://127.0.0.1:%d%s"), Porta, *Caminho)
		: TEXT("auto-start estava desligado; liguei e subi o servidor.");
}

FString FNodeScribeMcpSetup::GarantirEntradaNoMcpJson(bool bForcar)
{
	int32 Porta = 0;
	FString CaminhoDaUrl;
	if (!LerEnderecoDoServidor(Porta, CaminhoDaUrl))
	{
		return TEXT("sem o ModelContextProtocol nao ha endereco para escrever no .mcp.json.");
	}

	const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d%s"), Porta, *CaminhoDaUrl);
	const FString Arquivo = GetMcpJsonPath();

	TSharedPtr<FJsonObject> Raiz;
	FString Texto;

	if (FFileHelper::LoadFileToString(Texto, *Arquivo))
	{
		const TSharedRef<TJsonReader<>> Leitor = TJsonReaderFactory<>::Create(Texto);
		if (!FJsonSerializer::Deserialize(Leitor, Raiz) || !Raiz.IsValid())
		{
			// Reescrever por cima apagaria a configuracao de outros servidores
			// -- e o arquivo pode estar quebrado justamente por estar sendo
			// editado agora.
			return FString::Printf(TEXT("[erro]: %s existe mas nao e JSON valido. Nao mexo nele."), *Arquivo);
		}
	}
	else
	{
		Raiz = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Servidores;
	if (Raiz->HasTypedField<EJson::Object>(TEXT("mcpServers")))
	{
		Servidores = Raiz->GetObjectField(TEXT("mcpServers"));
	}
	else
	{
		Servidores = MakeShared<FJsonObject>();
		Raiz->SetObjectField(TEXT("mcpServers"), Servidores);
	}

	// Qualquer entrada que ja' aponte para este endereco serve, tenha o nome que
	// tiver: quem configurou na mao escolheu o nome dela, e um segundo apelido
	// para o mesmo servidor faria o assistente listar tudo em dobro.
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Par : Servidores->Values)
	{
		const TSharedPtr<FJsonObject>* Entrada = nullptr;
		FString UrlDaEntrada;

		if (Par.Value.IsValid()
			&& Par.Value->TryGetObject(Entrada)
			&& (*Entrada)->TryGetStringField(TEXT("url"), UrlDaEntrada)
			&& UrlDaEntrada.Equals(Url, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT(".mcp.json ja aponta para %s, como \"%s\"."), *Url, *Par.Key);
		}
	}

	if (Servidores->HasField(NomeDaEntrada) && !bForcar)
	{
		return FString::Printf(
			TEXT(".mcp.json ja tem uma entrada \"%s\", apontando para outro lugar. Deixei como esta -- ")
			TEXT("pode ser um tunel ou outro editor. Tools -> NodeScribe -> Configurar MCP do projeto corrige para %s."),
			NomeDaEntrada, *Url);
	}

	const TSharedPtr<FJsonObject> Nova = MakeShared<FJsonObject>();
	Nova->SetStringField(TEXT("type"), TEXT("http"));
	Nova->SetStringField(TEXT("url"), Url);
	Servidores->SetObjectField(NomeDaEntrada, Nova);

	FString Saida;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Escritor =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Saida);

	if (!FJsonSerializer::Serialize(Raiz.ToSharedRef(), Escritor)
		|| !FFileHelper::SaveStringToFile(Saida, *Arquivo))
	{
		return FString::Printf(TEXT("[erro]: nao consegui gravar %s."), *Arquivo);
	}

	return FString::Printf(TEXT("escrevi a entrada \"%s\" -> %s em %s."), NomeDaEntrada, *Url, *Arquivo);
}

void FNodeScribeMcpSetup::RegisterStartupHook()
{
	StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FNodeScribeMcpSetup::RegisterMenu));
}

void FNodeScribeMcpSetup::RegisterMenu()
{
	FToolMenuOwnerScoped Dono(FName("NodeScribeMcpSetup"));

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(FName("LevelEditor.MainMenu.Tools"));
	if (Menu == nullptr)
	{
		return;
	}

	FToolMenuSection& Secao = Menu->FindOrAddSection(
		"NodeScribe", LOCTEXT("SecaoNodeScribe", "NodeScribe"));

	Secao.AddMenuEntry(
		"NodeScribeConfigurarMcp",
		LOCTEXT("ConfigurarMcp", "Configurar MCP do projeto"),
		LOCTEXT("ConfigurarMcpTip",
			"Liga o servidor MCP da Engine e poe o endereco dele no .mcp.json da raiz do projeto, que e onde o "
			"Claude Code procura.\n\n"
			"Isto ja roda sozinho a cada abertura. O item existe para o caso de a porta ter mudado, ou de voce "
			"ter uma entrada antiga apontando para o lugar errado -- so daqui ela e corrigida."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			const FString DoServidor = GarantirServidor();
			const FString DoArquivo = GarantirEntradaNoMcpJson(/*bForcar*/ true);

			UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *DoServidor);
			UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *DoArquivo);

			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				LOCTEXT("ResultadoMcp", "Servidor: {0}\n\nArquivo: {1}\n\nSe o assistente ja estava aberto, reconecte-o para ele ver a mudanca."),
				FText::FromString(DoServidor),
				FText::FromString(DoArquivo)));
		})));
}

void FNodeScribeMcpSetup::Unregister()
{
	if (StartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
		StartupCallbackHandle.Reset();
	}

	if (UObjectInitialized())
	{
		UToolMenus::UnregisterOwner(FName("NodeScribeMcpSetup"));
	}
}

#undef LOCTEXT_NAMESPACE
