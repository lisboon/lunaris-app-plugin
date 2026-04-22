// Copyright Epic Games, Inc. All Rights Reserved.

#include "Lunaris.h"

#define LOCTEXT_NAMESPACE "FLunarisModule"

void FLunarisModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FLunarisModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLunarisModule, Lunaris)