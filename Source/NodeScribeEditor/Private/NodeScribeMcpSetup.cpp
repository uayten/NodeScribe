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
	/** Epic's plugin settings class, by path -- not by include. */
	const TCHAR* SettingsClassPath =
		TEXT("/Script/ModelContextProtocolEngine.ModelContextProtocolSettings");

	const TCHAR* PropAutoStart = TEXT("bAutoStartServer");
	const TCHAR* PropPort      = TEXT("ServerPortNumber");
	const TCHAR* PropPath      = TEXT("ServerUrlPath");

	/**
	 * The entry's name in `.mcp.json`.
	 *
	 * It is the Engine's server being pointed at, not NodeScribe -- it serves
	 * every toolset in the project, and ours is one of them. Calling it
	 * "nodescribe" would make it look like there is one server per plugin.
	 */
	const TCHAR* EntryName = TEXT("unreal-mcp");

	/**
	 * One second.
	 *
	 * The order matters: ModelContextProtocol itself decides whether to start
	 * the server in its own PostEngineInit. If we turned `bAutoStartServer` on in
	 * the middle of that same batch, the result would depend on which module
	 * loaded first -- and in both bad outcomes either nobody starts it, or both
	 * do and one brings the other down. Waiting for the first frame settles it:
	 * if the server was going to start by itself, it already has.
	 */
	const float WaitSeconds = 1.0f;

	/** The settings class, or null if Epic's plugin is not here. */
	UClass* FindSettingsClass()
	{
		return FindObject<UClass>(nullptr, SettingsClassPath);
	}
}

FString FNodeScribeMcpSetup::GetMcpJsonPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json"));
}

void FNodeScribeMcpSetup::Start()
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&FNodeScribeMcpSetup::Check), WaitSeconds);
}

void FNodeScribeMcpSetup::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FNodeScribeMcpSetup::Check(float)
{
	UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *EnsureServer());

	// Without forcing: at startup, an entry the person wrote outranks ours.
	// Fixing it is what the menu item does, when they ask.
	UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *EnsureMcpJsonEntry(/*bForce*/ false));

	// Only once.
	return false;
}

bool FNodeScribeMcpSetup::ReadServerAddress(int32& OutPort, FString& OutPath)
{
	UClass* SettingsClass = FindSettingsClass();
	if (SettingsClass == nullptr)
	{
		return false;
	}

	const UObject* Cdo = SettingsClass->GetDefaultObject();
	const FNumericProperty* Port = FindFProperty<FNumericProperty>(SettingsClass, PropPort);
	const FStrProperty* Path = FindFProperty<FStrProperty>(SettingsClass, PropPath);

	if (Cdo == nullptr || Port == nullptr || Path == nullptr)
	{
		return false;
	}

	OutPort = static_cast<int32>(Port->GetUnsignedIntPropertyValue(Port->ContainerPtrToValuePtr<void>(Cdo)));
	OutPath = Path->GetPropertyValue_InContainer(Cdo);

	return true;
}

FString FNodeScribeMcpSetup::EnsureServer()
{
	UClass* SettingsClass = FindSettingsClass();
	if (SettingsClass == nullptr)
	{
		// Not an error: the dependency is optional, and all the rest of the
		// plugin -- buttons, Message Log, command.txt -- works without it.
		return TEXT("the ModelContextProtocol plugin is not in this project, so there is no server to start.");
	}

	UObject* Cdo = SettingsClass->GetDefaultObject();
	const FBoolProperty* AutoStart = FindFProperty<FBoolProperty>(SettingsClass, PropAutoStart);

	if (Cdo == nullptr || AutoStart == nullptr)
	{
		return FString::Printf(
			TEXT("[error]: found the MCP settings but not the `%s` property. The Engine renamed it, and auto-start ")
			TEXT("has to be turned back on by hand in Project Settings -> Plugins -> Model Context Protocol."),
			PropAutoStart);
	}

	if (AutoStart->GetPropertyValue_InContainer(Cdo))
	{
		return TEXT("the server was already set to start by itself.");
	}

	AutoStart->SetPropertyValue_InContainer(Cdo, true);

	// SaveConfig, not TryUpdateDefaultConfigFile: this writes to the per-user
	// ini, inside Saved/. Writing to the project's `Config/` would dirty a
	// versioned file nobody asked for, and it is not needed -- if the value
	// vanishes, the next launch comes through here again and turns it back on.
	Cdo->SaveConfig();

	// Turning the setting on only counts for the next launch: the moment Epic's
	// plugin looks at it has already passed. For this session, the console
	// command -- which only exists if the plugin is loaded, and goes away with
	// it if it is not.
	bool bStarted = false;
	if (GEngine != nullptr)
	{
		bStarted = GEngine->Exec(nullptr, TEXT("ModelContextProtocol.StartServer"));
	}

	int32 Port = 0;
	FString Path;
	const bool bHasAddress = ReadServerAddress(Port, Path);

	if (!bStarted)
	{
		return TEXT("turned auto-start on for the next launches, but the ")
			TEXT("`ModelContextProtocol.StartServer` command did not answer -- in this session the server stays down.");
	}

	return bHasAddress
		? FString::Printf(TEXT("auto-start was off; turned it on and started the server at http://127.0.0.1:%d%s"), Port, *Path)
		: TEXT("auto-start was off; turned it on and started the server.");
}

