// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEnhance.h"

#include "LookDevContentBrowserExtensions.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

DEFINE_LOG_CATEGORY(LogLookDevEnhance);

void FLookDevEnhanceModule::StartupModule()
{
	FLookDevContentBrowserExtensions::InstallHooks();
}

void FLookDevEnhanceModule::ShutdownModule()
{
	FLookDevContentBrowserExtensions::RemoveHooks();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLookDevEnhanceModule, LookDevEnhance)
