---
name: runtime
description: Runtime layer of the Lunaris plugin — UGameInstanceSubsystem that orchestrates mission fetch, async asset loading, actor spawning, and Designer-Mode live reconciliation. The only layer that touches UWorld, AActor, and FStreamableManager.
user-invocable: true
argument-hint: ""
---

# Runtime Layer

**Purpose:** the Runtime layer is where the plugin **becomes a game**. It owns `ULunarisMissionSubsystem` — a `UGameInstanceSubsystem` that consumes raw JSON from the Network layer, parses it into Core USTRUCTs, asynchronously loads target classes, spawns actors, and (in Designer Mode) live-reconciles them as the backend publishes new versions.

**Position in the dependency graph:** `Runtime → Network → Core`. Runtime is the only layer allowed to include `Engine/World.h`, `Engine/AssetManager.h`, `Engine/StreamableManager.h`, `Containers/Ticker.h`, or `JsonObjectConverter.h`. Network and Core must never depend on Runtime.

---

## Files

```
Source/Lunaris/
├── Public/Runtime/
│   └── LunarisMissionSubsystem.h    ← UGameInstanceSubsystem + delegates + tracking maps
└── Private/Runtime/
    └── LunarisMissionSubsystem.cpp  ← lifecycle, fetch pipeline, poll loop, reconcile
```

A single subsystem covers the MVP. Future runtime concerns (spawning multiple actors per mission, parameter binding from `data: Record<string, unknown>` on graph nodes, condition evaluation) will likely grow new files in this layer — keep them in `Runtime/` so the layering stays clean.

---

## Public API

```cpp
UCLASS()
class LUNARIS_API ULunarisMissionSubsystem : public UGameInstanceSubsystem
{
    UFUNCTION(BlueprintCallable, Category = "Lunaris")
    void LoadAndSpawnMission(const FString& MissionId);

    UFUNCTION(BlueprintCallable, Category = "Lunaris|Designer Mode")
    void RefreshDesignerMode();
};
```

| Method | Purpose |
|---|---|
| `LoadAndSpawnMission(MissionId)` | Idempotent. Fetches the latest published contract from the backend, async-loads the target class, spawns the actor, and (if Designer Mode is on) starts tracking it for reconciliation. Calling it again with the same id destroys the previously spawned actor before respawning — designers can hammer the call without leaving orphaned actors. |
| `RefreshDesignerMode()` | Re-reads `ULunarisSettings::bDesignerMode` and starts/stops the poll loop accordingly. Use this after toggling the setting at runtime; otherwise the setting is sampled once at `Initialize`. |

Both are `UFUNCTION(BlueprintCallable)` — designers wire them from Level Blueprint (`BeginPlay`, trigger overlap, input event) without touching C++.

---

## Blueprint Events

```cpp
UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
FOnMissionSpawned OnMissionSpawned;       // (MissionId, SpawnedActor)

UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
FOnMissionFailed OnMissionFailed;         // (MissionId, ErrorMessage)

UPROPERTY(BlueprintAssignable, Category = "Lunaris|Events")
FOnMissionReconciled OnMissionReconciled; // (MissionId, ReconciledActor, OldLocation, bClassReplaced)
```

| Event | Fires when |
|---|---|
| `OnMissionSpawned` | Initial spawn after `LoadAndSpawnMission` succeeds. Also fires after a class-replacement reconcile (the new actor goes through the same async-load + spawn pipeline). |
| `OnMissionFailed` | Any terminal failure: HTTP error, JSON parse failure, missing or non-AActor class, missing world. Carries a human-readable error string suitable for HUD overlay. Does **not** fire for transient poll failures (those are `Verbose` logs only — they retry on the next tick). |
| `OnMissionReconciled` | Designer-Mode detected a publish and applied the change. `ReconciledActor` is the same actor when the move was in-place (`bClassReplaced=false`), or `nullptr` when the class changed and a fresh spawn is on the way (the new actor arrives via `OnMissionSpawned` after the async load completes). `OldLocation` is the position the actor occupied before reconcile, useful for UI animations or debug HUD. |

All three are `DECLARE_DYNAMIC_MULTICAST_DELEGATE_*` so multiple Blueprints can bind. Names start with `On` per the project convention.

---

## Lifecycle

