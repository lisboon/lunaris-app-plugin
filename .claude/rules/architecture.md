---
paths:
  - "Source/Lunaris/**/*.cpp"
  - "Source/Lunaris/**/*.h"
---

# Unreal Engine Plugin Layer — Rules

## Architecture Overview

Lunaris Unreal Plugin is a pure C++ module designed to fetch, parse, and execute JSON-based Mission Contracts asynchronously. It follows a strict separation of concerns to avoid blocking the Game Thread and to prevent memory leaks.

- **Core Layer (`Public/Core/`)** — Pure USTRUCTs defining the data contract (JSON mapping) and UDeveloperSettings.
- **Network Layer (`Public/Network/`)** — Isolated HTTP request/response handling. Does not know about Actors or Worlds.
- **Runtime Layer (`Public/Runtime/`)** — `UGameInstanceSubsystem` and Asset Resolvers. Bridges Network data to the Game Thread (Spawning, Delegates).

**Dependency Direction**: Runtime → Network → Core. (Network must never depend on Runtime).

---

## 1. Memory Management & Safety

Unreal's Garbage Collector (GC) is aggressive. Async operations (HTTP, Asset Loading) can outlive the objects that started them.

- **Never use raw pointers (`T*`) for UObjects that persist.** Use `TObjectPtr<T>` or `UPROPERTY()`.
- **Async Callbacks (Lambdas/Delegates):** If an async operation calls back into a `UObject`, you **MUST** capture a `TWeakObjectPtr` of `this`. Check validity before executing:
  ```cpp
  TWeakObjectPtr<ULunarisMissionSubsystem> WeakThis(this);
  Handle->BindCompleteDelegate(FStreamableDelegate::CreateLambda([WeakThis, MissionData, Handle]() {
      if (ULunarisMissionSubsystem* StrongThis = WeakThis.Get()) {
          StrongThis->OnTargetClassLoaded(MissionData, Handle);
      }
  }));
  ```
- Object Validity: Always use IsValid(Obj) instead of Obj != nullptr.


## 2. Asynchronous Execution

- `No Hitching`: You must never block the Game Thread to load heavy assets.
- `Soft Referencing`: Never use `StaticLoadObject` or hard `TSubclassOf` references in the mission path.
- `Asset Loading`: Always use `FSoftClassPath` and `FStreamableManager::RequestAsyncLoad`.
- `Getting Loaded Assets`: Do not call `SoftClass.Get()` immediately after a streamable callback. Keep the `TSharedPtr<FStreamableHandle>` and use `Handle->GetLoadedAsset()`.

  ```cpp
  // ✅ CORRECT
  UClass* LoadedClass = Cast<UClass>(Handle->GetLoadedAsset());
  ```

## 3. Network & HTTP Delegation

The Network Layer (`FLunarisHttpClient`) is a static/stateless wrapper around `FHttpModule`. It must not hold game state.

- `Delegates`: Use standard C++ delegates (`DECLARE_DELEGATE_TwoParams`) to pass data back to the Runtime layer.
- `Error Handling`: Check `bSuccess`, `Response.IsValid()`, and `Response->GetResponseCode() == 200` before parsing.
- `JSON Parsing`: Rely on `FJsonObjectConverter::JsonObjectStringToUStruct` to map JSON directly to `FLunarisMissionData` USTRUCTs.

## 4. Subsystems & Blueprint Exposure

The entry point for Game Designers is the `ULunarisMissionSubsystem`, which inherits from `UGameInstanceSubsystem`.

- `Why GameInstance?` It survives level transitions. Missions shouldn't break when loading a new map.
- `Events`: Expose outcomes to Blueprints using `DECLARE_DYNAMIC_MULTICAST_DELEGATE.`
  - Prefix events with `On` (e.g., `OnMissionSpawned`).
  - Mark them as `UPROPERTY(BlueprintAssignable).`
- `Functions`: Expose callable methods using `UFUNCTION(BlueprintCallable, Category = "Lunaris").`

## 5. Spawning & World Interaction

When the Runtime Layer spawns an actor based on the Mission Contract:

- `World Context`: Always ensure the `UWorld` is valid `(GetWorld())`.
- `Collision Handling`: Use `ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn` to prevent missions from failing silently if coordinates collide with geometry.
  ```cpp
  FActorSpawnParameters SpawnParams;
  SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
  AActor* SpawnedActor = World->SpawnActor<AActor>(LoadedClass, Location, Rotation, SpawnParams);
  ```

## 6. Naming Conventions (Epic Standard)

Follow Unreal Engine standards strictly:

- `U` prefix for classes inheriting from UObject.
- `A` prefix for classes inheriting from AActor.
- `F` prefix for structs (USTRUCT) and pure C++ classes.
- `E` prefix for Enums.
- Use PascalCase for variables and methods.
- Use `TEXT("MyString")` macro for all string literals to ensure proper encoding.
Logging: Use the custom module category: `UE_LOG(LogLunaris, Warning, TEXT("Message %s"), *Var);`