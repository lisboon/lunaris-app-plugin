---
name: project
description: lunaris-app-plugin Architecture and Patterns Guide
user-invocable: true
argument-hint: ""
---

# lunaris-app-plugin — Architecture and Patterns Guide

You are a specialist assistant on this project. Use this knowledge to generate code that is consistent with existing standards.

This plugin is the **Unreal Engine consumer** of the Lunaris Mission Orchestration Engine. Its counterpart is `lunaris-app-backend` (NestJS/DDD). This plugin does **not** produce contracts — it **fetches, parses, and executes** them inside UE5.

---

## Architecture

The plugin follows a **Layered Architecture** inspired by Clean Architecture, adapted to Unreal Engine's object model:

- **Core Layer** (`Source/Lunaris/Public/Core/` + `Private/Core/`) — Pure `USTRUCT`s and `UDeveloperSettings`. Data contract and configuration. No HTTP, no Actors, no World.
- **Network Layer** (`Source/Lunaris/Public/Network/` + `Private/Network/`) — Stateless HTTP wrappers over `FHttpModule`. Fetches raw JSON, delegates back.
- **Runtime Layer** (`Source/Lunaris/Public/Runtime/` + `Private/Runtime/`) — `UGameInstanceSubsystem`, Asset Resolvers, Spawning. Bridges Network data to the Game Thread.

**Dependency Direction**: `Runtime → Network → Core`. Never the other way around. The Network layer must never know about `UWorld`, `AActor`, or `FStreamableManager`.

---

## Product Model (M2M Consumer)

The plugin is a **Machine-to-Machine consumer** of the backend's Engine API (`/missions/engine/*`):

| Concept | Role | Scope |
|---|---|---|
| `EngineApiKey` | HMAC-hashed key stored in `ULunarisSettings`. Injected as `x-api-key` on every request. | Per-Workspace (issued by backend) |
| `MissionContract` | Compiled JSON payload from backend describing what to spawn, where, and when. | Per-Mission |
| `MissionSubsystem` | The runtime entry point. Survives level transitions. | Per-GameInstance |

**Rules:**
- Every request to `/missions/engine/*` **must** carry the `x-api-key` header pulled from `ULunarisSettings` (via `GetDefault<ULunarisSettings>()`).
- Never hardcode URLs or keys — always read from settings.
- "Mission" is the canonical domain name — do not introduce "Quest", "Task", or "Objective" as substitutes.

---

## Module Structure

The plugin is a single UE module (`Lunaris`), but internally split by layer:

```
Source/Lunaris/
├── Lunaris.Build.cs              # Module dependencies
├── Public/
│   ├── Lunaris.h                 # Module interface + LogLunaris category
│   ├── Core/
│   │   ├── LunarisTypes.h        # USTRUCTs (FLunarisMissionData, FTargetActor, ...)
│   │   └── LunarisSettings.h     # UDeveloperSettings (EngineApiKey, BackendUrl)
│   ├── Network/
│   │   └── LunarisHttpClient.h   # Stateless HTTP wrapper (static methods)
│   └── Runtime/
│       └── LunarisMissionSubsystem.h  # UGameInstanceSubsystem entry point
└── Private/
    ├── Lunaris.cpp
    ├── Core/                     # (empty unless USTRUCTs need custom validators)
    ├── Network/
    │   └── LunarisHttpClient.cpp
    ├── Runtime/
    │   └── LunarisMissionSubsystem.cpp
    └── Tests/                    # IMPLEMENT_SIMPLE_AUTOMATION_TEST files
        └── LunarisJsonParsingTest.cpp
```

---

## Naming Conventions (Epic Standard)

### Prefixes
| Prefix | Applies To | Example |
|---|---|---|
| `U` | Classes inheriting from `UObject` | `ULunarisMissionSubsystem`, `ULunarisSettings` |
| `A` | Classes inheriting from `AActor` | `ALunarisMissionActor` |
| `F` | `USTRUCT` or pure C++ class | `FLunarisMissionData`, `FLunarisHttpClient` |
| `E` | Enums | `ELunarisMissionState` |
| `I` | Interfaces (`UINTERFACE`) | `ILunarisResolvable` |

