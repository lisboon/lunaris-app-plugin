// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

LUNARIS_API DECLARE_LOG_CATEGORY_EXTERN(LogLunaris, Log, All);

class FLunarisModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};