```cpp
void Initialize(FSubsystemCollectionBase&) override;  // sample bDesignerMode → maybe StartPolling
void Deinitialize() override;                          // unconditional StopPolling
```

`UGameInstanceSubsystem` semantics: created once when the GameInstance starts, lives across level transitions, destroyed on app shutdown. The poll loop survives map changes — exactly what we want, because tracked missions also survive (the `SpawnedMissions` map carries weak ptrs that simply go invalid when the world unloads, and reconcile naturally respawns into the new world).

`Initialize` reads `ULunarisSettings` once. To respond to a runtime toggle of `bDesignerMode`, designers call `RefreshDesignerMode()` from Blueprint after changing the setting. Without that call, the toggle is effective on the next PIE start.

---

## Data Flow — Initial Spawn

```
LoadAndSpawnMission(id)
    └─> destroy any previously tracked actor for `id`
    └─> FLunarisHttpClient::FetchMissionActive(id, callback)
            └─> HandleMissionFetched(bSuccess, body, id)
                    └─> FJsonObjectConverter → FLunarisMissionData
                    └─> RequestSpawnFromData(data)
                            └─> FStreamableManager::RequestAsyncLoad(SoftPath, OnLoaded, HighPriority)
                                    └─> OnTargetClassLoaded(data, handle)
                                            ├─> Cast<UClass>(handle->GetLoadedAsset())
                                            ├─> validate AActor subclass + valid world
                                            ├─> World->SpawnActor<AActor>(...)
                                            ├─> SpawnedMissions[id] = actor
                                            ├─> LastKnownTargets[id] = data.TargetActor
                                            ├─> seed LastKnownHashes[id] via FetchMissionActiveHash
                                            └─> OnMissionSpawned.Broadcast(id, actor)
```

Three details that matter:

1. **The `FStreamableDelegate` is passed at `RequestAsyncLoad` time, not bound after.** Binding the completion delegate after the request via `Handle->BindCompleteDelegate(...)` is the canonical UE pitfall: when the asset is already in memory (cached from a previous load or referenced by the map), `RequestAsyncLoad` marks `HasLoadCompleted()==true` synchronously, and `BindCompleteDelegate` then returns `false` and silently drops the callback. The mission orphans with no log. This was a real bug fixed in commit `afe7d19` — never reverse the order.
2. **The lambda doesn't capture the handle.** It looks the handle back up from `InFlightLoads` by `MissionId` (the handle doesn't exist when the lambda is constructed). `OnTargetClassLoaded` keeps a `SoftPath.ResolveObject()` fallback as a safety net for the rare sync-fire timing.
3. **Hash seed-on-spawn.** Right after a successful spawn, the subsystem fires a one-shot `FetchMissionActiveHash` to populate `LastKnownHashes[MissionId]`. Without this seed, a designer publishing a new version within the poll-interval window after spawn would set the cache to the new hash on the first poll without ever reconciling — the actor would silently stay at the old version forever. The extra HTTP call is one per spawn; spawns are rare; the robustness is worth it.

---

## Data Flow — Designer-Mode Reconciliation

```
FTSTicker fires every DesignerPollIntervalSeconds
    └─> PollAllMissions(deltaTime)
            └─> for each MissionId in SpawnedMissions:
                    └─> FLunarisHttpClient::FetchMissionActiveHash(id, callback)
                            └─> HandleHashPolled(id, bSuccess, body)
                                    ├─> parse { "hash": "..." }
                                    ├─> compare against LastKnownHashes[id]
                                    ├─> if same: return (no work)
                                    └─> if different:
                                            └─> LastKnownHashes[id] = newHash
                                            └─> FLunarisHttpClient::FetchMissionActive(id, ...)
                                                    └─> HandleReconcileFetched(id, bSuccess, body)
                                                            └─> parse FLunarisMissionData
                                                            └─> ApplyReconcile(newData)
                                                                    ├─> if same class & live actor:
                                                                    │       └─> actor->SetActorLocation(newLoc)
                                                                    │       └─> OnMissionReconciled(actor, oldLoc, false)
                                                                    └─> else:
                                                                            └─> destroy old actor (if any)
                                                                            └─> RequestSpawnFromData(newData)  → re-enters spawn pipeline
                                                                            └─> OnMissionReconciled(nullptr, oldLoc, true)
```

