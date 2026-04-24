// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool /*bSuccess*/, const FString& /*JsonString*/);

// The watch endpoint returns the same delegate shape (bool + raw response body).
// An alias keeps the call site semantically labelled without proliferating delegate
// types or forking the shared response-validation handler.
using FOnMissionHashFetchComplete = FOnMissionFetchComplete;

class LUNARIS_API FLunarisHttpClient
{
public:
	// Fetches the full compiled MissionContract for the published version of the mission.
	// On success the delegate receives the raw JSON body; on failure a human-readable error string.
	static void FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate);

	// Fetches only the activeHash of the published version. Designed for the designer-mode
	// poll loop in ULunarisMissionSubsystem — a hash change triggers a follow-up
	// FetchMissionActive. Same auth, throttle policy and validation pipeline as FetchMissionActive.
	static void FetchMissionActiveHash(const FString& MissionId, FOnMissionHashFetchComplete OnCompleteDelegate);

private:
	// Common request builder for any /missions/engine/:id/<UrlSuffix> endpoint.
	// Pulls config from ULunarisSettings, fail-fast on missing key, dispatches async.
	static void DispatchEngineRequest(const FString& MissionId, const TCHAR* UrlSuffix, FOnMissionFetchComplete OnCompleteDelegate);

	// Shared response handler — validates bSuccess → IsValid → 200 in that order, then delivers
	// the raw response body. Reused by every Engine-surface method above.
	static void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMissionFetchComplete OnCompleteDelegate, FString MissionId);
};
