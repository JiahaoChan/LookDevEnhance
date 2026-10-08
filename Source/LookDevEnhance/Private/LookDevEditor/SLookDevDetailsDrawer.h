// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LookDevAssetToolkit.h"
#include "Animation/CurveSequence.h"
#include "Widgets/SCompoundWidget.h"

class FLookDevAssetToolkit;
class IDetailsView;

enum class ELookDevDetailsPanel : uint8;

/**
 * LookDev 编辑器视口内的浮空详情抽屉
 *
 * 仿 ContentDrawer 的浮空面板：覆盖在视口右侧而不参与停靠布局，
 * 视口因此始终保持完整宽高比。内含"环境"（预览环境 Actor）、"选中 Actor"
 * （组件树 + 详情，对齐关卡编辑器 SActorDetails）、"大纲"
 * （预览 Actor 大纲，SLookDevOutlinerPanel）与"摄像机书签"四个页面，
 * 头部按钮切换页面、X 按钮收起；
 * 显隐与当前页面由 FLookDevAssetToolkit::ActiveDetailsPanel 驱动。
 * 展开 / 收起带宽度动画；完全收起后视口最右缘显示一条窄入口列（四个图标按钮），
 * 点击对应图标即展开到对应页面。
 */
class SLookDevDetailsDrawer : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SLookDevDetailsDrawer) {}
		SLATE_ARGUMENT(TWeakPtr<FLookDevAssetToolkit>, LookDevToolkit)
		SLATE_ARGUMENT(TSharedPtr<IDetailsView>, EnvironmentDetailsView)
		SLATE_ARGUMENT(TSharedPtr<IDetailsView>, SelectedActorDetailsView)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, SelectedActorComponentTree)
	SLATE_END_ARGS()

public:
	// 抽屉面板完全展开时的宽度
	static constexpr float DrawerWidth = 450.0f;

private:
	// 所属的 LookDev 工具集，读取 / 切换 ActiveDetailsPanel 状态
	TWeakPtr<FLookDevAssetToolkit> LookDevToolkitWeak;

	// 展开/收起动画（宽度展开，仿 ContentDrawer 的尺寸动画）
	FCurveSequence SlideAnimation;

	// 动画曲线句柄（0=完全收起，1=完全展开）
	FCurveHandle SlideCurve;

	// 上一帧的面板状态，Tick 检测收起 <-> 展开切换以驱动动画（展开态下切换页面不动画）；
	// 初始值在 Construct 中设置（此处只有枚举前向声明，不能用枚举值做默认初始化）
	ELookDevDetailsPanel CachedPanelState = ELookDevDetailsPanel::None;

public:
	/**
	 *	构建控件
	 *
	 *	@param	InArgs	Slate 参数，包含所属工具集与两个详情视图
	 */
	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
    // 面板可见性：展开状态或收起动画进行中（宽度未归零）时可见
    EVisibility GetPanelVisibility() const;

    // 右侧入口条可见性：抽屉完全收起后显示
    EVisibility GetStripVisibility() const;

    // 面板宽度：展开/收起动画驱动（0 ~ DrawerWidth）
    FOptionalSize GetPanelWidth() const;

    // 头部页面切换按钮的勾选态（当前页面按钮按下）
    ECheckBoxState IsPanelChecked(ELookDevDetailsPanel Panel) const;

    // 页面切换按钮点击：委托给工具集的 ToggleDetailsPanel（再点当前页 = 收起）
    void OnPanelToggled(ECheckBoxState NewState, ELookDevDetailsPanel Panel);

    // 内容区当前页面下标（0=环境，1=选中 Actor，2=场景大纲，3=摄像机书签）
    int32 GetActivePageIndex() const;
};
