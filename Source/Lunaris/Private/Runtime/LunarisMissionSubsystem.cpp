// Copyright 2026, Lunaris. All Rights Reserved.

#include "Runtime/LunarisMissionSubsystem.h"
#include "Network/LunarisHttpClient.h"
#include "Core/LunarisSettings.h"
#include "Lunaris.h"
#include "JsonObjectConverter.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"

namespace
{
    // Parses { "hash": "..." } from the /active/hash endpoint body. Returns false if the body is
    // not valid JSON or the field is missing. Network layer hands us raw bodies on purpose
    // (per .claude/rules/network.md) — JSON parsing belongs to Runtime.
    bool TryParseHashFromBody(const FString& Body, FString& OutHash)
    {
        TSharedPtr<FJsonObject> Json;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
        if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
        {
            return false;
        }
        return Json->TryGetStringField(TEXT("hash"), OutHash);
    }
}

// =====================================================================================
//  Lifecycle
// =====================================================================================

void ULunarisMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const ULunarisSettings* Settings = GetDefault<ULunarisSettings>();
    if (Settings && Settings->bDesignerMode)
    {
        StartPolling();
    }
}

void ULunarisMissionSubsystem::Deinitialize()
{
    StopPolling();
    Super::Deinitialize();
}

void ULunarisMissionSubsystem::RefreshDesignerMode()
{
    const ULunarisSettings* Settings = GetDefault<ULunarisSettings>();
    if (Settings && Settings->bDesignerMode)
    {
        StartPolling();
    }
    else
    {
        StopPolling();
    }
}

// =====================================================================================
//  Public entry point
// =====================================================================================

void ULunarisMissionSubsystem::LoadAndSpawnMission(const FString& MissionId)
{
    if (MissionId.IsEmpty())
    {
        UE_LOG(LogLunaris, Error, TEXT("LoadAndSpawnMission called with empty MissionId."));
        OnMissionFailed.Broadcast(MissionId, TEXT("MissionId is empty."));
        return;
    }

    // Idempotent: re-triggering the same mission destroys the previous actor before respawning,
    // so designers can hammer the call without leaving orphaned actors in the world.
    if (TWeakObjectPtr<AActor>* ExistingWeak = SpawnedMissions.Find(MissionId))
    {
        if (AActor* Existing = ExistingWeak->Get())
        {
            Existing->Destroy();
        }
        SpawnedMissions.Remove(MissionId);
    }

    UE_LOG(LogLunaris, Log, TEXT("LoadAndSpawnMission: requesting '%s'"), *MissionId);

    FLunarisHttpClient::FetchMissionActive(
        MissionId,
        FOnMissionFetchComplete::CreateUObject(this, &ULunarisMissionSubsystem::HandleMissionFetched, MissionId)
    );
}

// =====================================================================================
//  Initial fetch + spawn pipeline
// =====================================================================================

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

    RequestSpawnFromData(MissionData);
}

void ULunarisMissionSubsystem::RequestSpawnFromData(FLunarisMissionData MissionData)
{
    UE_LOG(LogLunaris, Log, TEXT("Mission '%s' received. Async-loading class '%s'..."),
        *MissionData.MissionId, *MissionData.TargetActor.ClassPath);

    const FSoftObjectPath SoftPath(MissionData.TargetActor.ClassPath);
    const FString LoadKey = MissionData.MissionId;

    TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
    FStreamableDelegate OnLoaded = FStreamableDelegate::CreateLambda(
        [WeakThis, MissionData, LoadKey]()
        {
            if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get())
            {
                TSharedPtr<FStreamableHandle> Handle;
                if (TSharedPtr<FStreamableHandle>* Found = StrongThis->InFlightLoads.Find(LoadKey))
                {
                    Handle = *Found;
                }
                StrongThis->OnTargetClassLoaded(MissionData, Handle);
            }
        }
    );

    TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        SoftPath,
        OnLoaded,
        FStreamableManager::AsyncLoadHighPriority
    );

    if (!Handle.IsValid())
    {
        OnTargetClassLoaded(MissionData, nullptr);
        return;
    }

    InFlightLoads.Add(LoadKey, Handle);
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

    if (!SpawnedActor)
    {
        UE_LOG(LogLunaris, Warning, TEXT("SpawnActor returned null for mission '%s'."), *MissionData.MissionId);
        OnMissionFailed.Broadcast(MissionData.MissionId, TEXT("SpawnActor returned null."));
        return;
    }

    UE_LOG(LogLunaris, Log, TEXT("Spawned '%s' for mission '%s' at (%.0f, %.0f, %.0f)."),
        *LoadedClass->GetName(),
        *MissionData.MissionId,
        MissionData.TargetActor.SpawnLocation.X,
        MissionData.TargetActor.SpawnLocation.Y,
        MissionData.TargetActor.SpawnLocation.Z);

    // Track for reconciliation. The weak ptr lets us detect external destruction.
    SpawnedMissions.Add(MissionData.MissionId, SpawnedActor);
    LastKnownTargets.Add(MissionData.MissionId, MissionData.TargetActor);

    // Seed the hash cache with the current activeHash so the first poll has something to compare
    // against. Without this, a designer publishing a new version within the poll interval window
    // would set the cache to V2 directly and never reconcile from V1 → V2.
    {
        const FString MissionIdCopy = MissionData.MissionId;
        TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
        FLunarisHttpClient::FetchMissionActiveHash(
            MissionIdCopy,
            FOnMissionHashFetchComplete::CreateLambda([WeakThis, MissionIdCopy](bool bSeedSuccess, const FString& Body)
            {
                ULunarisMissionSubsystem* Self = WeakThis.Get();
                if (!Self || !bSeedSuccess) return;
                FString Hash;
                if (TryParseHashFromBody(Body, Hash))
                {
                    Self->LastKnownHashes.Add(MissionIdCopy, Hash);
                }
            })
        );
    }

    OnMissionSpawned.Broadcast(MissionData.MissionId, SpawnedActor);
}

