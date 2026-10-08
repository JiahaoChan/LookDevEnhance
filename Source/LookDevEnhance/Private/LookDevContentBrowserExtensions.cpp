// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevContentBrowserExtensions.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "LookDevEditor/LookDevAssetEditor.h"
#include "LookDevEnhance.h"
#include "LookDevEnhanceSettings.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

FDelegateHandle FLookDevContentBrowserExtensions::ContentBrowserExtenderDelegateHandle;

void FLookDevContentBrowserExtensions::InstallHooks()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FContentBrowserMenuExtender_SelectedAssets ExtenderDelegate = FContentBrowserMenuExtender_SelectedAssets::CreateStatic(
		&FLookDevContentBrowserExtensions::OnExtendContentBrowserAssetSelectionMenu);

	TArray<FContentBrowserMenuExtender_SelectedAssets>& ExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	ExtenderDelegates.Add(ExtenderDelegate);
	ContentBrowserExtenderDelegateHandle = ExtenderDelegates.Last().GetHandle();
}

void FLookDevContentBrowserExtensions::RemoveHooks()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll(
			[](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
			{
				return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle;
			});
	}
}

TSharedRef<FExtender> FLookDevContentBrowserExtensions::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	// 仅当所有选中资产都能在 LookDev 编辑器中打开时才显示该入口
	const ULookDevEnhanceSettings* Settings = GetDefault<ULookDevEnhanceSettings>();
	bool bAllAssetsSupported = SelectedAssets.Num() > 0;
	for (const FAssetData& Asset : SelectedAssets)
	{
		if (!Settings->CanOpenInLookDevEditor(Asset))
		{
			bAllAssetsSupported = false;
			break;
		}
	}

	if (bAllAssetsSupported)
	{
		// "CommonAssetActions" 分段在资产右键菜单顶部显示为 "Common" 区域
		Extender->AddMenuExtension(
			"CommonAssetActions",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&FLookDevContentBrowserExtensions::PopulateAssetContextMenu, SelectedAssets));
	}

	return Extender;
}

void FLookDevContentBrowserExtensions::PopulateAssetContextMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenInLookDevEditor", "Open in LookDev Editor"),
		LOCTEXT("OpenInLookDevEditorTooltip", "Opens the selected assets in the LookDev editor, previewed inside the configured LookDev scene."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
		FUIAction(FExecuteAction::CreateStatic(&FLookDevContentBrowserExtensions::ExecuteOpenInLookDevEditor, SelectedAssets)),
		NAME_None,
		EUserInterfaceActionType::Button);
}

void FLookDevContentBrowserExtensions::ExecuteOpenInLookDevEditor(TArray<FAssetData> SelectedAssets)
{
	const ULookDevEnhanceSettings* Settings = GetDefault<ULookDevEnhanceSettings>();

	// 未配置 LookDev 场景时无法提供预览环境，提示用户前往项目设置配置
	if (Settings->LookDevScene.IsNull())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoLookDevSceneConfigured", "No LookDev scene is configured. Set one in Project Settings -> Engine -> LookDev Enhance."));
		return;
	}

	TArray<UObject*> AssetsToOpen;
	for (const FAssetData& Asset : SelectedAssets)
	{
		if (UObject* AssetObject = Asset.GetAsset())
		{
			AssetsToOpen.Add(AssetObject);
		}
	}

	ULookDevAssetEditor::OpenLookDevEditorForAssets(AssetsToOpen);
}

#undef LOCTEXT_NAMESPACE
