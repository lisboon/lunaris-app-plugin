// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/StreamableManager.h"
#include "Core/LunarisTypes.h"
#include "LunarisMissionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionSpawned, const FString&, MissionId, AActor*, SpawnedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionFailed,  const FString&, MissionId, const FString&, ErrorMessage);

UCLASS()
class LUNARIS_API ULunarisMissionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionSpawned OnMissionSpawned;

    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionFailed OnMissionFailed;

    UFUNCTION(BlueprintCallable, Category = "Lunaris")
    void LoadAndSpawnMission(const FString& MissionId);

private:
    void HandleMissionFetched(bool bSuccess, const FString& JsonStringOrError, FString MissionId);
    void OnTargetClassLoaded(FLunarisMissionData MissionData, TSharedPtr<FStreamableHandle> Handle);

    TMap<FString, TSharedPtr<FStreamableHandle>> InFlightLoads;
};