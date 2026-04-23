---
paths:
  - "Source/Lunaris/Public/Network/**/*.h"
  - "Source/Lunaris/Private/Network/**/*.cpp"
---

# Network Layer (HTTP) — Rules

## Overview

The `Network` layer in the Lunaris Plugin replaces the concept of a "Controller" in standard web development. Instead of receiving requests, it **executes** them.

It acts as an isolated, stateless wrapper over Unreal's `FHttpModule`. It must **never** hold game state, spawn actors, or know about the `UWorld`. Its sole responsibility is:
1. Format the HTTP Request.
2. Inject Authentication (`x-api-key`).
3. Handle Timeouts and Network Errors.
4. Pass the raw response back to the caller via a Delegate.

---

## 1. Statutory Implementation

Network handlers must be pure C++ classes or static methods. Do not inherit from `UObject` unless explicitly managing complex long-lived state (not needed for MVP).

```cpp
// ❌ WRONG: Don't tie HTTP logic to an Actor or Subsystem
void ULunarisMissionSubsystem::FetchMission() { ... }

// ✅ CORRECT: Isolated static helper
class LUNARIS_API FLunarisHttpClient
{
public:
    static void FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate);
};
```

## 2. Authentication & Headers

Every request destined for the Lunaris Backend M2M `(/missions/engine/*)` `must` include the `x-api-key` header.

- `Never hardcode the URL or Key`. Always retrieve them from `ULunarisSettings` (via `GetDefault<ULunarisSettings>()`).
- Always check if the `EngineApiKey` is empty and fail-fast (abort request) if it is.
  ```cpp
  const ULunarisSettings* Settings = GetDefault<ULunarisSettings>();
  if (Settings->EngineApiKey.IsEmpty()) {
      // Fail fast via delegate
      return;
  }
  Request->SetHeader(TEXT("x-api-key"), Settings->EngineApiKey);
  ```

## 3. Asynchronous Delegates

The FHttpModule executes asynchronously on a background thread but triggers its completion callback on the Game Thread.

- `Always use C++ Delegates (DECLARE_DELEGATE)` to pass the result back to the caller.
- `Never block the Game Thread`. Do not use `while(!Request->OnProcessRequestComplete())` or any synchronous wait mechanisms
  ```cpp
  // Delegate signature: (bool bSuccess, const FString& JsonOrErrorMessage)
  DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool, const FString&);
  ```

## 4. Defensive Response Handling

The backend can fail (500), the network can drop (Timeout), or the user's key might be revoked (401). The Network layer must handle all these before passing data to the parser.

You must check for validity in this exact order:

1. `Connection Success & Validity: if (!bSuccess || !Response.IsValid())`
2. `HTTP Status Code: if (Response->GetResponseCode() != 200)`
3. (Only then pass the raw string to the callback)
    ```cpp
    void FLunarisHttpClient::OnHttpResponseReceived(...)
    {
        if (!bSuccess || !Response.IsValid())
        {
            OnCompleteDelegate.ExecuteIfBound(false, TEXT("Network/Timeout error."));
            return;
        }

        const int32 StatusCode = Response->GetResponseCode();
        if (StatusCode != 200)
        {
            OnCompleteDelegate.ExecuteIfBound(false, FString::Printf(TEXT("HTTP Error %d"), StatusCode));
            return;
        }

        // Success
        OnCompleteDelegate.ExecuteIfBound(true, Response->GetContentAsString());
    }
    ``` 

## 5. Security & Logging

- `Do NOT log the x-api-key.` Never print the user's secret to the `UE_LOG` console.
- Log the target URL and MissionId to aid debugging, but sanitize any sensitive parameters if added in the future.
- Use the custom `LogLunaris` category for all output:
  ```cpp
  UE_LOG(LogLunaris, Log, TEXT("Fetching mission from %s"), *Url);
  ```

## Error Handling Contract

The Network layer delegates the response. The `bSuccess` boolean acts as the contract:

- `bSuccess == true` → The `FString` argument contains the `raw JSON payload.`
- `bSuccess == false` → The `FString` argument contains a `human-readable error message` (e.g., "HTTP Error 401").

The caller (e.g., `ULunarisMissionSubsystem`) is responsible for parsing the JSON into `USTRUCTs` using `FJsonObjectConverter`. The Network layer `does not` parse JSON.
