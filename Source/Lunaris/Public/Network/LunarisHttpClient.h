// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

DECLARE_DELEGATE_TwoParams(FOnMissionFetchComplete, bool /*bSuccess*/, const FString& /*JsonString*/);

class LUNARIS_API FLunarisHttpClient
{
public:
	static void FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate);

private:
	static void OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMissionFetchComplete OnCompleteDelegate, FString MissionId);
};