### Casing
- **PascalCase** for types, methods, and properties: `FetchMissionActive`, `MissionId`
- **bPrefix** for booleans: `bIsLoaded`, `bSuccess`
- **UPPER_SNAKE_CASE** for macros and defines only

### Files
- `.h` in `Public/`, `.cpp` in `Private/`, mirrored by layer.
- Tests in `Private/Tests/` named `[TargetClass]Test.cpp`.

### Strings
- **Always** wrap literals with `TEXT("...")` for UTF-16 correctness.

### Logging
- **Always** use the custom category: `UE_LOG(LogLunaris, Verbosity, TEXT("..."), *Var);`
- Declared in `Lunaris.h` via `DECLARE_LOG_CATEGORY_EXTERN(LogLunaris, Log, All);`

---

## Memory & GC Safety

Unreal's Garbage Collector can destroy `UObject`s mid-flight during async operations. This is the #1 source of crashes in this plugin.

- **Never use raw `T*` pointers** for `UObject`s that persist across frames. Use `TObjectPtr<T>` (UE5.0+) or `UPROPERTY()`.
- **Async callbacks must capture `TWeakObjectPtr<This>`**, never `this` directly:
  ```cpp
  TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
  Handle->BindCompleteDelegate(FStreamableDelegate::CreateLambda([WeakThis, Handle]() {
      if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get()) {
          StrongThis->OnTargetClassLoaded(Handle);
      }
  }));
  ```
- **Validity check**: always `IsValid(Obj)`, never `Obj != nullptr`. `IsValid` also catches `PendingKill` objects.
- **Keep `TSharedPtr<FStreamableHandle>` alive** until the asset is consumed. Losing the handle = losing the loaded asset.

---

## Async & Threading

- **Game Thread is sacred** — never block it to load assets or wait for HTTP.
- **HTTP**: `FHttpModule` dispatches on a background thread; the completion callback fires back on the Game Thread. Use C++ delegates (`DECLARE_DELEGATE_TwoParams`) to pass results upward.
- **Asset Loading**: never `StaticLoadObject` or hard `TSubclassOf` in the mission path. Always `FSoftClassPath` + `FStreamableManager::RequestAsyncLoad`.
- **Retrieving loaded assets**: do not call `SoftClass.Get()` after the callback — use `Handle->GetLoadedAsset()`:
  ```cpp
  UClass* LoadedClass = Cast<UClass>(Handle->GetLoadedAsset());
  ```

---

## HTTP Contract (Network Layer)

The Network layer is a **stateless** wrapper. It must not hold game state, spawn actors, or parse JSON into USTRUCTs (that belongs to the Runtime).

### Delegate Contract
```cpp
// bSuccess == true  → second arg is raw JSON payload
// bSuccess == false → second arg is a human-readable error message ("HTTP Error 401", "Network/Timeout error.")
DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool /*bSuccess*/, const FString& /*JsonOrError*/);
```

### Response Validation Order (mandatory)
1. `if (!bSuccess || !Response.IsValid())` → timeout/network failure
2. `if (Response->GetResponseCode() != 200)` → HTTP-level error
3. Only then emit `bSuccess=true` with `Response->GetContentAsString()`

### Auth
- Pull `EngineApiKey` from `GetDefault<ULunarisSettings>()`.
- Fail-fast (invoke delegate with `bSuccess=false`) if the key is empty.
- **Never** log the key. Log the URL and MissionId only.

---

## JSON Parsing (Core Layer)

