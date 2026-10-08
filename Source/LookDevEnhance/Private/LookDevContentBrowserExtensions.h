// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

/**
 * 内容浏览器资产右键菜单扩展
 *
 * 为内容浏览器资产右键菜单的 Common 分段添加“在 LookDev 编辑器中打开”入口，
 * 仅当所有选中资产都属于 LookDev Enhance 设置中启用的类型时才显示。
 * 由 FLookDevEnhanceModule 在模块启动 / 关闭时调用 InstallHooks / RemoveHooks 完成安装与移除。
 */
class FLookDevContentBrowserExtensions
{

private:
	// 已注册的内容浏览器菜单扩展委托句柄，RemoveHooks 据此移除扩展
	static FDelegateHandle ContentBrowserExtenderDelegateHandle;

public:
	/**
	 *	向内容浏览器模块注册资产选中菜单扩展委托，并保存委托句柄以供后续移除
	 */
	static void InstallHooks();

	/**
	 *	按保存的委托句柄从内容浏览器模块移除菜单扩展委托
	 */
	static void RemoveHooks();

private:
	/**
	 *	内容浏览器资产选中菜单的扩展回调，决定是否注入“在 LookDev 编辑器中打开”入口
	 *
	 *	@param	SelectedAssets	当前选中的资产列表
	 *	@return	菜单扩展器，仅当所有选中资产都受支持时才包含扩展入口
	 */
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);

	/**
	 *	向菜单中填充“在 LookDev 编辑器中打开”菜单项
	 *
	 *	@param	MenuBuilder		菜单构建器
	 *	@param	SelectedAssets	当前选中的资产列表，点击菜单项时透传给执行回调
	 */
	static void PopulateAssetContextMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);

	/**
	 *	菜单项点击回调，加载选中资产并在 LookDev 编辑器中打开
	 *
	 *	@param	SelectedAssets	要打开的资产列表；未配置 LookDev 场景时弹出提示并直接返回
	 */
	static void ExecuteOpenInLookDevEditor(TArray<FAssetData> SelectedAssets);
};
