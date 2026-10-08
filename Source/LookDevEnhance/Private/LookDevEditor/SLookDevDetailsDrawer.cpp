// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/SLookDevDetailsDrawer.h"

#include "IDetailsView.h"
#include "LookDevEditor/LookDevAssetToolkit.h"
#include "LookDevEditor/SLookDevCameraBookmarkPanel.h"
#include "LookDevEditor/SLookDevOutlinerPanel.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

void SLookDevDetailsDrawer::Construct(const FArguments& InArgs)
{
	LookDevToolkitWeak = InArgs._LookDevToolkit;

	// 展开/收起动画曲线（宽度展开，仿 ContentDrawer 的尺寸动画）；
	// 构建时若默认展开（ActiveDetailsPanel 初值即环境页）直接跳到动画末尾，不播开场动画
	SlideCurve = SlideAnimation.AddCurve(0.0f, 0.25f, ECurveEaseFunction::CubicOut);
	CachedPanelState = ELookDevDetailsPanel::None;
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		CachedPanelState = Toolkit->GetActiveDetailsPanel();
		if (CachedPanelState != ELookDevDetailsPanel::None)
		{
			SlideAnimation.JumpToEnd();
		}
	}

	// 头部页面切换按钮（ToggleButton 风格的复选框，勾选态跟随 ActiveDetailsPanel）
	auto MakePanelToggleButton = [this](ELookDevDetailsPanel Panel, const FText& Label, const FText& Tooltip) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
			.IsChecked(this, &SLookDevDetailsDrawer::IsPanelChecked, Panel)
			.OnCheckStateChanged(this, &SLookDevDetailsDrawer::OnPanelToggled, Panel)
			.ToolTipText(Tooltip)
			[
				SNew(STextBlock)
				.Text(Label)
			];
	};

	// 右侧入口条的图标按钮：抽屉收起时显示，点击展开到对应页面
	auto MakeStripButton = [this](ELookDevDetailsPanel Panel, const FSlateBrush* Icon, const FText& Tooltip) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.OnClicked_Lambda([this, Panel]()
			{
				if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
				{
					Toolkit->ToggleDetailsPanel(Panel);
				}
				return FReply::Handled();
			})
			.ToolTipText(Tooltip)
			.ContentPadding(4.0f)
			[
				SNew(SImage)
				.Image(Icon)
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	};

	const TSharedRef<SWidget> EnvironmentContent = InArgs._EnvironmentDetailsView.IsValid()
		? InArgs._EnvironmentDetailsView.ToSharedRef()
		: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget);

	const TSharedRef<SWidget> SelectedActorDetailsContent = InArgs._SelectedActorDetailsView.IsValid()
		? InArgs._SelectedActorDetailsView.ToSharedRef()
		: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget);

	// 选中 Actor 页面：组件树在上、详情在下（对齐关卡编辑器 SActorDetails 的 SSplitter 组合）
	const TSharedRef<SWidget> SelectedActorContent = SNew(SSplitter)
		.Orientation(Orient_Vertical)
		+ SSplitter::Slot()
		.Value(0.3f)
		.MinSize(120.0f)
		[
			InArgs._SelectedActorComponentTree.IsValid()
				? InArgs._SelectedActorComponentTree.ToSharedRef()
				: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
		]
		+ SSplitter::Slot()
		.Value(0.7f)
		[
			SelectedActorDetailsContent
		];

	ChildSlot
	[
		SNew(SHorizontalBox)

		// 详情面板（宽度动画展开/收起）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(this, &SLookDevDetailsDrawer::GetPanelWidth)
			.Visibility(this, &SLookDevDetailsDrawer::GetPanelVisibility)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.0f)
				[
					SNew(SVerticalBox)

					// 头部：页面切换按钮 + 收起按钮
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f, 2.0f, 2.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f)
						[
							MakePanelToggleButton(
								ELookDevDetailsPanel::Environment,
								LOCTEXT("EnvironmentPanelLabel", "Environment"),
								LOCTEXT("EnvironmentPanelTooltip", "Show the preview environment actor's editable properties"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f)
						[
							MakePanelToggleButton(
								ELookDevDetailsPanel::SelectedActor,
								LOCTEXT("SelectedActorPanelLabel", "Selected Actor"),
								LOCTEXT("SelectedActorPanelTooltip", "Show the properties of the actor selected in the viewport"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f)
						[
							MakePanelToggleButton(
								ELookDevDetailsPanel::Outliner,
								LOCTEXT("OutlinerPanelLabel", "Outliner"),
								LOCTEXT("OutlinerPanelTooltip", "Show the list of preview actors (useful for actors that are hard to click in the viewport, e.g. effects)"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f)
						[
							MakePanelToggleButton(
								ELookDevDetailsPanel::CameraBookmarks,
								LOCTEXT("BookmarksPanelLabel", "Bookmarks"),
								LOCTEXT("BookmarksPanelTooltip", "Show the camera bookmarks (position, rotation and FOV presets)"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNullWidget::NullWidget
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
							.OnClicked_Lambda([this]()
							{
								if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
								{
									Toolkit->ToggleDetailsPanel(ELookDevDetailsPanel::None);
								}
								return FReply::Handled();
							})
							.ToolTipText(LOCTEXT("CloseDrawerTooltip", "Close the details panel"))
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Symbols.X"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]

					// 内容：详情视图与书签面板按当前页面切换
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex(this, &SLookDevDetailsDrawer::GetActivePageIndex)
						+ SWidgetSwitcher::Slot()
						[
							EnvironmentContent
						]
						+ SWidgetSwitcher::Slot()
						[
							SelectedActorContent
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SLookDevOutlinerPanel)
							.LookDevToolkit(LookDevToolkitWeak)
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SLookDevCameraBookmarkPanel)
							.LookDevToolkit(LookDevToolkitWeak)
						]
					]
				]
			]
		]

		// 右侧入口条：抽屉完全收起后显示，贴视口最右缘、顶部对齐（工具栏下方）
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(2.0f)
			.Visibility(this, &SLookDevDetailsDrawer::GetStripVisibility)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					MakeStripButton(
						ELookDevDetailsPanel::Environment,
						FAppStyle::GetBrush("LevelEditor.Tabs.Details"),
						LOCTEXT("OpenEnvironmentPanelTooltip", "Open the preview environment details panel"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					MakeStripButton(
						ELookDevDetailsPanel::SelectedActor,
						FAppStyle::GetBrush("ClassIcon.Actor"),
						LOCTEXT("OpenSelectedActorPanelTooltip", "Open the selected actor details panel"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					MakeStripButton(
						ELookDevDetailsPanel::Outliner,
						FAppStyle::GetBrush("LevelEditor.Tabs.Outliner"),
						LOCTEXT("OpenOutlinerPanelTooltip", "Open the preview actor list panel"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					MakeStripButton(
						ELookDevDetailsPanel::CameraBookmarks,
						FAppStyle::GetBrush("ClassIcon.CameraActor"),
						LOCTEXT("OpenBookmarksPanelTooltip", "Open the camera bookmarks panel"))
				]
			]
		]
	];
}

void SLookDevDetailsDrawer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	const ELookDevDetailsPanel CurrentState = Toolkit.IsValid() ? Toolkit->GetActiveDetailsPanel() : ELookDevDetailsPanel::None;
	if (CurrentState == CachedPanelState)
	{
		return;
	}

	// 仅在 收起 <-> 展开 切换时播放宽度动画；展开态下切换页面（Environment <-> SelectedActor）不动画。
	// PlayRelative：正向播放则从头播，反向播放中则掉头，已在目标端点则保持——中途反向不会有跳变
	const bool bWasOpen = CachedPanelState != ELookDevDetailsPanel::None;
	const bool bIsOpen = CurrentState != ELookDevDetailsPanel::None;
	if (bWasOpen != bIsOpen)
	{
		SlideAnimation.PlayRelative(AsShared(), bIsOpen);
	}
	CachedPanelState = CurrentState;
}

EVisibility SLookDevDetailsDrawer::GetPanelVisibility() const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	const bool bOpen = Toolkit.IsValid() && Toolkit->GetActiveDetailsPanel() != ELookDevDetailsPanel::None;
	return (bOpen || SlideCurve.GetLerp() > 0.0f) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SLookDevDetailsDrawer::GetStripVisibility() const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	const bool bOpen = Toolkit.IsValid() && Toolkit->GetActiveDetailsPanel() != ELookDevDetailsPanel::None;
	return (!bOpen && SlideCurve.GetLerp() <= 0.0f) ? EVisibility::Visible : EVisibility::Collapsed;
}

FOptionalSize SLookDevDetailsDrawer::GetPanelWidth() const
{
	return FOptionalSize(DrawerWidth * SlideCurve.GetLerp());
}

ECheckBoxState SLookDevDetailsDrawer::IsPanelChecked(ELookDevDetailsPanel Panel) const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	return (Toolkit.IsValid() && Toolkit->GetActiveDetailsPanel() == Panel)
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SLookDevDetailsDrawer::OnPanelToggled(ECheckBoxState NewState, ELookDevDetailsPanel Panel)
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		Toolkit->ToggleDetailsPanel(Panel);
	}
}

int32 SLookDevDetailsDrawer::GetActivePageIndex() const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid())
	{
		return 0;
	}
	switch (Toolkit->GetActiveDetailsPanel())
	{
		case ELookDevDetailsPanel::SelectedActor:
			return 1;
		case ELookDevDetailsPanel::Outliner:
			return 2;
		case ELookDevDetailsPanel::CameraBookmarks:
			return 3;
		case ELookDevDetailsPanel::Environment:
		default:
			return 0;
	}
}

#undef LOCTEXT_NAMESPACE
