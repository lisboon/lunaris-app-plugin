---
name: network
description: Network layer of the Lunaris plugin — stateless HTTP wrappers over FHttpModule, fetching raw JSON from the backend Engine surface. Knows nothing about UWorld, AActor, USTRUCT parsing, or game state.
user-invocable: true
argument-hint: ""
---

# Network Layer

**Purpose:** translate the Lunaris backend's M2M Engine REST surface (`/missions/engine/*`) into asynchronous C++ delegate callbacks on the Game Thread. The Network layer is the **only** place in the plugin allowed to talk HTTP. It does not parse JSON into `USTRUCT`s, does not own game state, and does not know what a `UWorld` is.

**Position in the dependency graph:** `Runtime → Network → Core`. Network depends on Core (`ULunarisSettings`) but **never** on Runtime.

---

## Files

```
Source/Lunaris/
├── Public/Network/
│   └── LunarisHttpClient.h      ← public static API + delegate types
└── Private/Network/
    └── LunarisHttpClient.cpp    ← FHttpModule glue + shared response handler
```

There is no class instance — `FLunarisHttpClient` is a pure namespace of static methods. This is intentional: **stateless** is one of the layer rules.

---

## Public API

```cpp
// Delegate signature: bool bSuccess + FString (raw JSON body on success, error message on failure)
DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool, const FString&);

// Watch endpoint shares the same shape — alias keeps call sites self-documenting
using FOnMissionHashFetchComplete = FOnMissionFetchComplete;

class LUNARIS_API FLunarisHttpClient
{
public:
    static void FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate);
    static void FetchMissionActiveHash(const FString& MissionId, FOnMissionHashFetchComplete OnCompleteDelegate);
};
```

| Method | Endpoint | Purpose |
|---|---|---|
| `FetchMissionActive` | `GET /missions/engine/:id/active` | Full compiled `MissionContract` for the published version. Called when a mission needs to spawn or when the watch poll detects a hash change. |
| `FetchMissionActiveHash` | `GET /missions/engine/:id/active/hash` | Returns only `{ "hash": "..." }`. Cheap to poll. Designer Mode loops on this in `ULunarisMissionSubsystem` and only invokes `FetchMissionActive` when the hash differs from the last known value. |

Both methods are **fire-and-forget** from the caller's perspective: the HTTP request dispatches asynchronously on a worker thread (UE's `FHttpModule`), and the completion callback is marshalled back to the Game Thread automatically.

### Internal helper

`DispatchEngineRequest(MissionId, UrlSuffix, OnComplete)` — shared private builder. Pulls config from `ULunarisSettings`, fail-fast on missing key, constructs `URL = BackendUrl + "/missions/engine/" + MissionId + "/" + UrlSuffix`, sets headers, dispatches. Both public methods are one-liners over this helper. Adding a third Engine endpoint is trivial — call the helper with a new `UrlSuffix`.

---

## Delegate Contract

```
OnComplete(bSuccess = true,  body)  → body is the raw response payload (JSON string, NOT parsed)
OnComplete(bSuccess = false, error) → error is a human-readable string ("HTTP Error 401", "Network/Timeout error.", "EngineApiKey is missing.")
```

The Runtime layer calls `FJsonObjectConverter::JsonObjectStringToUStruct(body, &MyStruct, 0, 0)` and broadcasts `OnMissionSpawned` / `OnMissionFailed` based on the result. **Never put parsing logic here** — it would couple the layer to specific USTRUCTs and break the "Network is reusable for any future endpoint" property.

---

## Mandatory Validation Order

In `OnHttpResponseReceived`, validation runs in this exact order. Skipping a step opens crash paths:

1. `if (!bSuccess || !Response.IsValid())` → timeout, DNS failure, connection drop. The `Response` pointer may be null here; do **not** dereference before this check.
2. `if (Response->GetResponseCode() != 200)` → HTTP-level error (4xx, 5xx). Log the body for diagnostics — never the request headers (would expose the API key).
3. Only then `OnComplete(true, Response->GetContentAsString())`.

This order is enforced by the rule in `.claude/rules/network.md` §4.

---

## Auth & Headers

Every Engine request **must** carry the `x-api-key` header. The key comes from `ULunarisSettings::EngineApiKey`, read via `GetDefault<ULunarisSettings>()` — never hardcoded, never read from environment directly.

**Fail-fast on missing key:** if `EngineApiKey` is empty, the method invokes the delegate with `bSuccess=false` and returns immediately. The HTTP request is never dispatched. This avoids hitting the backend with anonymous requests that would just round-trip to 401.

