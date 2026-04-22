// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LunarisSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Lunaris"))
class LUNARIS_API ULunarisSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (Tooltip = "Base URL of the Lunaris NestJS backend. No trailing slash."))
	FString BackendUrl = TEXT("http://localhost:3001");

	UPROPERTY(Config, EditAnywhere, Category = "Backend", meta = (Tooltip = "M2M API key from the Lunaris web editor. Keep this secret — do not commit to source control."))
	FString EngineApiKey;
};