### Reconcile decision tree

| Previous state | New `ClassPath` | Action |
|---|---|---|
| Live actor, same class | matches | `SetActorLocation` — zero flicker, no async load |
| Live actor, different class | differs | `Destroy` old + async-load new + spawn at new location |
| Tracked but actor invalid (destroyed externally) | any | Async-load new + spawn at new location |
| Untracked | any | Untracked missions are not polled — re-trigger via `LoadAndSpawnMission` |

The "live actor + same class" path is the **hot path** for typical designer iteration: tweaking a `SpawnLocation` on the published version. It's a single `SetActorLocation` call — no HTTP after the hash poll, no asset load, no actor recreation. That's the smooth, flicker-free demo experience.

---

## Tracking State

| Map | Purpose | Lifetime |
|---|---|---|
| `InFlightLoads: TMap<FString, TSharedPtr<FStreamableHandle>>` | Holds streamable handles until `OnTargetClassLoaded` consumes them. Losing the handle = losing the loaded asset. | Removed in `OnTargetClassLoaded` |
| `SpawnedMissions: TMap<FString, TWeakObjectPtr<AActor>>` | Active spawned actor per mission. Weak ptr lets us detect external destruction without holding a strong reference / `UPROPERTY`. | Cleared on respawn / class-replacement reconcile / subsystem deinit |
| `LastKnownTargets: TMap<FString, FLunarisTargetActor>` | Cached `TargetActor` data for each tracked mission. Used by `ApplyReconcile` to compare `ClassPath` (move vs replace) and report `OldLocation` in the event. | Updated on spawn and on each reconcile |
| `LastKnownHashes: TMap<FString, FString>` | Last seen `activeHash` per tracked mission. Compared against poll responses to detect publishes. Seeded after spawn via `FetchMissionActiveHash`. | Updated on every confirmed publish |

All four are intentionally **non-`UPROPERTY`** value containers. `TWeakObjectPtr` and `TSharedPtr<FStreamableHandle>` carry their own lifetime semantics; `FString` and `FLunarisTargetActor` are POD-ish data. None hold strong refs that would interfere with GC.

---

## Why FTSTicker (and not Tick/FTickableGameObject)

The poll loop runs at a configurable interval (`DesignerPollIntervalSeconds`, default 3.0s). Using `Tick`, `UActor::Tick`, or `FTickableGameObject::Tick` for this would fire **every frame** (60+ Hz) and waste ~20,000× more CPU than needed for a 3-second cadence task.

