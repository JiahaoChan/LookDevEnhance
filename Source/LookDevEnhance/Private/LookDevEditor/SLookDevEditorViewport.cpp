// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/SLookDevEditorViewport.h"

#include "LookDevEditor/LookDevAssetToolkit.h"
#include "LookDevEditor/LookDevViewportClient.h"
#include "LookDevEditor/SLookDevDetailsDrawer.h"
#include "LumenVisualizationMenuCommands.h"
#include "SSubobjectEditor.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

void SLookDevEditorViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	LookDevToolkitWeak = InArgs._LookDevToolkit;

	// 把视口控件回填给客户端：FEditorViewportClient 的过渡动画（如 F 键聚焦）要求
	// EditorViewportWidget 有效，而该指针只能在控件存在后设置
	if (TSharedPtr<FLookDevViewportClient> LookDevViewportClient = StaticCastSharedPtr<FLookDevViewportClient>(InArgs._EditorViewportClient))
	{
		LookDevViewportClient->SetEditorViewportWidget(StaticCastSharedRef<SEditorViewport>(SharedThis(this)));
	}

	SAssetEditorViewport::FArguments BaseArgs;
	BaseArgs._EditorViewportClient = InArgs._EditorViewportClient;
	SAssetEditorViewport::Construct(BaseArgs, InViewportConstructionArgs);

	// 浮空详情抽屉：在基类构建的视口内容外层包一层 SOverlay，抽屉覆盖在视口右侧
	//（不参与停靠布局，视口保持完整宽高比）；顶部留出视口工具栏的高度，避免遮挡工具栏
	if (const TSharedPtr<FLookDevAssetToolkit> PinnedToolkit = LookDevToolkitWeak.Pin())
	{
		const TSharedRef<SWidget> ViewportContent = ChildSlot.GetWidget();
		ChildSlot
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				ViewportContent
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(0.0f, 44.0f, 6.0f, 6.0f))
			[
				SNew(SLookDevDetailsDrawer)
				.LookDevToolkit(LookDevToolkitWeak)
				.EnvironmentDetailsView(PinnedToolkit->GetEnvironmentDetailsView())
				.SelectedActorDetailsView(PinnedToolkit->GetSelectedActorDetailsView())
				.SelectedActorComponentTree(PinnedToolkit->GetSelectedActorComponentTree())
			]
		];
	}
}

void SLookDevEditorViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();

	// 绑定 Lumen 可视化命令（命令集由 CommonMenuExtensions 模块在启动时注册）
	FLumenVisualizationMenuCommands::Get().BindCommands(*GetCommandList(), GetViewportClient());
}

