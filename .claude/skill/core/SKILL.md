---
name: core
description: Core layer of the Lunaris plugin — pure data contract (USTRUCTs mapping backend JSON) and UDeveloperSettings. Zero dependencies on Network, Runtime, Engine/World, or HTTP.
user-invocable: true
argument-hint: ""
---

# Core Layer

**Purpose:** the Core layer owns (1) the **data contract** the plugin reads from the Lunaris backend and (2) the **user-visible configuration** exposed through Unreal's Project Settings. It is pure: no HTTP, no `UWorld`, no `FStreamableManager`, no spawning. Everything here is safe to include from any other layer.

**Scope:** Core is the single source of truth for the shape of a `MissionContract`. If the backend JSON changes, the change starts here.

---

## Files

```
Source/Lunaris/
├── Public/Core/
│   ├── LunarisTypes.h        ← FLunarisMissionData, FLunarisTargetActor (USTRUCTs, BlueprintType)
│   └── LunarisSettings.h     ← ULunarisSettings (UDeveloperSettings) — BackendUrl, EngineApiKey, Designer Mode
└── Private/Core/             ← currently empty; add .cpp here only if a USTRUCT needs a non-trivial body (custom validator, normalizer) — defaults live inline in the header
```

There is intentionally **no** `LunarisTypes.cpp` or `LunarisSettings.cpp` today. Every member has an inline default, and USTRUCTs with only data and no behavior don't need a translation unit. If a future type needs custom logic (e.g. normalizing a class path before use), add a `.cpp` next to this rule and document it here.

---

## USTRUCT Contracts

`FLunarisTargetActor`
```cpp
USTRUCT(BlueprintType)
struct FLunarisTargetActor
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
    FString ClassPath;                // "/Game/BP_LunarisCube.BP_LunarisCube_C" — FSoftClassPath-compatible

    UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
    FVector SpawnLocation = FVector::ZeroVector;
};
```

`FLunarisMissionData`
```cpp
USTRUCT(BlueprintType)
struct FLunarisMissionData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
    FString MissionId;

    UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
    FLunarisTargetActor TargetActor;
};
```

### JSON mapping contract

The backend emits camelCase JSON. `FJsonObjectConverter::JsonObjectStringToUStruct` matches **case-insensitively by default**, so PascalCase UPROPERTY names (`MissionId`, `TargetActor`, `ClassPath`, `SpawnLocation`) resolve correctly against `missionId`, `targetActor`, `classPath`, `spawnLocation`. **Do not rename properties to snake_case** — keep the UE-idiomatic PascalCase and let the converter bridge the naming.

Today the runtime consumes the `missionData` payload directly (shape `{ missionId, targetActor: { classPath, spawnLocation } }`). The `mission.types.ts` file in the backend declares a richer `MissionContract` with `meta` and `graph` — that is aspirational and not yet consumed by the plugin. When we start reading those, add matching `UPROPERTY` fields here first, then wire through Runtime.

### Rules for extending the contract

- A new field on the backend **must** be added to the USTRUCT here before any other layer can read it.
- USTRUCTs exposed to Blueprint require `USTRUCT(BlueprintType)` + `UPROPERTY(BlueprintReadOnly, Category = "Lunaris")` on every member.
- Always initialize new members (`FVector::ZeroVector`, `INDEX_NONE`, `false`) — UE leaves uninitialized members as garbage otherwise.
- Unknown JSON keys are ignored by the converter, which makes this layer forward-compatible: backend can ship new fields without breaking older plugin builds.
- **Never** add methods with side effects here. Pure helpers (validators, normalizers) are OK and go into `Private/Core/`.

---

## UDeveloperSettings — `ULunarisSettings`

```cpp
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Lunaris"))
class LUNARIS_API ULunarisSettings : public UDeveloperSettings
```

Appears under **Edit → Project Settings → Plugins → Lunaris** in the Editor. `Config = Game` + `DefaultConfig` persists to `Config/DefaultGame.ini`, which is what ships with the project.

### Fields

