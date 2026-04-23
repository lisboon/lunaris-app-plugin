# lunaris-app-plugin

Unreal Engine 5.7+ Plugin for the **Lunaris Mission Orchestration Engine**. It consumes compiled JSON contracts from the backend and executes game logic dynamically using Soft Referencing and asynchronous asset loading.

## What Lunaris Plugin is (immutable product context)

- **M2M Consumer**: It authenticates against the Lunaris Backend using an HMAC-hashed `x-api-key`.
- **Zero-Recompile Workflow**: It parses `MissionContract` JSONs and spawns actors or executes logic without requiring Game Designers to rebuild C++ or cook `.uasset` files.
- **Performance First**: All HTTP requests are asynchronous. All asset loading utilizes `FStreamableManager` (Soft Referencing) to ensure the Game Thread is never blocked.

## MANDATORY RULE: Keep Your Skills Up to Date

**After ANY interaction with the project** — whether it's reading code, editing files, refactoring C++, fixing memory leaks, or answering questions — **you MUST update the corresponding skill** in `.claude/skills/` with what you've learned.

This includes, but is not limited to:
- **New USTRUCTs, UCLASSes or UENUMs** → update the module's skill.
- **Changes to HTTP Parsing or Memory Management** → update the skill.
- **New Blueprint Nodes exposed** → document in `project/SKILL.md`.
- **New external integrations/plugins** → update `integrations/SKILL.md`.
- **Changes to `Lunaris.Build.cs` (Dependencies)** → update `lunaris-ecosystem`.

**Skills are the lifeblood of the project. If they are out of date, all future interactions will be compromised.**

---

## Architecture

- **Core Layer** (`Public/Core/` & `Private/Core/`) — Pure USTRUCTs defining the data contract (JSON mapping) and UDeveloperSettings.
- **Network Layer** (`Public/Network/` & `Private/Network/`) — Pure HTTP request/response handling. Isolated from gameplay logic.
- **Runtime Layer** (`Public/Runtime/` & `Private/Runtime/`) — `UGameInstanceSubsystem` and Asset Resolvers. Bridges the Network data to the Game Thread (Spawning, Delegates).
- **Dependency Direction**: Runtime → Network → Core. (Network should never depend on Runtime).

## Stack

- **Engine**: Unreal Engine 5.7+
- **Language**: Modern C++ (C++20 standard supported by UE5)
- **Modules Used**: `Core`, `CoreUObject`, `Engine`, `HTTP`, `Json`, `JsonUtilities`, `DeveloperSettings`.

## Conventions

- **Files**: Split into `Public/` (.h) and `Private/` (.cpp). 
- **Prefixes**: 
  - `U` for classes deriving from `UObject` (e.g., `ULunarisMissionSubsystem`).
  - `A` for classes deriving from `AActor`.
  - `F` for pure C++ classes or `USTRUCT` (e.g., `FLunarisHttpClient`, `FLunarisMissionData`).
  - `E` for Enums.
- **Logging**: Always use `UE_LOG(LogLunaris, ...)` for debug and errors.
- **Exposing to Blueprint**: Use `UPROPERTY(BlueprintReadOnly)` for data structs and `UFUNCTION(BlueprintCallable)` for methods designers need to access.

## Patterns

- **Memory Management**: Never use raw pointers for UObjects that must persist. Use `TObjectPtr` or `UPROPERTY()`. For async delegates, use `TWeakObjectPtr` to prevent crashes if the object is destroyed mid-flight.
- **Async Asset Loading**: Never use `StaticLoadObject` or `StaticLoadClass` (hard references) on the main thread for heavy assets. Always use `FSoftClassPath` and `FStreamableManager::RequestAsyncLoad`.
- **HTTP Delegation**: Network classes must not hold state. They fire requests and return data via `DECLARE_DELEGATE` (C++ delegates, not dynamic multicast).
- **Subsystem**: The entry point is always a `UGameInstanceSubsystem`, ensuring the logic survives level transitions.

## Commands

*(As this is a UE5 Plugin, commands relate to the Editor and Build Tools)*

- **Generate VS Files**: Right-click `.uproject` -> Generate Visual Studio project files.
- **Live Coding**: Press `Ctrl + Alt + F11` inside the Unreal Editor (for minor C++ changes).
- **Full Rebuild**: Close Editor, Clean Build from IDE (Visual Studio / Rider).

## Skills (Slash Commands)

For details on each module (Core, Network, Runtime), use the following skills:

- **In Development**

## Rules (Contextual)

Path-specific rules are automatically loaded when working with files:

- **In Development**

## Shared Skills (Lunaris Ecosystem)

Skills with the `lunaris-` prefix are symlinks to `~/lunaris-claude/` and work in all repos:

- **In Development**