// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SAssetEditorViewport.h"

class FLookDevAssetToolkit;

/**
 * LookDev 编辑器的视口控件
 *
 * 基于 SAssetEditorViewport，通过 UToolMenus 构建视口工具栏
 * （变换 / 吸附 / 相机 / 朝向归位 / 视图模式 / 显示标志）。
 * 由 FLookDevAssetToolkit 的视口工厂委托创建。
 */
class SLookDevEditorViewport : public SAssetEditorViewport
{

public:
	SLATE_BEGIN_ARGS(SLookDevEditorViewport) {}
		SLATE_ARGUMENT(TSharedPtr<FEditorViewportClient>, EditorViewportClient)
		SLATE_ARGUMENT(TWeakPtr<FLookDevAssetToolkit>, LookDevToolkit)
	SLATE_END_ARGS()

private:
	// 所属的 LookDev 工具集，组装视口工具栏上下文时使用
	TWeakPtr<FLookDevAssetToolkit> LookDevToolkitWeak;

public:
	/**
	 *	构建控件
	 *
	 *	@param	InArgs						Slate 参数，包含视口客户端与所属工具集
	 *	@param	InViewportConstructionArgs	视口构造参数
	 */
	void Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs);

protected:
	virtual void BindCommands() override;

	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
};
