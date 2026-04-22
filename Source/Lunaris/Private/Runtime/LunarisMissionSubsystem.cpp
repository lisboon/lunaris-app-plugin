// Copyright 2026, Lunaris. All Rights Reserved.

#include "Runtime/LunarisMissionSubsystem.h"
#include "Network/LunarisHttpClient.h"
#include "Lunaris.h"
#include "JsonObjectConverter.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

void ULunarisMissionSubsystem::LoadAndSpawnMission(const FString& MissionId)
{
	FLunarisHttpClient::FetchMissionActive(
		MissionId, 
		FOnMissionFetchComplete::CreateUObject(this, &ULunarisMissionSubsystem::HandleMissionFetched, MissionId)
	);
}

void ULunarisMissionSubsystem::HandleMissionFetched(bool bSuccess, const FString& JsonString, FString MissionId)
{
	if (!IsValid(this)) return;

	if (!bSuccess)
	{
		OnMissionFailed.Broadcast(MissionId, JsonString); 
		return;
	}

	FLunarisMissionData MissionData;
	const bool bParsed = FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &MissionData, 0, 0);

	if (!bParsed)
	{
		UE_LOG(LogLunaris, Error, TEXT("Failed to parse mission JSON. Raw: %s"), *JsonString);
		OnMissionFailed.Broadcast(MissionId, TEXT("Failed to parse mission JSON"));
		return;
	}

	UE_LOG(LogLunaris, Log, TEXT("Mission '%s' received. Async-loading actor..."), *MissionData.MissionId);

	const FSoftClassPath SoftPath(MissionData.TargetActor.ClassPath);
	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SoftPath,
		FStreamableDelegate::CreateUObject(this, &ULunarisMissionSubsystem::OnTargetLoaded, MissionData)
	);
}

void ULunarisMissionSubsystem::OnTargetLoaded(FLunarisMissionData MissionData)
{
	if (!IsValid(this)) return;

	UGameInstance* GI = GetGameInstance();
	if (!IsValid(GI)) return;

	UWorld* World = GI->GetWorld();
	if (!IsValid(World)) return;

	const TSoftClassPtr<AActor> SoftClass(MissionData.TargetActor.ClassPath);
	UClass* LoadedClass = SoftClass.Get();
	if (!LoadedClass)
	{
		UE_LOG(LogLunaris, Error, TEXT("OnTargetLoaded: Failed to resolve class '%s'."), *MissionData.TargetActor.ClassPath);
		return;
	}

	if (!LoadedClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogLunaris, Error, TEXT("OnTargetLoaded: Class '%s' is not an AActor subclass."), *MissionData.TargetActor.ClassPath);
		return;
	}

	AActor* SpawnedActor = World->SpawnActor<AActor>(LoadedClass, MissionData.TargetActor.SpawnLocation, FRotator::ZeroRotator);
	if (SpawnedActor)
	{
		UE_LOG(LogLunaris, Log, TEXT("Actor '%s' spawned for mission '%s'."), *LoadedClass->GetName(), *MissionData.MissionId);
		OnMissionSpawned.Broadcast(MissionData.MissionId, SpawnedActor);
	}
	else
	{
		UE_LOG(LogLunaris, Warning, TEXT("SpawnActor returned null for mission '%s'."), *MissionData.MissionId);
		OnMissionFailed.Broadcast(MissionData.MissionId, TEXT("SpawnActor returned null"));
	}
}