// =====================================================================================
//  Designer Mode poll loop
// =====================================================================================

void ULunarisMissionSubsystem::StartPolling()
{
    if (PollerHandle.IsValid())
    {
        return; // already running
    }

    const ULunarisSettings* Settings = GetDefault<ULunarisSettings>();
    const float Interval = Settings ? Settings->DesignerPollIntervalSeconds : 3.0f;

    TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
    PollerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WeakThis](float DeltaTime) -> bool
        {
            if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get())
            {
                return StrongThis->PollAllMissions(DeltaTime);
            }
            return false; // subsystem gone — auto-remove the ticker
        }),
        Interval
    );

    UE_LOG(LogLunaris, Log, TEXT("Designer Mode polling enabled (interval: %.1fs)."), Interval);
}

void ULunarisMissionSubsystem::StopPolling()
{
    if (PollerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PollerHandle);
        PollerHandle.Reset();
        UE_LOG(LogLunaris, Log, TEXT("Designer Mode polling stopped."));
    }
}

bool ULunarisMissionSubsystem::PollAllMissions(float /*DeltaTime*/)
{
    if (SpawnedMissions.Num() == 0)
    {
        return true; // keep ticker alive — no work this tick, will pick up new spawns later
    }

    // Snapshot keys first; the callbacks may mutate SpawnedMissions/LastKnownHashes.
    TArray<FString> MissionIds;
    SpawnedMissions.GenerateKeyArray(MissionIds);

    TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
    for (const FString& MissionId : MissionIds)
    {
        FLunarisHttpClient::FetchMissionActiveHash(
            MissionId,
            FOnMissionHashFetchComplete::CreateLambda([WeakThis, MissionId](bool bSuccess, const FString& Body)
            {
                if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get())
                {
                    StrongThis->HandleHashPolled(MissionId, bSuccess, Body);
                }
            })
        );
    }

    return true;
}

void ULunarisMissionSubsystem::HandleHashPolled(FString MissionId, bool bSuccess, const FString& BodyOrError)
{
    if (!bSuccess)
    {
        UE_LOG(LogLunaris, Verbose, TEXT("Designer poll: hash fetch failed for '%s' (%s). Will retry next tick."),
            *MissionId, *BodyOrError);
        return;
    }

    FString NewHash;
    if (!TryParseHashFromBody(BodyOrError, NewHash))
    {
        UE_LOG(LogLunaris, Warning, TEXT("Designer poll: malformed hash response for '%s'. Body: %s"), *MissionId, *BodyOrError);
        return;
    }

    FString* CachedHash = LastKnownHashes.Find(MissionId);
    if (!CachedHash)
    {
        // First time we observe this mission's hash (seed-on-spawn was lost or skipped).
        // Record it without reconciling — there's nothing to compare against yet.
        LastKnownHashes.Add(MissionId, NewHash);
        return;
    }

    if (*CachedHash == NewHash)
    {
        return; // no publish since last poll
    }

    UE_LOG(LogLunaris, Log, TEXT("Designer poll: hash changed for '%s' (%s → %s) — fetching new contract."),
        *MissionId, *CachedHash->Left(8), *NewHash.Left(8));

    LastKnownHashes[MissionId] = NewHash;

    TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
    FLunarisHttpClient::FetchMissionActive(
        MissionId,
        FOnMissionFetchComplete::CreateLambda([WeakThis, MissionId](bool bFetchSuccess, const FString& Body)
        {
            if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get())
            {
                StrongThis->HandleReconcileFetched(MissionId, bFetchSuccess, Body);
            }
        })
    );
}

