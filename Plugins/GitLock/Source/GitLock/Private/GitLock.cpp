// Copyright Epic Games, Inc. All Rights Reserved.

#include "GitLock.h"
#include "GitLockStyle.h"
#include "GitLockCommands.h"
#include "HttpModule.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "GitLockSettings.h"

#include "Json.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"


static const FName GitLockTabName("GitLock");

#define LOCTEXT_NAMESPACE "FGitLockModule"

void FGitLockModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FGitLockStyle::Initialize();
	FGitLockStyle::ReloadTextures();

	FGitLockCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FGitLockCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FGitLockModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGitLockModule::RegisterMenus));

	OnObjectSavedHandle = FCoreUObjectDelegates::OnObjectSaved.AddRaw(this, &FGitLockModule::OnObjectSaved);
	if (GetMutableDefault<UGitLockSettings>()->EnableWhenStart)
	{
		InitGitData();
	}
	ServerURL = GetMutableDefault<UGitLockSettings>()->ServerURL;
}

void FGitLockModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FGitLockStyle::Shutdown();

	FGitLockCommands::Unregister();
	FCoreUObjectDelegates::OnObjectSaved.Remove(OnObjectSavedHandle);
}

void FGitLockModule::PluginButtonClicked()
{
	ProcessingRequestItems.Empty();
	IgnoreItems.Empty();
	if (!EnableLock)
	{
		HTTPRequest_IsReady();
	}
	else
	{
		EnableLock = false;
		auto ret = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("GitLock is turned off. Do you want to unlock all files?")), &Title);
		if (ret == EAppReturnType::Yes)
		{
			HTTPRequest_CleanLock();
		}
	}
}

void FGitLockModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
		UToolMenu* DLToolsMenu = Menu->AddSubMenu(FToolMenuOwner(Menu->GetFName()),FName("JJYYTools"),FName("JJYYTools"),FText::FromString("JJYYTools"));
		FToolMenuSection& Section = DLToolsMenu->FindOrAddSection("JJYYTools");
		Section.AddMenuEntryWithCommandList(FGitLockCommands::Get().PluginAction, PluginCommands);
		
	}

	// {
	// 	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
	// 	{
	// 		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
	// 		{
	// 			FToolMenuEntry& Entry = Section.AddEntry(
	// 				FToolMenuEntry::InitToolBarButton(FGitLockCommands::Get().PluginAction));
	// 			Entry.SetCommandList(PluginCommands);
	// 		}
	// 	}
	// }
}

void FGitLockModule::OnObjectSaved(UObject* SavedObject)
{
	if (IsRunningCommandlet()) return;
	if (!EnableLock) return;
	// Ensure the saved object is a non-UWorld asset (UWorlds are handled separately)
	if (!SavedObject->IsA<UWorld>() && SavedObject->IsAsset())
	{
		if (GEngine->IsAutosaving()) return;
		FString PathName = SavedObject->GetPathName();
		if (PathName.EndsWith("_C")) return;
		FString Path = FPaths::GetPath(PathName);
		Path = Path.Replace(TEXT("/Game"),TEXT("Content"));
		FString Name = FPaths::GetBaseFilename(PathName);
		FString NewPathName = Path + "/" + Name + ".uasset";
		HTTPRequest_SetLock(NewPathName);
	}
	if (SavedObject->IsA<UWorld>() && SavedObject->IsAsset())
	{
		if (GEngine->IsAutosaving()) return;
		FString PathName = SavedObject->GetPathName();
		if (PathName.EndsWith("_C")) return;
		FString Path = FPaths::GetPath(PathName);
		Path = Path.Replace(TEXT("/Game"),TEXT("Content"));
		FString Name = FPaths::GetBaseFilename(PathName);
		FString NewPathName = Path + "/" + Name + ".umap";
		HTTPRequest_SetLock(NewPathName);
	}
}