FString FNodeScribeMcpSetup::EnsureMcpJsonEntry(bool bForce)
{
	int32 Port = 0;
	FString UrlPath;
	if (!ReadServerAddress(Port, UrlPath))
	{
		return TEXT("without ModelContextProtocol there is no address to write into .mcp.json.");
	}

	const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d%s"), Port, *UrlPath);
	const FString FilePath = GetMcpJsonPath();

	TSharedPtr<FJsonObject> Root;
	FString Text;

	if (FFileHelper::LoadFileToString(Text, *FilePath))
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			// Writing over it would erase the configuration of other servers --
			// and the file may be broken precisely because it is being edited
			// right now.
			return FString::Printf(TEXT("[error]: %s exists but is not valid JSON. Leaving it alone."), *FilePath);
		}
	}
	else
	{
		Root = MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> Servers;
	if (Root->HasTypedField<EJson::Object>(TEXT("mcpServers")))
	{
		Servers = Root->GetObjectField(TEXT("mcpServers"));
	}
	else
	{
		Servers = MakeShared<FJsonObject>();
		Root->SetObjectField(TEXT("mcpServers"), Servers);
	}

	// Any entry already pointing at this address will do, whatever its name:
	// whoever configured it by hand chose its name, and a second alias for the
	// same server would make the assistant list everything twice.
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Servers->Values)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		FString EntryUrl;

		if (Pair.Value.IsValid()
			&& Pair.Value->TryGetObject(Entry)
			&& (*Entry)->TryGetStringField(TEXT("url"), EntryUrl)
			&& EntryUrl.Equals(Url, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT(".mcp.json already points at %s, as \"%s\"."), *Url, *Pair.Key);
		}
	}

	if (Servers->HasField(EntryName) && !bForce)
	{
		return FString::Printf(
			TEXT(".mcp.json already has an entry \"%s\", pointing somewhere else. Left it as it is -- ")
			TEXT("it may be a tunnel or another editor. Tools -> NodeScribe -> Configure project MCP fixes it to %s."),
			EntryName, *Url);
	}

	const TSharedPtr<FJsonObject> NewEntry = MakeShared<FJsonObject>();
	NewEntry->SetStringField(TEXT("type"), TEXT("http"));
	NewEntry->SetStringField(TEXT("url"), Url);
	Servers->SetObjectField(EntryName, NewEntry);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer)
		|| !FFileHelper::SaveStringToFile(Output, *FilePath))
	{
		return FString::Printf(TEXT("[error]: could not write %s."), *FilePath);
	}

	return FString::Printf(TEXT("wrote the entry \"%s\" -> %s in %s."), EntryName, *Url, *FilePath);
}

void FNodeScribeMcpSetup::RegisterStartupHook()
{
	StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FNodeScribeMcpSetup::RegisterMenu));
}

void FNodeScribeMcpSetup::RegisterMenu()
{
	FToolMenuOwnerScoped Owner(FName("NodeScribeMcpSetup"));

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(FName("LevelEditor.MainMenu.Tools"));
	if (Menu == nullptr)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(
		"NodeScribe", LOCTEXT("NodeScribeSection", "NodeScribe"));

	Section.AddMenuEntry(
		"NodeScribeConfigureMcp",
		LOCTEXT("ConfigureMcp", "Configure project MCP"),
		LOCTEXT("ConfigureMcpTip",
			"Turns on the Engine's MCP server and puts its address into the .mcp.json at the project root, which is "
			"where Claude Code looks.\n\n"
			"This already runs by itself on every launch. The item exists for when the port changed, or you "
			"have an old entry pointing at the wrong place -- only from here does it get fixed."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			const FString FromServer = EnsureServer();
			const FString FromFile = EnsureMcpJsonEntry(/*bForce*/ true);

			UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *FromServer);
			UE_LOG(LogNodeScribe, Log, TEXT("MCP: %s"), *FromFile);

			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				LOCTEXT("McpResult", "Server: {0}\n\nFile: {1}\n\nIf the assistant was already open, reconnect it so it sees the change."),
				FText::FromString(FromServer),
				FText::FromString(FromFile)));
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