void ULunarisMissionSubsystem::HandleReconcileFetched(FString MissionId, bool bSuccess, const FString& JsonStringOrError)
{
    if (!bSuccess)
    {
        UE_LOG(LogLunaris, Warning, TEXT("Reconcile fetch failed for '%s': %s"), *MissionId, *JsonStringOrError);
        return;
    }

    FLunarisMissionData NewData;
    if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonStringOrError, &NewData, 0, 0))
    {
        UE_LOG(LogLunaris, Error, TEXT("Reconcile: failed to parse mission JSON for '%s'."), *MissionId);
        return;
    }

    if (NewData.TargetActor.ClassPath.IsEmpty())
    {
        UE_LOG(LogLunaris, Warning, TEXT("Reconcile: empty ClassPath in new contract for '%s' — skipping."), *MissionId);
        return;
    }

    // The MissionId in the parsed body should match what we polled, but the URL is the source of
    // truth — bind the parsed data to the polled id to avoid any mismatch.
    NewData.MissionId = MissionId;

    ApplyReconcile(NewData);
}

void ULunarisMissionSubsystem::ApplyReconcile(const FLunarisMissionData& NewData)
{
    TWeakObjectPtr<AActor>* WeakActorPtr = SpawnedMissions.Find(NewData.MissionId);
    AActor* ExistingActor = WeakActorPtr ? WeakActorPtr->Get() : nullptr;
    const FLunarisTargetActor* PrevTarget = LastKnownTargets.Find(NewData.MissionId);

    const FVector OldLocation = PrevTarget ? PrevTarget->SpawnLocation : FVector::ZeroVector;
    const bool bSameClass = PrevTarget && PrevTarget->ClassPath == NewData.TargetActor.ClassPath;

    // Cheap path: same class, actor still alive → just move it. Zero flicker, no async load.
    if (IsValid(ExistingActor) && bSameClass)
    {
        ExistingActor->SetActorLocation(NewData.TargetActor.SpawnLocation);
        LastKnownTargets[NewData.MissionId] = NewData.TargetActor;

        UE_LOG(LogLunaris, Log, TEXT("Reconciled '%s' in place: (%.0f, %.0f, %.0f) → (%.0f, %.0f, %.0f)."),
            *NewData.MissionId,
            OldLocation.X, OldLocation.Y, OldLocation.Z,
            NewData.TargetActor.SpawnLocation.X, NewData.TargetActor.SpawnLocation.Y, NewData.TargetActor.SpawnLocation.Z);

        OnMissionReconciled.Broadcast(NewData.MissionId, ExistingActor, OldLocation, /*bClassReplaced=*/false);
        return;
    }

    // Replace path: class changed, or the previous actor was destroyed externally. Tear down the
    // old actor (if any) and run the standard async-load + spawn pipeline. OnTargetClassLoaded
    // re-populates SpawnedMissions / LastKnownTargets and re-seeds LastKnownHashes.
    if (IsValid(ExistingActor))
    {
        ExistingActor->Destroy();
    }
    SpawnedMissions.Remove(NewData.MissionId);
    LastKnownTargets.Remove(NewData.MissionId);

    UE_LOG(LogLunaris, Log, TEXT("Reconciling '%s' via class replacement (was '%s', now '%s')."),
        *NewData.MissionId,
        PrevTarget ? *PrevTarget->ClassPath : TEXT("<none>"),
        *NewData.TargetActor.ClassPath);

    // ApplyReconcile receives const&; copy into the async pipeline which captures by value.
    FLunarisMissionData DataCopy = NewData;
    RequestSpawnFromData(DataCopy);

    // Fire the event with a null actor — listeners can use the missionId + bClassReplaced=true
    // signal to re-bind any UI references; the new actor will arrive via OnMissionSpawned.
    OnMissionReconciled.Broadcast(NewData.MissionId, /*ReconciledActor=*/nullptr, OldLocation, /*bClassReplaced=*/true);
}