`FTSTicker::GetCoreTicker().AddTicker(delegate, interval)` is the canonical UE5 primitive for "do X every N seconds":
- Fires only at the configured interval (UE schedules it; you don't burn frames waiting).
- Survives world transitions (the ticker is global, not world-scoped).
- Auto-cleans up: returning `false` from the delegate removes it; `RemoveTicker(handle)` cancels at any time.
- Continues running when PIE is paused — important for Designer Mode, where the user might pause, edit on the backend, unpause, and expect the change to land.

Tom Looman's UE5 material warns repeatedly against `Tick` abuse for cadence-based work; this design follows that guidance. **Never** swap this for `FTickableGameObject` to "make it simpler" — the cost is enormous and the readability gain is tiny.

---

## Designer Mode Runtime Toggle

`Initialize` samples `ULunarisSettings::bDesignerMode` once. To toggle without restarting PIE, call `RefreshDesignerMode()` from Blueprint after editing the setting. The method is idempotent — calling it twice in a row with the same setting value is a no-op (`StartPolling` short-circuits if `PollerHandle` is already valid; `StopPolling` short-circuits if it's invalid).

A future enhancement could subscribe to `UDeveloperSettings::OnSettingChanged` for fully automatic reaction, but it adds editor-only API surface and isn't worth the complexity for MVP.

---

## GC Safety (the #1 source of crashes in this layer)

Async operations frequently outlive the objects that started them. UE's GC will happily destroy a `UObject` mid-flight. The patterns below are non-negotiable:

- **Capture `TWeakObjectPtr<This>` in every async lambda**, never raw `this`. Check `WeakThis.Get()` before any member access:
  ```cpp
  TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
  FLunarisHttpClient::FetchMissionActiveHash(id,
      FOnMissionHashFetchComplete::CreateLambda([WeakThis, id](bool bOK, const FString& Body) {
          if (ULunarisMissionSubsystem* Self = WeakThis.Get()) {
              Self->HandleHashPolled(id, bOK, Body);
          }
      }));
  ```
- **Use `IsValid(Obj)`, never `Obj != nullptr`.** `IsValid` also catches `PendingKill` objects — necessary for `TWeakObjectPtr::Get()` results since a `Destroy()` doesn't immediately null the pointer.
- **Hold `TSharedPtr<FStreamableHandle>` until consumed.** Losing the handle releases the load and the asset gets garbage-collected before `OnTargetClassLoaded` can use it.
- **Never store a `UObject*` in a non-`UPROPERTY` field expecting it to survive GC.** This layer's `SpawnedMissions` uses `TWeakObjectPtr` precisely because it's a value container with no GC reachability — actors are owned by the `World`, not the subsystem.

---

## What Runtime Must NOT Do

- ❌ Open HTTP requests directly. Always go through `FLunarisHttpClient` — Network is the only layer that talks to `FHttpModule`.
- ❌ Hardcode URLs or API keys. Read `ULunarisSettings` via `GetDefault<ULunarisSettings>()`.
- ❌ Use `Tick` / `FTickableGameObject` for cadence-based work. (See Why FTSTicker above.)
- ❌ Use `StaticLoadObject` / `StaticLoadClass` / hard `TSubclassOf` references in the mission path. Always `FSoftObjectPath` + `FStreamableManager::RequestAsyncLoad`. Hard refs blow asset memory and force synchronous loads.
- ❌ Bind a `FStreamableDelegate` after `RequestAsyncLoad`. Pass it as the second argument or you race with cached assets (see Initial Spawn note 1).
- ❌ Spawn from a worker thread. HTTP and Streamable callbacks both marshal back to the Game Thread automatically — trust UE.
- ❌ Throw exceptions. UE gameplay code does not. All failure modes flow through `OnMissionFailed.Broadcast` and `UE_LOG(LogLunaris, Error, ...)`.

---

## Conventions (UE 5.7 idioms)

- `TObjectPtr<T>` over raw `T*` for any `UObject` referenced by a `UPROPERTY`. Not used today (no UPROPERTY UObjects on the subsystem) but the rule applies the moment you add one.
- `TWeakObjectPtr<T>` for transient/optional references that must not block GC.
- `TSharedPtr<T>` for non-UObject lifetime management (e.g. `FStreamableHandle`).
- `FString::Printf(TEXT("..."), ...)` for formatted strings; `*Var` to dereference into `%s`.
- Subsystem bool returns from tickers: `true` = keep ticking, `false` = auto-remove. Always document which you mean inline.
- Categories: `"Lunaris"` for primary methods, `"Lunaris|Events"` for delegates, `"Lunaris|Designer Mode"` for designer-specific surface. The pipe groups them in Blueprint search.

---

## Testing

Per `.claude/rules/testing.md`, Runtime is **not unit-tested** today — `FStreamableManager` and `UWorld` are heavy to mock and the value-per-test is low. Coverage:
- ✅ Manual QA via the `MVP_TEST.md` walkthrough.
- ❌ Automated tests for the subsystem itself.
- 🟡 Future: extract pure decision functions (e.g. `ShouldReconcileInPlace(prev, next) -> bool`) so the algorithm can be unit-tested independently of `UWorld`.

The reconcile algorithm has enough branching that adding a pure-function unit test for `ShouldReconcileInPlace` is the highest-ROI testing investment when we get there.

---

## Update this skill when you change Runtime

Per `CLAUDE.md`'s MANDATORY rule, update this file whenever you:
- add/rename a `UFUNCTION` or `BlueprintAssignable` delegate
- change the reconcile decision tree (move vs replace conditions)
- alter the lifecycle hooks (`Initialize` / `Deinitialize` / new override)
- swap the ticker primitive (`FTSTicker` → `FTimerManager`, etc.) — this is a major change and needs explicit discussion
- modify the tracking-state map shapes (add/remove a map, change a value type)
- change Designer Mode toggle semantics
- add new failure paths or change which failures broadcast `OnMissionFailed` vs. log silently
