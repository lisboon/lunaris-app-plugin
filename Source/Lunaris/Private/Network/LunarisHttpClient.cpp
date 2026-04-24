// Copyright 2026, Lunaris. All Rights Reserved.

#include "Network/LunarisHttpClient.h"
#include "Core/LunarisSettings.h"
#include "Lunaris.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

void FLunarisHttpClient::FetchMissionActive(const FString& MissionId, FOnMissionFetchComplete OnCompleteDelegate)
{
	DispatchEngineRequest(MissionId, TEXT("active"), OnCompleteDelegate);
}

void FLunarisHttpClient::FetchMissionActiveHash(const FString& MissionId, FOnMissionHashFetchComplete OnCompleteDelegate)
{
	DispatchEngineRequest(MissionId, TEXT("active/hash"), OnCompleteDelegate);
}

void FLunarisHttpClient::DispatchEngineRequest(const FString& MissionId, const TCHAR* UrlSuffix, FOnMissionFetchComplete OnCompleteDelegate)
{
	if (MissionId.IsEmpty())
	{
		UE_LOG(LogLunaris, Error, TEXT("FLunarisHttpClient: MissionId cannot be empty."));
		OnCompleteDelegate.ExecuteIfBound(false, TEXT("MissionId is empty."));
		return;
	}

	const ULunarisSettings* Settings = GetDefault<ULunarisSettings>();
	if (Settings->EngineApiKey.IsEmpty())
	{
		UE_LOG(LogLunaris, Error, TEXT("FLunarisHttpClient: EngineApiKey is not configured."));
		OnCompleteDelegate.ExecuteIfBound(false, TEXT("EngineApiKey is missing."));
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/missions/engine/%s/%s"), *Settings->BackendUrl, *MissionId, UrlSuffix);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("x-api-key"), Settings->EngineApiKey);
	Request->SetTimeout(10.f);

	Request->OnProcessRequestComplete().BindStatic(&FLunarisHttpClient::OnHttpResponseReceived, OnCompleteDelegate, MissionId);
	Request->ProcessRequest();

	UE_LOG(LogLunaris, Log, TEXT("FLunarisHttpClient: Fetching mission '%s' from %s"), *MissionId, *Url);
}

void FLunarisHttpClient::OnHttpResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMissionFetchComplete OnCompleteDelegate, FString MissionId)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogLunaris, Error, TEXT("FLunarisHttpClient: HTTP request failed for mission '%s'."), *MissionId);
		OnCompleteDelegate.ExecuteIfBound(false, TEXT("HTTP request failed."));
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	if (StatusCode != 200)
	{
		UE_LOG(LogLunaris, Error, TEXT("FLunarisHttpClient: Backend returned HTTP %d. Body: %s"), StatusCode, *Response->GetContentAsString());
		OnCompleteDelegate.ExecuteIfBound(false, FString::Printf(TEXT("HTTP Error %d"), StatusCode));
		return;
	}

	// Sucesso! Retornamos o JSON limpo para quem pediu.
	OnCompleteDelegate.ExecuteIfBound(true, Response->GetContentAsString());
}
