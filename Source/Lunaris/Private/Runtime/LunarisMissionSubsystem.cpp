// Copyright 2026, Lunaris. All Rights Reserved.

#include "Runtime/LunarisMissionSubsystem.h"
#include "Network/LunarisHttpClient.h"
#include "Lunaris.h"
#include "JsonObjectConverter.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void ULunarisMissionSubsystem::LoadAndSpawnMission(const FString& MissionId)
{
    if (MissionId.IsEmpty())
    {
        UE_LOG(LogLunaris, Error, TEXT("LoadAndSpawnMission called with empty MissionId."));
        OnMissionFailed.Broadcast(MissionId, TEXT("MissionId is empty."));
        return;
    }

    UE_LOG(LogLunaris, Log, TEXT("LoadAndSpawnMission: requesting '%s'"), *MissionId);

    FLunarisHttpClient::FetchMissionActive(
        MissionId,
        FOnMissionFetchComplete::CreateUObject(this, &ULunarisMissionSubsystem::HandleMissionFetched, MissionId)
    );
}

void ULunarisMissionSubsystem::HandleMissionFetched(bool bSuccess, const FString& JsonStringOrError, FString MissionId)
{
    if (!bSuccess)
    {
        OnMissionFailed.Broadcast(MissionId, JsonStringOrError);
        return;
    }

    FLunarisMissionData MissionData;
    const bool bParsed = FJsonObjectConverter::JsonObjectStringToUStruct(JsonStringOrError, &MissionData, 0, 0);

    if (!bParsed)
    {
        UE_LOG(LogLunaris, Error, TEXT("Failed to parse mission JSON for '%s'. Raw: %s"), *MissionId, *JsonStringOrError);
        OnMissionFailed.Broadcast(MissionId, TEXT("Failed to parse mission JSON."));
        return;
    }

    if (MissionData.TargetActor.ClassPath.IsEmpty())
    {
        OnMissionFailed.Broadcast(MissionId, TEXT("TargetActor.ClassPath is empty."));
        return;
    }

    UE_LOG(LogLunaris, Log, TEXT("Mission '%s' received. Async-loading class '%s'..."),
        *MissionData.MissionId, *MissionData.TargetActor.ClassPath);

    const FSoftObjectPath SoftPath(MissionData.TargetActor.ClassPath);

    TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        SoftPath,
        FStreamableDelegate(),
        FStreamableManager::AsyncLoadHighPriority
    );

    if (!Handle.IsValid())
    {
        OnTargetClassLoaded(MissionData, nullptr);
        return;
    }

    InFlightLoads.Add(MissionId, Handle);

    TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
    Handle->BindCompleteDelegate(FStreamableDelegate::CreateLambda(
        [WeakThis, MissionData, Handle]()
        {
            if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get())
            {
                StrongThis->OnTargetClassLoaded(MissionData, Handle);
            }
        }
    ));
}

void ULunarisMissionSubsystem::OnTargetClassLoaded(FLunarisMissionData MissionData, TSharedPtr<FStreamableHandle> Handle)
{
    InFlightLoads.Remove(MissionData.MissionId);

    UClass* LoadedClass = nullptr;

    if (Handle.IsValid())
    {
        LoadedClass = Cast<UClass>(Handle->GetLoadedAsset());
    }

    if (!LoadedClass)
    {
        const FSoftObjectPath SoftPath(MissionData.TargetActor.ClassPath);
        LoadedClass = Cast<UClass>(SoftPath.ResolveObject());
    }

    if (!LoadedClass)
    {
        UE_LOG(LogLunaris, Error, TEXT("OnTargetClassLoaded: failed to resolve UClass for '%s'."), *MissionData.TargetActor.ClassPath);
        OnMissionFailed.Broadcast(MissionData.MissionId, FString::Printf(TEXT("Failed to resolve class '%s'."), *MissionData.TargetActor.ClassPath));
        return;
    }

    if (!LoadedClass->IsChildOf(AActor::StaticClass()))
    {
        UE_LOG(LogLunaris, Error, TEXT("OnTargetClassLoaded: class '%s' is not an AActor subclass."), *MissionData.TargetActor.ClassPath);
        OnMissionFailed.Broadcast(MissionData.MissionId, FString::Printf(TEXT("Class '%s' is not an AActor."), *MissionData.TargetActor.ClassPath));
        return;
    }

    UGameInstance* GI = GetGameInstance();
    UWorld* World = GI ? GI->GetWorld() : nullptr;
    if (!IsValid(World))
    {
        OnMissionFailed.Broadcast(MissionData.MissionId, TEXT("No valid UWorld to spawn into."));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AActor* SpawnedActor = World->SpawnActor<AActor>(
        LoadedClass,
        MissionData.TargetActor.SpawnLocation,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (SpawnedActor)
    {
        UE_LOG(LogLunaris, Log, TEXT("Spawned '%s' for mission '%s' at (%.0f, %.0f, %.0f)."),
            *LoadedClass->GetName(),
            *MissionData.MissionId,
            MissionData.TargetActor.SpawnLocation.X,
            MissionData.TargetActor.SpawnLocation.Y,
            MissionData.TargetActor.SpawnLocation.Z);
        OnMissionSpawned.Broadcast(MissionData.MissionId, SpawnedActor);
    }
    else
    {
        UE_LOG(LogLunaris, Warning, TEXT("SpawnActor returned null for mission '%s'."), *MissionData.MissionId);
        OnMissionFailed.Broadcast(MissionData.MissionId, TEXT("SpawnActor returned null."));
    }
}
