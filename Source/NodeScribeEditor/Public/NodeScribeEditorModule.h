#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FNodeScribeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Nome da aba, tambem usado para abrir a janela por codigo. */
	static const FName TabName;
};
