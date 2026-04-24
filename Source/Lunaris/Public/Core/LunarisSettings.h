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

	UPROPERTY(Config, EditAnywhere, Category = "Designer Mode", meta = (Tooltip = "When true, the MissionSubsystem polls the backend watch endpoint and live-reconciles spawned missions whenever a new version is published. Intended for editor/dev builds only — MUST be false in shipped builds."))
	bool bDesignerMode = false;

	UPROPERTY(Config, EditAnywhere, Category = "Designer Mode", meta = (ClampMin = "0.5", ClampMax = "60.0", UIMin = "0.5", UIMax = "15.0", EditCondition = "bDesignerMode", Units = "Seconds", Tooltip = "Interval between hash polls while Designer Mode is active. Lower = faster preview, higher = less HTTP traffic. Ignored when Designer Mode is off."))
	float DesignerPollIntervalSeconds = 3.0f;
};