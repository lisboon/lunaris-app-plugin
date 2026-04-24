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

    // Fires whenever Designer Mode detects a published version change and updates a tracked mission.
    // bClassReplaced = true means the actor was destroyed and respawned (target classPath changed);
    // false means SetActorLocation was used in place (same class, new location).
    UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
    FOnMissionReconciled OnMissionReconciled;

    // Fetch a mission contract from the backend and spawn its target actor. Idempotent per
    // MissionId — calling it again with the same id destroys the previously spawned actor
    // (if any) before respawning, so the caller can re-trigger without duplicating actors.
    UFUNCTION(BlueprintCallable, Category = "Lunaris")
    void LoadAndSpawnMission(const FString& MissionId);

    // Re-reads ULunarisSettings and starts/stops the Designer Mode poll loop accordingly.
    // Useful when the user toggles bDesignerMode at runtime — call from Blueprint after the change.
    // Otherwise the setting is sampled once at Initialize.
    UFUNCTION(BlueprintCallable, Category = "Lunaris|Designer Mode")
    void RefreshDesignerMode();

    // UGameInstanceSubsystem overrides
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    // ---- Initial fetch + spawn pipeline ----
    void HandleMissionFetched(bool bSuccess, const FString& JsonStringOrError, FString MissionId);
    void RequestSpawnFromData(FLunarisMissionData MissionData);
    void OnTargetClassLoaded(FLunarisMissionData MissionData, TSharedPtr<FStreamableHandle> Handle);

    // ---- Designer Mode poll loop ----
    void StartPolling();
    void StopPolling();
    bool PollAllMissions(float DeltaTime);
    void HandleHashPolled(FString MissionId, bool bSuccess, const FString& BodyOrError);
    void HandleReconcileFetched(FString MissionId, bool bSuccess, const FString& JsonStringOrError);
    void ApplyReconcile(const FLunarisMissionData& NewData);

    // ---- Tracking state ----
    // Active streamable loads, keyed by MissionId. Holding the TSharedPtr keeps the loaded asset alive
    // until OnTargetClassLoaded consumes it.
    TMap<FString, TSharedPtr<FStreamableHandle>> InFlightLoads;

    // Spawned actor per MissionId. WeakObjectPtr lets us detect external destruction without holding a UPROPERTY.
    TMap<FString, TWeakObjectPtr<AActor>> SpawnedMissions;

    // Last contract data observed for each tracked mission. Used by ApplyReconcile to decide
    // SetActorLocation vs destroy+respawn (compares classPath). Always seeded after a successful spawn.
    TMap<FString, FLunarisTargetActor> LastKnownTargets;

    // Last activeHash per tracked mission. Compared against new poll responses to detect publishes.
    TMap<FString, FString> LastKnownHashes;

    // FTSTicker registration. Re-registered on Designer Mode toggle. NEVER use FTickableGameObject
    // here — Tom Looman's UE5 material warns against per-frame work for cadence-based tasks; the
    // ticker fires only on the configured interval.
    FTSTicker::FDelegateHandle PollerHandle;
};
