// Copyright 2026, Lunaris. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LunarisTypes.generated.h"

USTRUCT(BlueprintType)
struct FLunarisTargetActor
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
	FString ClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
	FVector SpawnLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FLunarisMissionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
	FString MissionId;

	UPROPERTY(BlueprintReadOnly, Category = "Lunaris")
	FLunarisTargetActor TargetActor;
};