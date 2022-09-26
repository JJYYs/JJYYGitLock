// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "GitLockStyle.h"

class FGitLockCommands : public TCommands<FGitLockCommands>
{
public:

	FGitLockCommands()
		: TCommands<FGitLockCommands>(TEXT("GitLock"), NSLOCTEXT("Contexts", "GitLock", "GitLock Plugin"), NAME_None, FGitLockStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