- Use `FJsonObjectConverter::JsonObjectStringToUStruct` to map JSON → `USTRUCT`.
- USTRUCT field names **must match the backend payload keys exactly** (case-sensitive by default in UE's converter unless `CPF_Transient` flags are tuned).
- If the backend uses `camelCase`, the USTRUCT properties should also be `camelCase` in JSON (UE handles this via `UPROPERTY` metadata, but prefer contract alignment).
- Parsing failures are **silent** in UE — always check the `bool` return of `JsonObjectStringToUStruct` and `UE_LOG(LogLunaris, Error, ...)` on failure.

---

## Blueprint Exposure

The Runtime layer is the only place that should expose anything to Blueprints.

- **Events** → `DECLARE_DYNAMIC_MULTICAST_DELEGATE_*` + `UPROPERTY(BlueprintAssignable)`, prefixed `On` (e.g., `OnMissionSpawned`, `OnMissionFailed`).
- **Functions** → `UFUNCTION(BlueprintCallable, Category = "Lunaris")`.
- **Data** → `UPROPERTY(BlueprintReadOnly)` for designer-visible fields.
- Core USTRUCTs used in Blueprints need `USTRUCT(BlueprintType)` + `UPROPERTY(BlueprintReadOnly)` on each member.

---

## Spawning & World

When spawning actors from a contract:
- Always validate `GetWorld()` is non-null.
- Use `ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn` so missions don't fail silently on geometry collisions.
- Spawn on the Game Thread only (guaranteed if triggered from HTTP/Streamable callbacks, which marshal back to Game Thread).

---

## Error Handling

| Scenario | Location | How |
|---|---|---|
| HTTP failure (timeout, 4xx, 5xx) | Network | Delegate `bSuccess=false` + error string |
| JSON parse failure | Runtime | `UE_LOG(LogLunaris, Error, ...)` + broadcast `OnMissionFailed` |
| Asset load failure (soft class not found) | Runtime | `UE_LOG` + broadcast `OnMissionFailed` |
| Missing `EngineApiKey` | Network | Fail-fast, never dispatch the request |
| World invalid during spawn | Runtime | Early return + warning log |

There are no exceptions in UE gameplay code — everything flows through delegates, logs, and `bool` returns.

---

## Testing

- **Framework**: Unreal Automation Framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`)
- **Location**: `Source/Lunaris/Private/Tests/`
- **Naming hierarchy**: `Lunaris.Unit.<Layer>.<Target>` (e.g., `Lunaris.Unit.Core.JsonParsing`)
- **Flags**: `EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter`
- **Scope**:
  - ✅ Core: JSON → USTRUCT roundtrip with `FJsonObjectConverter`
  - ✅ Network: response-handler logic (mock bSuccess/StatusCode inputs)
  - ❌ Runtime: not unit-tested (requires `FStreamableManager` mocking) — covered by manual QA
- Run via **Tools → Session Frontend → Automation** inside the UE Editor.

---

## Build Dependencies (`Lunaris.Build.cs`)

Minimum required modules:
- `Core`, `CoreUObject`, `Engine` — UE foundation
- `HTTP` — `FHttpModule`
- `Json`, `JsonUtilities` — `FJsonObjectConverter`
- `DeveloperSettings` — `ULunarisSettings`

Any new dependency added here **must** be reflected in the `lunaris-ecosystem` skill (when present).

---

## When Generating Code

1. **Respect layer boundaries** — never `#include "Engine/World.h"` from the Network layer.
2. **Every persistent `UObject*` gets `UPROPERTY()` or `TObjectPtr`** — no exceptions.
3. **Every async lambda captures `TWeakObjectPtr`** — never raw `this`.
4. **Every HTTP call reads config from `ULunarisSettings`** — never hardcode.
5. **Every log uses `LogLunaris`** — never `LogTemp`.
6. **Every string literal uses `TEXT("...")`**.
7. **Every USTRUCT exposed to Blueprint uses `USTRUCT(BlueprintType)`** with `UPROPERTY(BlueprintReadOnly)` members.
8. **Never use `StaticLoadObject` / `StaticLoadClass` in the mission path** — always soft references + `FStreamableManager`.
9. **Never log the `EngineApiKey`** — ever.
10. **Event names start with `On`** (`OnMissionSpawned`, not `MissionSpawnedEvent`).
11. **HTTP response validation follows the exact order**: `bSuccess` → `Response.IsValid()` → `StatusCode == 200`.
12. **Tests live under `Private/Tests/`** and use the `Lunaris.Unit.*` namespace.
13. **Conventional commits** — consistent with the backend repo (`feat:`, `fix:`, `refactor:`, `docs:`, `test:`, `chore:`).
14. **Any new USTRUCT / UCLASS / UENUM** — update the corresponding layer's `SKILL.md` (mandatory, per `CLAUDE.md`).
15. **Any change to `Lunaris.Build.cs`** — update the `lunaris-ecosystem` skill.
