// Copyright Epic Games, Inc. All Rights Reserved.

#include "GitLockCommands.h"

#define LOCTEXT_NAMESPACE "FGitLockModule"

void FGitLockCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "GitLock", "Execute GitLock action", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
