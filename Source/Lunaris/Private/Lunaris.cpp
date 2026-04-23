// Copyright 2026, Lunaris. All Rights Reserved.

#include "Lunaris.h"

#define LOCTEXT_NAMESPACE "FLunarisModule"

DEFINE_LOG_CATEGORY(LogLunaris);

void FLunarisModule::StartupModule()
{
    UE_LOG(LogLunaris, Log, TEXT("Lunaris module started."));
}

void FLunarisModule::ShutdownModule()
{
    UE_LOG(LogLunaris, Log, TEXT("Lunaris module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLunarisModule, Lunaris)