void FGitLockModule::HTTPRequest_IsReady()
{
	//Create Request
	FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();

	HttpRequest->SetVerb("GET");
	//Set URL
	FString URL = FString::Printf(TEXT("%s/isready"), *ServerURL);
	HttpRequest->SetURL(URL);

	//Bind Callback
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGitLockModule::HandleRequest_IsReady);

	//Time Out
	HttpRequest->SetTimeout(1);

	//Send Request
	HttpRequest->ProcessRequest();
}

void FGitLockModule::HandleRequest_IsReady(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpRequest.IsValid() || !HttpResponse.IsValid())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Connect Server Faild!")), &Title);
		return;
	}
	int32 responseCode = HttpResponse->GetResponseCode();
	if (bSucceeded && EHttpResponseCodes::IsOk(responseCode))
	{
		if (InitGitData())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
								 FText::FromString(
									 FString::Printf(TEXT("Enable GitLock!\nUserName: %s\nBranch: %s\n"), *UserID,*GitBranch)), &Title);
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Initialization error!")), &Title);
		}
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Connect Server Faild!")), &Title);
	}
}

bool FGitLockModule::InitGitData()
{
	int32 ReturnCode;
	FString OutStdOut;
	FString OutStdErr;
	bool getErr = false;
	GitExe = "git";
	{
		FPlatformProcess::ExecProcess(*GitExe, *FString::Printf(TEXT("-C %s config user.name"),*FPaths::ProjectDir()), &ReturnCode, &OutStdOut, &OutStdErr);
		if (ReturnCode == 0)
		{
			UserID = OutStdOut.Replace(TEXT("\n"),TEXT(""));
		}
		else
		{
			getErr = true;
		}
		
		FPlatformProcess::ExecProcess(*GitExe, *FString::Printf(TEXT("-C %s branch --show-current"),*FPaths::ProjectDir()), &ReturnCode, &OutStdOut, &OutStdErr);
		if (ReturnCode == 0)
		{
			GitBranch = OutStdOut.Replace(TEXT("\n"),TEXT(""));
		}
		else
		{
			getErr = true;
		}
		FString param = FString::Printf(TEXT("-C %s "),*FPaths::ProjectDir());
		param+= "log --pretty=format:%H -n 1";
		FPlatformProcess::ExecProcess(*GitExe,*param, &ReturnCode, &OutStdOut,
									  &OutStdErr);
		if (ReturnCode == 0)
		{
			GitHashId = OutStdOut.Replace(TEXT("\n"),TEXT("")).Replace(TEXT("'"),TEXT(""));
		}
		else
		{
			getErr = true;
		}
		if (getErr)
		{
			UE_LOG(LogTemp, Log, TEXT("An error occurred after calling git cmd!"));
		}
		else
		{
			EnableLock = true;
		}
	}
	
	return EnableLock;
}