**Content-Type** is always `application/json` even for `GET` (backends sometimes vary their parser by content-type; cheap to be consistent).

**Timeout** is 10 seconds. Engine endpoints are millisecond-grade in normal operation; anything past 10s is almost certainly a network outage and should fall back to error.

---

## Security & Logging

- **Never** `UE_LOG` the value of `EngineApiKey`. Not even at `Verbose`. Not even truncated. The plaintext key authenticates the entire studio's mission surface — leaking it via a log file is a real incident.
- Log the URL and `MissionId` only. Both are non-sensitive (the URL contains only the public mission slug, not auth material).
- All logs use the `LogLunaris` category, declared in `Lunaris.h`. Never `LogTemp`.
- Response body logging is acceptable on error paths (helps diagnose 4xx/5xx). On success, no body logging — payloads can be large and will spam the log.

---

## Threading

`FHttpModule` dispatches requests on its own worker pool. UE guarantees that the completion callback bound via `OnProcessRequestComplete()` fires on the **Game Thread**. This means:
- The Runtime callback (`HandleMissionFetched`) runs on the Game Thread → safe to spawn actors, touch `UWorld`, call `UGameInstanceSubsystem` methods.
- **No manual marshalling** with `AsyncTask(ENamedThreads::GameThread, ...)` is required or appropriate.
- **Do not** call `Request->WaitForResponse()` or any blocking wait — that would hang the Game Thread.

---

## What Network Must NOT Do

- ❌ Parse JSON into `USTRUCT`s. (Runtime's job, via `FJsonObjectConverter`.)
- ❌ Hold game state. The class is stateless statics; no `TMap`, no caches.
- ❌ Reference `UWorld`, `AActor`, `FStreamableManager`, or anything from `Runtime/`.
- ❌ Inherit from `UObject` or `AActor`. The lifetime is per-request, owned by `FHttpModule`'s internal queue.
- ❌ Inject business logic (rate-limit handling beyond what the backend imposes, retry policies, exponential backoff). If those become needed, they belong in a higher-level wrapper or in the Runtime's call site, not here.
- ❌ Throw exceptions. UE gameplay code does not use exceptions — every failure path goes through the delegate's `bSuccess=false` channel.

---

## Adding a New Engine Endpoint

1. Add a `static void FetchX(...)` to the public class.
2. Implement it as a one-liner over `DispatchEngineRequest(MissionId, TEXT("path"), OnComplete)`.
3. If the response shape requires a different delegate signature (rare — almost everything is `bool + raw body`), declare a new delegate next to `FOnMissionFetchComplete`. If the shape matches, alias the existing one (`using FOnXComplete = FOnMissionFetchComplete;`).
4. Update this skill's API table.
5. Update the backend `mission` (or relevant module) `SKILL.md` to point to the corresponding HTTP route.

---

## Conventions (UE 5.7 idioms)

- `TSharedRef<IHttpRequest, ESPMode::ThreadSafe>` for the request handle (UE 5.x signature).
- `FString::Printf(TEXT("..."), ...)` for URL composition. Never raw concatenation that risks losing the `TEXT()` macro.
- `BindStatic` for completion callbacks — no captures, no lifetime concerns. The handler is a free function, the delegate type carries the payload.
- `Request->SetTimeout(10.f)` is in seconds, not milliseconds.
- `OnCompleteDelegate.ExecuteIfBound(...)` — never `Execute(...)`. Call sites might pass an unbound delegate during shutdown.

---

## Testing

Per `.claude/rules/testing.md`, Network is in scope for unit testing — but the real `FHttpModule` is heavy to mock. Today the layer is covered by the integration of running the plugin against a real backend (manual QA via the MVP_TEST.md walkthrough). Future automation: extract the response-handling logic into a pure helper (`ParseEngineResponse(bSuccess, ResponseCode, Body) → {success, payload}`) and unit-test that against synthetic inputs without hitting `FHttpModule`.

---

## Update this skill when you change Network

Per `CLAUDE.md`'s MANDATORY rule, update this file whenever you:
- add/rename a public `Fetch*` method or its endpoint
- change the delegate contract (signature, semantics of bSuccess)
- alter the validation order in `OnHttpResponseReceived`
- add new headers or change auth/timeout policy
- introduce caching, retries, or backoff (these would be major architectural deviations — discuss before implementing)
- change the security/logging policy around the API key