TSharedPtr<SWidget> SLookDevEditorViewport::BuildViewportToolbar()
{
	const FName ViewportToolbarName = "LookDevEditor.ViewportToolbar";

	// 工具栏菜单全局只需注册一次，所有 LookDev 编辑器窗口共用同一份菜单定义
	if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
	{
		UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None /* parent */, EMultiBoxType::SlimHorizontalToolBar);

		ViewportToolbarMenu->StyleName = "ViewportToolbar";

		{
			FToolMenuSection& LeftSection = ViewportToolbarMenu->AddSection("Left");
			LeftSection.AddEntry(UE::UnrealEd::CreateTransformsSubmenu());
			LeftSection.AddEntry(UE::UnrealEd::CreateSnappingSubmenu());
		}

		{
			FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection("Right");
			RightSection.Alignment = EToolMenuSectionAlign::Last;

			RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));

			// 朝向归位下拉框：把摄像机归位到 6 个轴向之一并按 F 键逻辑聚焦（优先选中，未选中时聚焦全部预览资产）
			RightSection.AddDynamicEntry("LookDevViewDirection", FNewToolMenuSectionDelegate::CreateLambda(
				[](FToolMenuSection& InDynamicSection)
				{
					TWeakPtr<SEditorViewport> WeakViewport;
					if (UUnrealEdViewportToolbarContext* const Context = InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
					{
						WeakViewport = Context->Viewport;
					}

					InDynamicSection.AddEntry(FToolMenuEntry::InitComboButton(
						"ViewDirection",
						FUIAction(),
						FNewToolMenuChoice(FNewToolMenuDelegate::CreateLambda([WeakViewport](UToolMenu* Submenu)
						{
							FToolMenuSection& Section = Submenu->FindOrAddSection("Directions");

							// 预设视角：Default 归位到打开编辑器时的初始机位（与 SpawnPreviewForAssets 共用同一旋转常量）
							Section.AddMenuEntry(
								"ViewDefault",
								LOCTEXT("ViewDefault", "Default"),
								LOCTEXT("ViewDefaultTooltip", "Snap the camera back to the default view direction and focus on the preview assets"),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Perspective"),
								FUIAction(FExecuteAction::CreateLambda([WeakViewport]()
								{
									if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
									{
										if (const TSharedPtr<FLookDevViewportClient> LookDevClient = StaticCastSharedPtr<FLookDevViewportClient>(Viewport->GetViewportClient()))
										{
											LookDevClient->FocusFromViewDirection(FLookDevViewportClient::DefaultViewRotation);
										}
									}
								})));

							// 预设视角：Character Face 聚焦骨骼模型的 head 骨骼（正脸），仅预览单个含 head 骨骼的骨骼网格时可用
							Section.AddMenuEntry(
								"ViewCharacterFace",
								LOCTEXT("ViewCharacterFace", "Character Face"),
								LOCTEXT("ViewCharacterFaceTooltip", "Focus the camera on the head bone of the skeletal mesh (only available when previewing a single skeletal mesh with a head bone)"),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.SkeletalMeshComponent"),
								FUIAction(
									FExecuteAction::CreateLambda([WeakViewport]()
									{
										if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
										{
											if (const TSharedPtr<FLookDevViewportClient> LookDevClient = StaticCastSharedPtr<FLookDevViewportClient>(Viewport->GetViewportClient()))
											{
												LookDevClient->FocusOnHeadBone();
											}
										}
									}),
									FCanExecuteAction::CreateLambda([WeakViewport]()
									{
										if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
										{
											if (const TSharedPtr<FLookDevViewportClient> LookDevClient = StaticCastSharedPtr<FLookDevViewportClient>(Viewport->GetViewportClient()))
											{
												return LookDevClient->CanFocusHeadBone();
											}
										}
										return false;
									})));

							auto AddDirectionEntry = [&Section, WeakViewport](const FName Name, const FText& Label, const FSlateIcon& Icon, const FRotator& Rotation)
							{
								Section.AddMenuEntry(
									Name, Label,
									FText::Format(LOCTEXT("ViewDirectionEntryTooltip", "Snap the camera to the {0} axis and focus on the preview assets"), Label),
									Icon,
									FUIAction(FExecuteAction::CreateLambda([WeakViewport, Rotation]()
									{
										if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
										{
											if (const TSharedPtr<FLookDevViewportClient> LookDevClient = StaticCastSharedPtr<FLookDevViewportClient>(Viewport->GetViewportClient()))
											{
												LookDevClient->FocusFromViewDirection(Rotation);
											}
										}
									})));
							};

							// 旋转定义：X+ 即摄像机位于 +X 一侧看向 -X（正对资产前方），其余轴向同理
							AddDirectionEntry("ViewX+", LOCTEXT("ViewXPos", "X+"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Front"), FRotator(0.0, 180.0, 0.0));
							AddDirectionEntry("ViewX-", LOCTEXT("ViewXNeg", "X-"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Back"), FRotator::ZeroRotator);
							AddDirectionEntry("ViewY+", LOCTEXT("ViewYPos", "Y+"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Right"), FRotator(0.0, -90.0, 0.0));
							AddDirectionEntry("ViewY-", LOCTEXT("ViewYNeg", "Y-"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Left"), FRotator(0.0, 90.0, 0.0));
							AddDirectionEntry("ViewZ+", LOCTEXT("ViewZPos", "Z+"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Top"), FRotator(-90.0, 0.0, 0.0));
							AddDirectionEntry("ViewZ-", LOCTEXT("ViewZNeg", "Z-"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Bottom"), FRotator(90.0, 0.0, 0.0));

							// 15° 微俯视预设：水平轴向在同朝向基础上 Pitch=-15；顶/底视角收敛为 75° 俯/仰（15° 余量）
							AddDirectionEntry("ViewX+15", LOCTEXT("ViewXPos15", "X+15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Front"), FRotator(-15.0, 180.0, 0.0));
							AddDirectionEntry("ViewX-15", LOCTEXT("ViewXNeg15", "X-15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Back"), FRotator(-15.0, 0.0, 0.0));
							AddDirectionEntry("ViewY+15", LOCTEXT("ViewYPos15", "Y+15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Right"), FRotator(-15.0, -90.0, 0.0));
							AddDirectionEntry("ViewY-15", LOCTEXT("ViewYNeg15", "Y-15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Left"), FRotator(-15.0, 90.0, 0.0));
							AddDirectionEntry("ViewZ+15", LOCTEXT("ViewZPos15", "Z+15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Top"), FRotator(-75.0, 0.0, 0.0));
							AddDirectionEntry("ViewZ-15", LOCTEXT("ViewZNeg15", "Z-15"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Bottom"), FRotator(75.0, 0.0, 0.0));
						})),
						LOCTEXT("ViewDirectionLabel", "View Direction"),
						LOCTEXT("ViewDirectionButtonTooltip", "Snap the camera to a chosen axis and focus on the preview assets (uses the same focus logic as the F key)"),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.Perspective"),
						false));
				}));

			// 视图模式：在引擎通用条目上补充 Lumen 可视化子菜单（该子菜单本是关卡编辑器私有扩展，资产编辑器默认没有）
			RightSection.AddDynamicEntry("DynamicViewModes", FNewToolMenuSectionDelegate::CreateLambda(
				[](FToolMenuSection& InDynamicSection)
				{
					// 与引擎的 CreateViewModesSubmenu 一致：标签与图标跟随当前视图模式
					TAttribute<FText> LabelAttribute = UE::UnrealEd::GetViewModesSubmenuLabel(nullptr);
					TAttribute<FSlateIcon> IconAttribute = TAttribute<FSlateIcon>();
					bool bLumenVisualizeChecked = false;
					if (UUnrealEdViewportToolbarContext* const Context = InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
					{
						LabelAttribute = TAttribute<FText>::CreateLambda([WeakViewport = Context->Viewport]()
						{
							return UE::UnrealEd::GetViewModesSubmenuLabel(WeakViewport);
						});
						IconAttribute = TAttribute<FSlateIcon>::CreateLambda([WeakViewport = Context->Viewport]()
						{
							return UE::UnrealEd::GetViewModesSubmenuIcon(WeakViewport);
						});

						if (const TSharedPtr<SEditorViewport> Viewport = Context->Viewport.Pin())
						{
							if (const TSharedPtr<FEditorViewportClient> ViewportClient = Viewport->GetViewportClient())
							{
								bLumenVisualizeChecked = ViewportClient->IsViewModeEnabled(VMI_VisualizeLumen);
							}
						}
					}

					InDynamicSection.AddSubMenu(
						"ViewModes",
						LabelAttribute,
						LOCTEXT("ViewModesSubmenuTooltip", "View mode settings for the current viewport."),
						FNewToolMenuDelegate::CreateLambda(
							[bLumenVisualizeChecked](UToolMenu* Submenu)
							{
								UE::UnrealEd::PopulateViewModesMenu(Submenu);

								FToolMenuSection& Section = Submenu->FindOrAddSection("ViewMode");
								Section.AddSubMenu(
									"VisualizeLumenViewMode",
									LOCTEXT("VisualizeLumenViewModeDisplayName", "Lumen"),
									LOCTEXT("LumenVisualizationMenu_ToolTip", "Select a mode for Lumen visualization"),
									FNewMenuDelegate::CreateStatic(&FLumenVisualizationMenuCommands::BuildVisualisationSubMenu),
									FUIAction(
										FExecuteAction(),
										FCanExecuteAction(),
										FIsActionChecked::CreateLambda([bLumenVisualizeChecked]() { return bLumenVisualizeChecked; })),
									EUserInterfaceActionType::RadioButton,
									/* bInOpenSubMenuOnClick = */ false,
									FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeLumenMode"));
							}),
						false,
						IconAttribute);
				}));

			RightSection.AddEntry(UE::UnrealEd::CreateDefaultShowSubmenu());
			RightSection.AddEntry(UE::UnrealEd::CreatePerformanceAndScalabilitySubmenu());
		}
	}

	FToolMenuContext ViewportToolbarContext;
	ViewportToolbarContext.AppendCommandList(GetCommandList());

	UUnrealEdViewportToolbarContext* const ContextObject = UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this));
	// 把所属工具集放进工具栏上下文，工具栏条目据此访问编辑器状态
	if (TSharedPtr<FLookDevAssetToolkit> PinnedToolkit = LookDevToolkitWeak.Pin())
	{
		ContextObject->AssetEditorToolkit = PinnedToolkit;
	}
	ViewportToolbarContext.AddObject(ContextObject);

	return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

#undef LOCTEXT_NAMESPACE