void FGitLockModule::HTTPRequest_SetLock(const FString& item_id)
{
	if (LockedItems.Find(item_id) > INDEX_NONE) return;
	if (IgnoreItems.Find(item_id) > INDEX_NONE) return;
	if (ProcessingRequestItems.Find(item_id) > INDEX_NONE) return;

	{
		int32 ReturnCode;
		FString OutStdOut;
		FString OutStdErr;
		FString FilePath = FPaths::Combine(FPaths::ProjectDir(),item_id);
		FPlatformProcess::ExecProcess(*GitExe, *FString::Printf(TEXT("-C %s log %s"),*FPaths::ProjectDir(),*FilePath), &ReturnCode, &OutStdOut, &OutStdErr);
		if (ReturnCode == 0)
		{
			if (OutStdOut.Len() == 0)
			{
				//new file
				return;
			}
		}
		else
		{
			//Git error
			return;
		}
	}
	
	auto ret = FMessageDialog::Open(EAppMsgType::YesNo,
														FText::FromString(FString::Printf(
															TEXT("Ask for lock file: \n\n%s"),
															*item_id)), &Title);
	if (ret == EAppReturnType::No)
	{
		IgnoreItems.Add(item_id);
		return;
	}

	

	ProcessingRequestItems.AddUnique(item_id);
	/**json Data**/
	FString Serverdata;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<
		TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Serverdata);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue("item_id", item_id);
	JsonWriter->WriteValue("locked", true);
	JsonWriter->WriteValue("user", UserID);
	JsonWriter->WriteValue("branch", GitBranch);
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();

	//Create Request
	FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();

	//Set Header
	HttpRequest->SetHeader("accept", "application/json;charset=UTF-8");
	HttpRequest->SetHeader("Content-Type", "application/json;charset=UTF-8");

	HttpRequest->SetVerb("POST");
	//Set URL
	FString URL = FString::Printf(TEXT("%s/setlock/%s"), *ServerURL, *GitHashId);
	HttpRequest->SetURL(URL);
	//Set Data
	HttpRequest->SetContentAsString(Serverdata);

	//Bind Callback
	HttpRequest->OnProcessRequestComplete().BindRaw(this, &FGitLockModule::HandleRequest_SetLock);
	//Send Request
	HttpRequest->ProcessRequest();
}

void FGitLockModule::HandleRequest_SetLock(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse,
                                           bool bSucceeded)
{
	if (!HttpRequest.IsValid() || !HttpResponse.IsValid())
	{
		return;
	}

	FString MessageBody = HttpResponse->GetContentAsString();

	FString item_id;
	bool locked = false;
	FString user;
	FString branch;
	bool result = false;
	bool committed = false;

	int32 responseCode = HttpResponse->GetResponseCode();
	if (bSucceeded && EHttpResponseCodes::IsOk(responseCode))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageBody);
		TSharedPtr<FJsonObject> JsonObject;
		TSharedPtr<FJsonObject> TempJson;
		if (FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			item_id = JsonObject->GetStringField("item_id");
			locked = JsonObject->GetBoolField("locked");
			user = JsonObject->GetStringField("user");
			branch = JsonObject->GetStringField("branch");
			result = JsonObject->GetBoolField("result");
			committed = JsonObject->GetBoolField("committed");
			ProcessingRequestItems.Remove(item_id);

			if (!result)
			{
				if (locked && user != UserID)
				{
					if (IgnoreItems.Find(item_id) == INDEX_NONE)
					{
						auto ret = FMessageDialog::Open(EAppMsgType::YesNo,
						                                FText::FromString(FString::Printf(
							                                TEXT("lock failed \n\n%s\n\nHas been locked by {%s} in the {%s} branch \n\nIgnore this file?"),
							                                *item_id, *user,*branch)), &Title);
						if (ret == EAppReturnType::Yes)
						{
							IgnoreItems.AddUnique(item_id);
						}
					}
				}

				if (committed)
				{
					if (IgnoreItems.Find(item_id) == INDEX_NONE)
					{
						auto ret = FMessageDialog::Open(EAppMsgType::YesNo,
														FText::FromString(FString::Printf(
															TEXT("lock failed \n\n%s\n\nA new version has been committed in the {%s} branch by {%s} \n\nIgnore this file?"),
															*item_id,*branch,*user)), &Title);
						if (ret == EAppReturnType::Yes)
						{
							IgnoreItems.AddUnique(item_id);
						}
					}
				}
			}
			else
			{
				LockedItems.AddUnique(item_id);
			}
		}
	}
}

void FGitLockModule::HTTPRequest_CleanLock()
{
	//create request
	FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();

	//Set Header
	HttpRequest->SetHeader("Content-Type", "application/json;charset=UTF-8");

	HttpRequest->SetVerb("POST");
	//Set URL
	FString URL = FString::Printf(TEXT("%s/cleanlock/%s"), *ServerURL, *UserID);
	HttpRequest->SetURL(URL);
	
	//Send Request
	HttpRequest->ProcessRequest();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGitLockModule, GitLock)
