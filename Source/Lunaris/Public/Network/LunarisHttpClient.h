// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool /*bSuccess*/, const FString& /*JsonString*/);

using FOnMissionHashFetchComplete = FOnMissionFetchComplete;

class LUNARIS_API FLunarisHttpClient
{
public:
	static void FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate);

	static void FetchMissionActiveHash(const FString& MissionId, FOnMissionHashFetchComplete OnCompleteDelegate);

private:
	static void DispatchEngineRequest(const FString& MissionId, const TCHAR* UrlSuffix, FOnMissionFetchComplete OnCompleteDelegate);

	static void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMissionFetchComplete OnCompleteDelegate, FString MissionId);
};