| Field | Type | Default | Category | Notes |
|---|---|---|---|---|
| `BackendUrl` | `FString` | `http://localhost:3001` | Backend | Base URL, no trailing slash. Read by `FLunarisHttpClient`. |
| `EngineApiKey` | `FString` | empty | Backend | M2M key issued by the backend (`POST /api-keys`). Injected as `x-api-key` header. **Never log.** Fail-fast is enforced in `FLunarisHttpClient` when empty. |
| `bDesignerMode` | `bool` | `false` | Designer Mode | Toggles live reconciliation in `ULunarisMissionSubsystem`. **Must be false in shipped builds** — when true, the subsystem opens an HTTP poll loop against the backend. |
| `DesignerPollIntervalSeconds` | `float` | `3.0f` | Designer Mode | Interval between hash polls. `ClampMin = 0.5`, `ClampMax = 60.0`, `UIMax = 15.0`. `EditCondition = bDesignerMode` grays it out when designer mode is off. Units: seconds. |

### Rules for settings

- Always pull through `GetDefault<ULunarisSettings>()` — never hardcode URLs/keys in code.
- Every field is `Config` so values persist to `DefaultGame.ini` on save.
- Sensitive fields (`EngineApiKey`) are still stored in the config file — this is a dev-time convenience. For CI / shipped builds, override via `ENVIRONMENT_VARIABLE` injection at build time or a secrets manager (not implemented yet; document when added).
- Numeric fields use `ClampMin`/`ClampMax` for runtime safety and `UIMin`/`UIMax` for the editor slider. Prefer `Units = "Seconds"` on time values — the editor shows the suffix automatically.
- Booleans drive `EditCondition` on dependent fields so the UI stays self-explanatory (e.g. poll interval is dimmed when designer mode is off).

### Why designer mode is off by default

Shipped games must not open a background HTTP loop to the Lunaris backend. The API key would be baked into the client, the poll is wasted network traffic, and the feature is meaningless without a designer sitting at the web editor changing versions. The default enforces the "opt in once, only in editor" contract.

---

## Dependency Boundaries

Core is at the bottom of the dependency graph:

```
Runtime → Network → Core
```

- Core may `#include "CoreMinimal.h"`, `"Engine/DeveloperSettings.h"`, UE-standard math/string headers. **Nothing else.**
- Core must **not** include `HttpModule.h`, `Engine/World.h`, `Engine/StreamableManager.h`, or any `Runtime/*` / `Network/*` header.
- If you find yourself wanting to include Network/Runtime here, the design is inverted — push the logic down into a service/subsystem instead.

---

## Logging

Declared in `Lunaris.h` (module root, not `Core/`):
```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogLunaris, Log, All);
```

Core rarely logs — most validation failures are caught by the Notification pattern at the Runtime boundary. When Core does log (e.g. a future validator), it uses `LogLunaris`, never `LogTemp`.

---

## Testing

Per `.claude/rules/testing.md`:
- Core USTRUCTs are tested via `FJsonObjectConverter::JsonObjectStringToUStruct` round-trip — pass a raw JSON string, assert every USTRUCT field matches the expected values.
- Test file lives under `Source/Lunaris/Private/Tests/`, named `LunarisJsonParsingTest.cpp` (one per target class).
- Namespace: `Lunaris.Unit.Core.<Target>` (e.g. `Lunaris.Unit.Core.JsonParsing`).
- Settings (`ULunarisSettings`) are not unit-tested — they're data; tested implicitly by Network specs that read config.

---

## Conventions (UE 5.7 idioms)

- `FString` over `std::string`, `TArray<T>` over `std::vector`, `TMap<K,V>` over `std::unordered_map`. Never mix STL and UE containers across API boundaries.
- String literals wrapped in `TEXT("...")` for UTF-16 correctness.
- `TObjectPtr<T>` over raw `T*` for any `UObject` referenced by a `UPROPERTY` (UE 5.0+ idiom — gives the GC reachability for free and enables tooling). Not relevant today for Core but the rule applies if you add one.
- PascalCase for types and members, `b` prefix for booleans (`bDesignerMode`).
- `Category = "Lunaris"` on Blueprint-exposed fields; subcategorize (`"Lunaris|Backend"`, `"Lunaris|Designer Mode"`) only if the member count makes a single flat list unwieldy.

These mirror the standards Tom Looman and Stephen Ulibarri teach in their UE5 C++ material — stay on the idiomatic path.

---

## Update this skill when you change Core

Per `CLAUDE.md`'s MANDATORY rule, update this file whenever you:
- add/rename a USTRUCT or UPROPERTY
- add/change a `ULunarisSettings` field
- touch JSON mapping behavior (flags passed to `JsonObjectStringToUStruct`, custom converters)
- introduce a `.cpp` in `Private/Core/` (document why — it's the exception, not the default)
- change dependency boundaries (e.g. add a new `Public/Core/` header)
