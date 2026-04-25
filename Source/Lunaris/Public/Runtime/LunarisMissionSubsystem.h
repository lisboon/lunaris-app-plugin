// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/StreamableManager.h"
#include "Containers/Ticker.h"
#include "Core/LunarisTypes.h"
#include "LunarisMissionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionSpawned, const FString&, MissionId, AActor*, SpawnedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionFailed,  const FString&, MissionId, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnMissionReconciled,
    const FString&, MissionId,
    AActor*, ReconciledActor,
    const FVector&, OldLocation,
    bool, bClassReplaced);

UCLASS()
class LUNARIS_API ULunarisMissionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionSpawned OnMissionSpawned;

    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionFailed OnMissionFailed;

    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionReconciled OnMissionReconciled;

    UFUNCTION(BlueprintCallable, Category = "Lunaris")
    void LoadAndSpawnMission(const FString& MissionId);

    UFUNCTION(BlueprintCallable, Category = "Lunaris|Designer Mode")
    void RefreshDesignerMode();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    void HandleMissionFetched(bool bSuccess, const FString& JsonStringOrError, FString MissionId);
    void RequestSpawnFromData(FLunarisMissionData MissionData);
    void OnTargetClassLoaded(FLunarisMissionData MissionData, TSharedPtr<FStreamableHandle> Handle);

    void StartPolling();
    void StopPolling();
    bool PollAllMissions(float DeltaTime);
    void HandleHashPolled(FString MissionId, bool bSuccess, const FString& BodyOrError);
    void HandleReconcileFetched(FString MissionId, bool bSuccess, const FString& JsonStringOrError);
    void ApplyReconcile(const FLunarisMissionData& NewData);

    TMap<FString, TSharedPtr<FStreamableHandle>> InFlightLoads;

    TMap<FString, TWeakObjectPtr<AActor>> SpawnedMissions;

    TMap<FString, FLunarisTargetActor> LastKnownTargets;

    TMap<FString, FString> LastKnownHashes;

    FTSTicker::FDelegateHandle PollerHandle;
};
