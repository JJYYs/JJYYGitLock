// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GitLockSettings.generated.h"

/**
 * 
 */
UCLASS(config=GitLock)
class GITLOCK_API UGitLockSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	virtual FName GetCategoryName() const
	{
		return FName(TEXT("Plugins"));
	};
    
    public:
	UPROPERTY(config, EditAnywhere)
	bool EnableWhenStart = true;

	UPROPERTY(config, EditAnywhere)
	FString ServerURL = "http://127.0.0.1:4321";
	
};
