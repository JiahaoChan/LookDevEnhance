// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/LookDevAssetToolkit.h"

#include "AssetEditorModeManager.h"
#include "AssetSelection.h"
#include "AssetsPreviewActor.h"
#include "ActorFactories/ActorFactory.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformFileManager.h"
#include "HighResScreenshot.h"
#include "Interfaces/IPluginManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LookDevEditor/LookDevAssetEditor.h"
#include "LookDevEditor/LookDevViewportClient.h"
#include "LookDevEditor/SLookDevEditorViewport.h"
#include "LookDevEnhance.h"
#include "LookDevEnhanceSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Guid.h"
#include "Misc/NamePermissionList.h"
#include "Misc/ScopedSlowTask.h"
#include "PreviewScene.h"
#include "PropertyEditorArchetypePolicy.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Selection.h"
#include "ShaderCompiler.h"
#include "Slate/SceneViewport.h"
#include "SSubobjectEditor.h"
#include "SSubobjectInstanceEditor.h"
#include "Styling/AppStyle.h"
#include "SubobjectData.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ToolMenus.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

namespace UE::LookDevEnhance
{
	// 隐藏保存入口的菜单 Profile 名：本编辑器不提供保存（改动只作用于预览世界，保存应回到内容浏览器 / 关卡编辑器进行）。
	// 拒绝列表项按 Profile 名隔离，其他资产编辑器不激活本 Profile、不受影响（机制对齐引擎只读模式，见 RegisterToolbar）
	static const FName NoSaveMenuProfileName = TEXT("LookDevNoSave");

	/**
	 *	请求卸载一个以临时包形式（bLoadAsTempPackage=true）加载的流送关卡
	 *
	 *	@param	Level	要卸载的流送关卡，无效时直接返回
	 *
	 *	@note	除请求卸载外还会清除内部 World、包及其嵌套对象上的 RF_Standalone 标记：
	 *		编辑器模式下引擎的 GC 辅助逻辑会跳过这一步，不清除的话内部 UWorld 会泄漏。
	 *		（与 FMetaHumanCharacterEditorToolkit::RequestStreamingLevelUnload 相同的处理模式）
	 */
	static void RequestStreamingLevelUnload(ULevelStreaming* Level)
	{
		if (!IsValid(Level))
		{
			return;
		}

		if (ULevel* LoadedLevel = Level->GetLoadedLevel())
		{
			if (UWorld* InnerWorld = LoadedLevel->GetTypedOuter<UWorld>())
			{
				InnerWorld->ClearFlags(RF_Standalone | RF_Public);
				if (UPackage* InnerPackage = InnerWorld->GetPackage())
				{
					InnerPackage->ClearFlags(RF_Standalone);
					ForEachObjectWithPackage(InnerPackage, [](UObject* Obj)
						{
							Obj->ClearFlags(RF_Standalone);
							return true;
						}, EGetObjectsFlags::IncludeNestedObjects);
				}
			}
		}

		Level->SetShouldBeLoaded(false);
		Level->SetShouldBeVisibleInEditor(false);
		Level->SetIsRequestingUnloadAndRemoval(true);
	}

	/**
	 *	把详情面板 revert 基准重定向到指定快照对象的 Archetype 策略
	 *
	 *	属性行的 revert 箭头可见性与回写值都走 FPropertyNode::GetArchetype，其优先查询
	 *	PropertyEditorPolicy 注册的 IArchetypePolicy（PropertyEditorArchetypePolicy.h）；
	 *	策略是全局注册的，必须按对象指针精确过滤，只对被编辑对象返回快照，其余返回 nullptr
	 *	落到默认 CDO 逻辑。与 LevelInstance 属性覆盖（LevelInstanceEditorModule）同款做法
	 */
	class FSnapshotArchetypePolicy : public PropertyEditorPolicy::IArchetypePolicy
	{
	public:
		FSnapshotArchetypePolicy(const UObject* InEditedObject, UObject* InSnapshot)
			: EditedObject(InEditedObject)
			, Snapshot(InSnapshot)
		{
		}

		virtual UObject* GetArchetypeForObject(const UObject* Object) const override
		{
			return Object == EditedObject ? Snapshot.Get() : nullptr;
		}

	private:
		// 被编辑的对象（流送进预览世界的预览环境 Actor）
		TWeakObjectPtr<const UObject> EditedObject;

		// revert 基准快照（打开时复制的场景实例状态）
		TWeakObjectPtr<UObject> Snapshot;
	};
}

const FName FLookDevAssetToolkit::LookDevToolkitFName(TEXT("LookDevEditor"));

FLookDevAssetToolkit::FLookDevAssetToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit(InOwningAssetEditor)
{
	// 监听预览环境 Actor 的属性变化，把预览旋转偏移的增量实时同步到预览 Actor
	EnvironmentPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
		this, &FLookDevAssetToolkit::OnEnvironmentPropertyChanged);

	// 单栏布局：视口占满整窗；环境 / 选中 Actor 详情改为视口内的浮空抽屉（SLookDevDetailsDrawer），
	// 不参与停靠布局，避免侵占视口宽高比
	StandaloneDefaultLayout = FTabManager::NewLayout(FName(TEXT("Standalone_LookDevEditor_Layout_v2")))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.0f)
				->AddTab(ViewportTabID, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
		);
}

FLookDevAssetToolkit::~FLookDevAssetToolkit()
{
	// 缓存收尾：停止防抖轮询并兜底写盘（必须在 UnloadLookDevScene 之前，环境 Actor 与视口仍有效）
	if (EditorCacheTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorCacheTickerHandle);
	}
	FlushEditorCache();

	// 注销 revert 基准的 Archetype 策略并释放快照（策略是全局注册，必须先于场景卸载注销）
	if (EnvironmentArchetypePolicy.IsValid())
	{
		PropertyEditorPolicy::UnregisterArchetypePolicy(EnvironmentArchetypePolicy.Get());
		EnvironmentArchetypePolicy.Reset();
	}
	EnvironmentSnapshot.Reset();

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(EnvironmentPropertyChangedHandle);

	// 截图完成回调若仍在订阅（截图途中关闭编辑器），退订防野回调
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
	}

	if (EditorModeManager.IsValid())
	{
		EditorModeManager->GetSelectedComponents()->SelectionChangedEvent.RemoveAll(this);
	}

	// 解除预览组件上的选中判定委托绑定：委托裸持视口客户端指针（CreateRaw），须在预览世界销毁前解除
	if (const TSharedPtr<FLookDevViewportClient> LookDevViewportClient = StaticCastSharedPtr<FLookDevViewportClient>(ViewportClient))
	{
		LookDevViewportClient->ClearSelectionOutlineOverrides();
	}

	UnloadLookDevScene();
}

void FLookDevAssetToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);

	// 详情面板只存在于视口浮空抽屉中，注销基类的 Details 停靠 Tab，
	// 避免从 Window 菜单再生成一个持有同一 DetailsView 的停靠页（同一控件不能同时挂在两处）
	InTabManager->UnregisterTabSpawner(DetailsTabID);
}

void FLookDevAssetToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::UnregisterTabSpawners(InTabManager);
}

void FLookDevAssetToolkit::CreateWidgets()
{
	FBaseAssetToolkit::CreateWidgets();

	SelectedActorDetailsView = CreateSelectedActorDetailsView();

	// 组件树：对齐关卡编辑器 SActorDetails 的组合方式（SActorDetails.cpp:503-510），
	// ObjectContext 为当前视口选中的预览 Actor；预览世界不允许增删组件，关闭编辑能力
	SelectedActorComponentTree = SNew(SSubobjectInstanceEditor)
		.ObjectContext(this, &FLookDevAssetToolkit::GetComponentTreeObjectContext)
		.AllowEditing(false)
		.OnSelectionUpdated(this, &FLookDevAssetToolkit::OnComponentTreeSelectionUpdated);

	// 视口选择变化时刷新"选中 Actor"详情面板
	if (EditorModeManager.IsValid())
	{
		EditorModeManager->GetSelectedComponents()->SelectionChangedEvent.AddRaw(this, &FLookDevAssetToolkit::OnPreviewSelectionChanged);
	}
}

FName FLookDevAssetToolkit::GetToolkitFName() const
{
	return LookDevToolkitFName;
}

FText FLookDevAssetToolkit::GetBaseToolkitName() const
{
	if (EditedAssets.Num() > 0 && EditedAssets[0].IsValid())
	{
		if (EditedAssets.Num() > 1)
		{
			return FText::Format(LOCTEXT("ToolkitNameMulti", "{0} (+{1} more)"),
				FText::FromString(EditedAssets[0]->GetName()), FText::AsNumber(EditedAssets.Num() - 1));
		}
		return FText::FromString(EditedAssets[0]->GetName());
	}
	return LOCTEXT("ToolkitName", "LookDev Editor");
}

FText FLookDevAssetToolkit::GetToolkitName() const
{
	// 复用 GetBaseToolkitName 的多资产实现（"{0} (+{1} more)"），绕开基类 GetEditingObject 的单资产断言
	return GetBaseToolkitName();
}

FText FLookDevAssetToolkit::GetToolkitToolTipText() const
{
	// 与 GetToolkitName 一样绕开基类的单资产断言：单资产用基类静态的对象 tooltip（含资产路径），多资产逐行列出资产名；
	// 基于本类的 EditedAssets（基类 EditingObjects 在 5.8 为 private 不可直接访问）
	if (EditedAssets.Num() == 1 && EditedAssets[0].IsValid())
	{
		return GetToolTipTextForObject(EditedAssets[0].Get());
	}

	TArray<FString> AssetNames;
	AssetNames.Reserve(EditedAssets.Num());
	for (const TWeakObjectPtr<UObject>& Asset : EditedAssets)
	{
		if (Asset.IsValid())
		{
			AssetNames.Add(Asset->GetName());
		}
	}
	return FText::FromString(FString::Join(AssetNames, TEXT("\n")));
}

void FLookDevAssetToolkit::SetEditingObject(UObject* InObject)
{
	// revert 基准快照：复制场景实例打开时（缓存应用之前）的状态并注册 Archetype 策略，
	// 详情面板的 revert 箭头与回写以该快照为基准；须在 DetailsView->SetObject 之前完成，
	// 属性节点的 diff 缓存按注册时的 archetype 建立
	if (PreviewEnvironmentActor.IsValid())
	{
		EnvironmentSnapshot = TStrongObjectPtr<AAssetsPreviewActor>(DuplicateObject(PreviewEnvironmentActor.Get(), GetTransientPackage()));
		EnvironmentArchetypePolicy = MakeShared<UE::LookDevEnhance::FSnapshotArchetypePolicy>(PreviewEnvironmentActor.Get(), EnvironmentSnapshot.Get());
		PropertyEditorPolicy::RegisterArchetypePolicy(EnvironmentArchetypePolicy.Get());
	}

	// 细节面板展示 LookDev 场景中的预览环境 Actor，而不是被预览的资产本身
	DetailsView->SetObject(PreviewEnvironmentActor.Get());

	// 基类只传入第一个对象，完整的资产列表需要从拥有编辑器上取回
	EditedAssets.Reset();
	TArray<UObject*> Assets;
	if (const ULookDevAssetEditor* LookDevEditor = Cast<ULookDevAssetEditor>(OwningAssetEditor))
	{
		for (const TObjectPtr<UObject>& Asset : LookDevEditor->GetEditedAssets())
		{
			if (Asset)
			{
				EditedAssets.Add(Asset);
				Assets.Add(Asset);
			}
		}
	}
	if (Assets.Num() == 0 && InObject)
	{
		EditedAssets.Add(InObject);
		Assets.Add(InObject);
	}

	// 缓存恢复：环境属性必须在生成预览之前应用（SpawnPreviewForAssets 的初始朝向偏移、
	// 相机恢复与副本重建都依赖缓存值）；无历史缓存时全部走默认值
	EditorCacheKey = FLookDevEditorCache::MakeCacheKey(Assets);
	bHasLoadedCacheEntry = !EditorCacheKey.IsEmpty() && FLookDevEditorCache::LoadEntry(EditorCacheKey, LoadedCacheEntry);
	if (bHasLoadedCacheEntry)
	{
		ApplyCachedEnvironment(LoadedCacheEntry);
		MultiViewSlotMask = LoadedCacheEntry.MultiViewSlotMask;
		MultiViewCopySpacing = LoadedCacheEntry.MultiViewCopySpacing;

		// 缓存值落地后刷新详情面板，让 revert 箭头按新值与快照基准重新计算
		DetailsView->ForceRefresh();
	}

	// 书签按资产 Key 独立序列化，打开时读回（版本号递增让书签面板重建列表）
	CameraBookmarks = FLookDevEditorCache::LoadCameraBookmarks(EditorCacheKey);
	++CameraBookmarksVersion;

	SpawnPreviewForAssets(Assets);

	// 上次关闭时已生成多视图副本：按缓存的角度掩码自动重建，窗口完全还原上次状态
	if (bHasLoadedCacheEntry && LoadedCacheEntry.bHasMultiViewCopies && LoadedCacheEntry.MultiViewSlotMask != 0)
	{
		GenerateMultiViewCopies();
	}
	else if (bHasLoadedCacheEntry)
	{
		// 排布间距同时驱动原型行：无副本可恢复时按缓存间距重排原型（Spawn 用的是默认 50cm 边距）
		RelayoutOriginalPreviewActors();
	}

	// 记录基线快照（避免恢复后立即重写一次相同数据），并启动防抖轮询写盘
	if (!EditorCacheKey.IsEmpty())
	{
		if (EditorCacheTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(EditorCacheTickerHandle);
		}
		LastSavedCacheEntry = CaptureCacheEntry();
		bHasLastSavedCacheEntry = true;
		EditorCacheTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &FLookDevAssetToolkit::TickEditorCache), 0.5f);
	}
}

void FLookDevAssetToolkit::InitToolMenuContext(FToolMenuContext& MenuContext)
{
	FBaseAssetToolkit::InitToolMenuContext(MenuContext);

	// 无条件激活"无保存"菜单 Profile（注册见 RegisterToolbar）：隐藏工具栏 Save 按钮与 File 菜单的 Save / Save As
	UToolMenuProfileContext* ProfileContext = NewObject<UToolMenuProfileContext>();
	ProfileContext->ActiveProfiles.Add(UE::LookDevEnhance::NoSaveMenuProfileName);
	MenuContext.AddObject(ProfileContext);
}

void FLookDevAssetToolkit::RegisterToolbar()
{
	// 本编辑器的工具栏菜单名为 AssetEditor.<ToolkitFName>.ToolBar（FAssetEditorToolkit::GetToolMenuToolbarName），
	// 按类型命名、被所有编辑器实例共享：整个 LookDev 区块注册为动态条目，
	// 每个窗口生成自己的工具栏时回调各执行一次，按菜单上下文找回本实例后再绑定委托（详见 FillToolbarSection）；
	// 若在注册时直接 CreateSP(this)，最后注册的实例会被冻结进共享菜单，多开窗口时按钮串台
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(TEXT("AssetEditor.LookDevEditor.ToolBar"));
	ToolbarMenu->FindOrAddSection(TEXT("LookDev")).AddDynamicEntry(
		TEXT("LookDevToolbar"),
		FNewToolMenuSectionDelegate::CreateStatic(&FLookDevAssetToolkit::FillToolbarSection));

	// 隐藏保存入口：对齐引擎只读模式的菜单 Profile 机制（AssetEditorToolkit.cpp 的 InitializeReadOnlyMenuProfiles）——
	// 按菜单名注册带拒绝列表的 Profile，激活在 InitToolMenuContext 按实例进行；
	// 拒绝项按 Profile 名隔离，其他资产编辑器不激活本 Profile、不受影响。
	// ①共享的工具栏通用操作区（AssetEditorToolbar.CommonActions）的 Save 按钮；②本编辑器 File 菜单的 Save / Save As。
	// 注意 FAssetEditorCommonCommands 的头文件在 UnrealEd 私有目录不可引用，命令名按 UI_COMMAND 注册名硬编码
	UToolMenus* ToolMenus = UToolMenus::Get();
	const FName SaveAssetCommandName = TEXT("SaveAsset");
	const FName SaveAssetAsCommandName = TEXT("SaveAssetAs");

	FNamePermissionList NoSaveToolbarPermissions;
	NoSaveToolbarPermissions.AddDenyListItem(UE::LookDevEnhance::NoSaveMenuProfileName, SaveAssetCommandName);
	if (FToolMenuProfile* CommonActionsProfile = ToolMenus->AddRuntimeMenuProfile(TEXT("AssetEditorToolbar.CommonActions"), UE::LookDevEnhance::NoSaveMenuProfileName))
	{
		CommonActionsProfile->MenuPermissions = NoSaveToolbarPermissions;
	}

	FNamePermissionList NoSaveFileMenuPermissions;
	NoSaveFileMenuPermissions.AddDenyListItem(UE::LookDevEnhance::NoSaveMenuProfileName, SaveAssetCommandName);
	NoSaveFileMenuPermissions.AddDenyListItem(UE::LookDevEnhance::NoSaveMenuProfileName, SaveAssetAsCommandName);
	const FName FileSubmenuName = *(GetToolMenuName().ToString() + TEXT(".File"));
	if (FToolMenuProfile* FileMenuProfile = ToolMenus->AddRuntimeMenuProfile(FileSubmenuName, UE::LookDevEnhance::NoSaveMenuProfileName))
	{
		FileMenuProfile->MenuPermissions = NoSaveFileMenuPermissions;
	}
}

void FLookDevAssetToolkit::FillToolbarSection(FToolMenuSection& InSection)
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = GetToolkitFromToolMenu(InSection);
	if (!Toolkit.IsValid())
	{
		return;
	}

	// 生成按钮做成复合下拉框：点主按钮直接生成副本；下拉内是角度槽位的多选复选框，取消勾选的角度不生成
	InSection.AddEntry(FToolMenuEntry::InitComboButton(
		"GenerateMultiViewCopies",
		FUIAction(FExecuteAction::CreateSP(Toolkit.ToSharedRef(), &FLookDevAssetToolkit::GenerateMultiViewCopies)),
		FNewToolMenuChoice(FNewToolMenuDelegate::CreateSP(Toolkit.ToSharedRef(), &FLookDevAssetToolkit::FillMultiViewAnglesMenu)),
		LOCTEXT("GenerateMultiViewCopiesLabel", "Multi-View Copies"),
		LOCTEXT("GenerateMultiViewCopiesTooltip", "Generate a ring of rotated multi-view copies for the selected preview assets (all assets when nothing is selected); uncheck angles in the dropdown to skip them; existing copies are cleared and regenerated, so angle and spacing changes take effect immediately"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Duplicate"),
		false));

	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		"ClearMultiViewCopies",
		FExecuteAction::CreateSP(Toolkit.ToSharedRef(), &FLookDevAssetToolkit::ClearMultiViewCopies),
		LOCTEXT("ClearMultiViewCopiesLabel", "Clear Copies"),
		LOCTEXT("ClearMultiViewCopiesTooltip", "Remove all multi-view rotation copies"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")));

	// 高清截图按钮做成复合下拉框：点主按钮直接截图；下拉内是分辨率预设单选与水印开关
	InSection.AddEntry(FToolMenuEntry::InitComboButton(
		"CaptureHighResScreenshot",
		FUIAction(FExecuteAction::CreateSP(Toolkit.ToSharedRef(), &FLookDevAssetToolkit::CaptureHighResScreenshot)),
		FNewToolMenuChoice(FNewToolMenuDelegate::CreateSP(Toolkit.ToSharedRef(), &FLookDevAssetToolkit::FillScreenshotSettingsMenu)),
		LOCTEXT("CaptureHighResScreenshotLabel", "Screenshot"),
		LOCTEXT("CaptureHighResScreenshotTooltip", "Capture a high-resolution screenshot of the viewport (center-cropped to 16:9); resolution and watermark options are in the dropdown"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraActor"),
		false));
}

FLinearColor FLookDevAssetToolkit::GetDefaultTabColor() const
{
	return FLinearColor(0.8f, 0.8f, 0.8f);
}

TSharedPtr<FEditorViewportClient> FLookDevAssetToolkit::CreateEditorViewportClient() const
{
	const ULookDevEnhanceSettings* Settings = GetDefault<ULookDevEnhanceSettings>();
	const bool bHasLookDevScene = Settings && !Settings->LookDevScene.IsNull();

	// 光照由 LookDev 场景提供，仅在未配置场景时才回退到预览世界自带的默认灯光；
	// 开启 Lumen 图元追踪，与引擎官方预览编辑器（如 MetaHuman 编辑器）的预览世界配置对齐
	FPreviewScene* PreviewScene = new FPreviewScene(FPreviewScene::ConstructionValues()
		.SetCreateDefaultLighting(!bHasLookDevScene)
		.SetTransactional(false)
		.AllowLumenPrimitiveTrackingInPreviewWorld(true));
	PreviewScenePtr = PreviewScene;

	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(PreviewScene);

	if (bHasLookDevScene)
	{
		// 关卡式启动流程：带进度条分步加载场景，并等待着色器编译完成再出现窗口，
		// 避免首帧渲染使用热编译的 PSO（新编译的 RT 着色器立即 dispatch 会导致 GPU 挂死）
		FScopedSlowTask SlowTask(100.f, LOCTEXT("OpeningLookDevEditor", "Opening LookDev Editor..."));
		SlowTask.MakeDialog();

		SlowTask.EnterProgressFrame(60.f, LOCTEXT("LoadingLookDevScene", "Loading LookDev scene..."));
		LoadLookDevScene();

		SlowTask.EnterProgressFrame(40.f, LOCTEXT("CompilingLookDevShaders", "Compiling shaders..."));
		FShaderCompilingManager* ShaderCompilingManager = GShaderCompilingManager;
		if (ShaderCompilingManager != nullptr)
		{
			ShaderCompilingManager->FinishAllCompilation();
		}
	}

	// 启用 ITF 视口交互，让变换 Gizmo 驱动 ModeManager 的选择集
	if (UModeManagerInteractiveToolsContext* ToolsContext = GetEditorModeManager().GetInteractiveToolsContext())
	{
		ToolsContext->SetViewportInteractionsEnabled(true);
	}

	// 该视口客户端把点击选择（进而变换 Gizmo）限制在预览 Actor 上
	TSharedPtr<FLookDevViewportClient> NewViewportClient = MakeShared<FLookDevViewportClient>(EditorModeManager.Get(), PreviewScene, &PreviewActors, DeferredRTShadows);

	return NewViewportClient;
}

AssetEditorViewportFactoryFunction FLookDevAssetToolkit::GetViewportDelegate()
{
	return [this](const FAssetEditorViewportConstructionArgs& InArgs) -> TSharedRef<SAssetEditorViewport>
	{
		return SNew(SLookDevEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient)
			.LookDevToolkit(StaticCastSharedRef<FLookDevAssetToolkit>(AsShared()));
	};
}

TSharedRef<IDetailsView> FLookDevAssetToolkit::CreateDetailsView()
{
	return CreateDetailsViewInternal(/*bHideEnvironmentUnrelatedCategories=*/ true);
}

TSharedRef<IDetailsView> FLookDevAssetToolkit::CreateSelectedActorDetailsView()
{
	return CreateDetailsViewInternal(/*bHideEnvironmentUnrelatedCategories=*/ false);
}

TSharedRef<IDetailsView> FLookDevAssetToolkit::CreateDetailsViewInternal(bool bHideEnvironmentUnrelatedCategories)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	// 对齐关卡编辑器中"已放置实例"的详情视图（SActorDetails）：隐藏名称区标题头、隐藏 EditDefaultsOnly 属性
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;

	TSharedRef<IDetailsView> NewDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	NewDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([bHideEnvironmentUnrelatedCategories](const FPropertyAndParent& PropertyAndParent)
	{
		if (PropertyAndParent.Property.HasAllPropertyFlags(CPF_DisableEditOnInstance))
		{
			return false;
		}

		// 选中 Actor 详情保留全部内置类别（含 FActorDetails 的 Transform 定制），仅环境详情做过滤
		if (!bHideEnvironmentUnrelatedCategories)
		{
			return true;
		}

		// 隐藏与 LookDev 无关的内置类别（含结构体成员的父属性链，类别按顶层分组名比较）
		static const FName HiddenCategories[] = { TEXT("Actor"), TEXT("Rendering"), TEXT("LevelInstance"), TEXT("Transform") };
		auto IsInHiddenCategory = [](const FProperty& Property)
		{
			FString TopLevelCategory = Property.GetMetaData(TEXT("Category"));
			int32 SeparatorIndex = INDEX_NONE;
			if (TopLevelCategory.FindChar(TEXT('|'), SeparatorIndex))
			{
				TopLevelCategory.LeftInline(SeparatorIndex);
			}

			// 没有 Category 元数据的属性会归入声明类的同名默认分组（如 AActor → "Actor"）
			if (TopLevelCategory.IsEmpty())
			{
				if (const UClass* OwnerClass = Property.GetOwnerClass())
				{
					TopLevelCategory = OwnerClass->GetName();
				}
			}

			for (const FName& HiddenCategory : HiddenCategories)
			{
				if (TopLevelCategory == HiddenCategory.ToString())
				{
					return true;
				}
			}
			return false;
		};

		if (IsInHiddenCategory(PropertyAndParent.Property))
		{
			return false;
		}
		for (const FProperty* ParentProperty : PropertyAndParent.ParentProperties)
		{
			if (IsInHiddenCategory(*ParentProperty))
			{
				return false;
			}
		}

		return true;
	}));

	if (bHideEnvironmentUnrelatedCategories)
	{
		// Actor 的 Transform 区块由 ActorDetails 定制器的自定义构建器生成（类别名 TransformCommon），
		// 属性元数据过滤拦不住，需用自定义行可见性委托按类别名隐藏
		NewDetailsView->SetIsCustomRowVisibleDelegate(FIsCustomRowVisible::CreateLambda([](FName InRowName, FName InParentName)
		{
			static const TSet<FName> HiddenCustomRowParents = { TEXT("TransformCommon"), TEXT("Actor"), TEXT("Rendering"), TEXT("LevelInstance") };
			return !HiddenCustomRowParents.Contains(InParentName);
		}));
	}

	return NewDetailsView;
}

void FLookDevAssetToolkit::GenerateMultiViewCopies()
{
	if (PreviewScenePtr == nullptr)
	{
		return;
	}

	// 唯一化保护：已存在副本时先全部清除再重新生成，角度勾选与间距的修改立即生效
	if (MultiViewCopyActors.Num() > 0)
	{
		ClearMultiViewCopies();
	}

	// 确定源 Actor：优先选中的预览组件所属 Actor，未选中时取全部预览 Actor
	TArray<AActor*> SourceActors;
	if (EditorModeManager.IsValid())
	{
		USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
		for (FSelectionIterator It(*SelectedComponents); It; ++It)
		{
			USceneComponent* Component = Cast<USceneComponent>(*It);
			AActor* Owner = Component ? Component->GetOwner() : nullptr;
			if (Owner != nullptr && PreviewActors.Contains(Owner) && !SourceActors.Contains(Owner))
			{
				SourceActors.Add(Owner);
			}
		}
	}
	if (SourceActors.Num() == 0)
	{
		for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
		{
			if (AActor* ValidActor = Actor.Get())
			{
				SourceActors.Add(ValidActor);
			}
		}
	}
	if (SourceActors.Num() == 0)
	{
		return;
	}

	// 收集本次启用的角度槽位（原型朝向 +X 即 Yaw 0 占中心槽位，副本从两侧最近的 ±45° 开始逐个向外递增；
	// 槽位角度定义与启用状态见 GetMultiViewCopySlotYaw / IsMultiViewCopySlotEnabled）
	TArray<int32> EnabledSlotIndices;
	for (int32 ViewIndex = 0; ViewIndex < NumMultiViewCopySlots; ++ViewIndex)
	{
		if (IsMultiViewCopySlotEnabled(ViewIndex))
		{
			EnabledSlotIndices.Add(ViewIndex);
		}
	}
	if (EnabledSlotIndices.IsEmpty())
	{
		return;
	}

	// 副本的 packed 槽位序号（跳过原型占据的 0；N 为奇数时左右不对称，如 7 个副本为 -3..+4）
	const int32 NumCopiesPerSource = EnabledSlotIndices.Num();
	auto ComputePackedIndex = [NumCopiesPerSource](int32 CopyOrdinal)
	{
		int32 PackedIndex = CopyOrdinal - NumCopiesPerSource / 2;
		if (PackedIndex >= 0)
		{
			PackedIndex += 1;
		}
		return PackedIndex;
	};
	// 行带按槽位的实际左右伸展量计算（LeftExtent/RightExtent），不按对称最大序号——
	// 否则奇数副本数时行带一侧会留出空带，多资产行带拼接处出现空位
	const int32 MinPackedIndex = ComputePackedIndex(0);
	const int32 MaxPackedIndex = ComputePackedIndex(NumCopiesPerSource - 1);
	// 行带总宽 = 左右伸展量之和 = ((Max - Min) * 2 + 2) * HalfSpacing
	const int32 RowWidthFactor = (MaxPackedIndex - MinPackedIndex) * 2 + 2;

	// 多源全局排布：源集合覆盖全部预览 Actor（未选择 = 全部）时，每个源占一个 Y 轴向行带、
	// 所有行带整体以原点为中心依次排开（相邻行带间净距恰为 MultiViewCopySpacing，已含在 HalfSpacing 内），
	// 原型 Actor 迁就偏移到各自行带内（行带左侧伸展量处），避免多个资产的副本行互相重叠；
	// 只选中部分 Actor 生成时保持各源原位（当前位置为中心排副本），由用户自行控制
	int32 ValidPreviewCount = 0;
	for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
	{
		if (Actor.IsValid())
		{
			++ValidPreviewCount;
		}
	}
	const bool bGlobalLayout = ValidPreviewCount > 0 && SourceActors.Num() == ValidPreviewCount;

	float TotalRowWidth = 0.0f;
	if (bGlobalLayout)
	{
		for (const AActor* SourceActor : SourceActors)
		{
			const FBox Bounds = SourceActor->GetComponentsBoundingBox(/*bNonColliding=*/ true);
			// 间距允许负值（副本重叠压紧），HalfSpacing 钳最小 1cm 防止槽位完全坍缩、顺序翻转
			const float HalfSpacing = FMath::Max(FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y) + MultiViewCopySpacing * 0.5f, 1.0f);
			TotalRowWidth += RowWidthFactor * HalfSpacing;
		}
	}
	float BandCursor = -TotalRowWidth * 0.5f;

	for (AActor* SourceActor : SourceActors)
	{
		// 从映射找回源资产，副本复用 CreatePreviewActorForAsset 链路重新生成，
		// ActorFactory / 自定义兜底 / PostSpawn 特殊化与原预览保持一致
		const TWeakObjectPtr<UObject>* SourceAssetPtr = PreviewActorAssets.Find(SourceActor);
		if (SourceAssetPtr == nullptr || !SourceAssetPtr->IsValid())
		{
			UE_LOG(LogLookDevEnhance, Warning, TEXT("Preview actor '%s' has no recorded source asset; skipping multi-view copies."), *SourceActor->GetName());
			continue;
		}
		UObject* SourceAsset = SourceAssetPtr->Get();

		// 以 Actor 为单位测量：全部组件的包围盒 + Actor 位置
		const FBox SourceBounds = SourceActor->GetComponentsBoundingBox(/*bNonColliding=*/ true);
		// HalfSpacing = 包围盒半径 + 间距的一半，使相邻副本包围盒之间的净距离恰好等于 MultiViewCopySpacing；
		// 间距允许负值（副本重叠压紧），钳最小 1cm 防止槽位完全坍缩、顺序翻转
		const float HalfSpacing = FMath::Max(FMath::Max(SourceBounds.GetExtent().X, SourceBounds.GetExtent().Y) + MultiViewCopySpacing * 0.5f, 1.0f);
		FVector SourceLocation = SourceActor->GetActorLocation();

		// 全局排布时把原型迁就偏移到所属行带内（行带左侧伸展量处；行带 = 原型 + 副本的完整 Y 轴占位）
		if (bGlobalLayout)
		{
			const float LeftExtent = (-MinPackedIndex * 2 + 1) * HalfSpacing;
			SourceLocation.Y = BandCursor + LeftExtent;
			BandCursor += RowWidthFactor * HalfSpacing;
			SourceActor->SetActorLocation(SourceLocation);
		}

		// 动作序列 / 蒙太奇经 UActorFactoryAnimationAsset 以单节点模式播放（EAnimationMode::AnimationSingleNode），
		// 副本是全新生成的组件、进度从 0 开始；生成前捕获本体当前的播放状态，副本生成后逐一同步
		float SourceAnimTime = 0.0f;
		float SourceAnimPlayRate = 1.0f;
		bool bSourceAnimPlaying = true;
		bool bSyncAnimation = false;
		const USkeletalMeshComponent* SourceSkeletalMeshComponent = SourceActor->FindComponentByClass<USkeletalMeshComponent>();
		if (SourceSkeletalMeshComponent != nullptr)
		{
			if (const UAnimSingleNodeInstance* SourceAnim = SourceSkeletalMeshComponent->GetSingleNodeInstance())
			{
				SourceAnimTime = SourceAnim->GetCurrentTime();
				SourceAnimPlayRate = SourceAnim->GetPlayRate();
				bSourceAnimPlaying = SourceAnim->IsPlaying();
				bSyncAnimation = true;
			}
		}

		for (int32 CopyOrdinal = 0; CopyOrdinal < EnabledSlotIndices.Num(); ++CopyOrdinal)
		{
			const int32 ViewIndex = EnabledSlotIndices[CopyOrdinal];

			AActor* CopyActor = nullptr;
			USceneComponent* CopyComponent = CreatePreviewActorForAsset(SourceAsset, CopyActor);
			if (CopyActor == nullptr || CopyComponent == nullptr)
			{
				continue;
			}

			// 副本的播放进度 / 速率 / 播放状态对齐本体，全部副本与本体保持一致
			if (bSyncAnimation)
			{
				if (USkeletalMeshComponent* CopySkelComp = CopyActor->FindComponentByClass<USkeletalMeshComponent>())
				{
					if (UAnimSingleNodeInstance* CopyAnim = CopySkelComp->GetSingleNodeInstance())
					{
						CopyAnim->SetPlayRate(SourceAnimPlayRate);
						CopyAnim->SetPosition(SourceAnimTime, /*bFireNotifies=*/ false);
						CopyAnim->SetPlaying(bSourceAnimPlaying);
					}
				}
			}

			// 副本沿 Y 轴以原型为中心向两侧对称陈列。位置槽位紧凑排列：未勾选的槽位不留空位，
			// 后续副本依次补位（位置序号同样跳过原型占据的 0）；旋转始终绑定角度槽位本身
			const float SlotYaw = GetMultiViewCopySlotYaw(ViewIndex);
			const int32 PackedSlotIndex = ComputePackedIndex(CopyOrdinal);
			const float Center = SourceLocation.Y + PackedSlotIndex * 2.0f * HalfSpacing;
			CopyActor->SetActorLocation(FVector(SourceLocation.X, Center, SourceLocation.Z));
			// 副本朝向 = 槽位角度 + 环境配置的预览旋转偏移（正面不朝 +X 的资产经偏移校正后，副本同样跟随）
			CopyActor->SetActorRotation(FRotator(0.0f, SlotYaw, 0.0f) + GetPreviewRotationOffset());

			PreviewActors.Add(CopyActor);
			MultiViewCopyActors.Add(CopyActor);
			// 副本同样登记映射，对副本再次生成副本（副本的副本）时仍能找回源资产
			PreviewActorAssets.Add(CopyActor, SourceAsset);
			// 登记排布记录：间距变化时 RelayoutMultiViewCopies 据此纯位置重排（不参与生成决策）
			FLookDevMultiViewCopyLayout CopyLayout;
			CopyLayout.SourceActor = SourceActor;
			CopyLayout.PackedSlotIndex = PackedSlotIndex;
			MultiViewCopyLayouts.Add(CopyActor, CopyLayout);
		}
	}

	// 通知大纲面板重建列表（Tick 轮询版本号）
	++PreviewActorsVersion;
}

void FLookDevAssetToolkit::ClearMultiViewCopies()
{
	const bool bHadCopies = MultiViewCopyActors.Num() > 0;

	for (const TWeakObjectPtr<AActor>& CopyActor : MultiViewCopyActors)
	{
		AActor* Actor = CopyActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}

		// 跟踪以 Actor 为单位：直接从预览 Actor 集合移除，组件随 Actor->Destroy 一并销毁
		PreviewActors.Remove(Actor);
		PreviewActorAssets.Remove(Actor);
		Actor->Destroy();
	}
	MultiViewCopyActors.Reset();
	MultiViewCopyLayouts.Reset();

	// 确实清过副本时才恢复原预览 Actor 的标准排布（全局行带排布曾挪动原型）；
	// 无副本时不碰，避免误清会话中的手动拖拽位
	if (bHadCopies)
	{
		RelayoutOriginalPreviewActors();
	}

	// 通知大纲面板重建列表（Tick 轮询版本号）
	++PreviewActorsVersion;
}

float FLookDevAssetToolkit::GetMultiViewCopySlotYaw(int32 SlotArrayIndex)
{
	// 槽位号跳过原型占据的 0：数组下标 0..7 -> 槽位 -4..-1 / +1..+4
	int32 SlotIndex = SlotArrayIndex - NumMultiViewCopySlots / 2;
	if (SlotIndex >= 0)
	{
		SlotIndex += 1;
	}
	return SlotIndex * 45.0f;
}

bool FLookDevAssetToolkit::IsMultiViewCopySlotEnabled(int32 SlotArrayIndex) const
{
	return (MultiViewSlotMask & (1u << SlotArrayIndex)) != 0;
}

void FLookDevAssetToolkit::SetMultiViewCopySlotEnabled(int32 SlotArrayIndex, bool bEnabled)
{
	if (bEnabled)
	{
		MultiViewSlotMask |= static_cast<uint8>(1u << SlotArrayIndex);
	}
	else
	{
		MultiViewSlotMask &= static_cast<uint8>(~(1u << SlotArrayIndex));
	}
}

void FLookDevAssetToolkit::SetMultiViewCopySpacing(float InSpacing)
{
	// 允许负值：副本包围盒互相重叠、排布压紧；布局侧对 HalfSpacing 钳最小值防止槽位坍缩
	MultiViewCopySpacing = InSpacing;
	// 间距立即生效：有副本时纯位置重排（不销毁重建，动画与手动调整保留），无副本时重排原型行
	RelayoutMultiViewCopies();
}

void FLookDevAssetToolkit::ToggleDetailsPanel(ELookDevDetailsPanel Panel)
{
	// 再次点击当前已打开的页面 = 收起抽屉
	ActiveDetailsPanel = (ActiveDetailsPanel == Panel) ? ELookDevDetailsPanel::None : Panel;
}

TArray<AActor*> FLookDevAssetToolkit::GetSelectedPreviewActors() const
{
	TArray<AActor*> SelectedActors;
	if (!EditorModeManager.IsValid())
	{
		return SelectedActors;
	}

	// 选中组件所属的预览 Actor，按 Owner 去重（场景 Actor 在视口客户端中被锁定，不会进入选择集）
	USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
	for (FSelectionIterator It(*SelectedComponents); It; ++It)
	{
		if (const UActorComponent* Component = Cast<UActorComponent>(*It))
		{
			AActor* Owner = Component->GetOwner();
			if (Owner != nullptr && PreviewActors.Contains(Owner) && !SelectedActors.Contains(Owner))
			{
				SelectedActors.Add(Owner);
			}
		}
	}
	return SelectedActors;
}

bool FLookDevAssetToolkit::IsMultiViewCopyActor(const AActor* Actor) const
{
	return MultiViewCopyActors.Contains(Actor);
}

FText FLookDevAssetToolkit::GetPreviewActorDisplayLabel(const AActor* Actor) const
{
	if (Actor == nullptr)
	{
		return FText::GetEmpty();
	}

	const FString Label = Actor->GetActorLabel();
	if (!IsMultiViewCopyActor(Actor))
	{
		return FText::FromString(Label);
	}

	// 副本在行名后追加相对原型的朝向角（副本朝向 = 槽位角度 + 环境配置的预览旋转偏移，扣除偏移还原槽位角）
	const float RelativeYaw = FRotator::NormalizeAxis(Actor->GetActorRotation().Yaw - GetPreviewRotationOffset().Yaw);
	return FText::FromString(FString::Printf(TEXT("%s (%+.0f°)"), *Label, RelativeYaw));
}

UObject* FLookDevAssetToolkit::GetPreviewActorSourceAsset(const AActor* Actor) const
{
	const TWeakObjectPtr<UObject>* SourceAssetPtr = PreviewActorAssets.Find(const_cast<AActor*>(Actor));
	return (SourceAssetPtr != nullptr && SourceAssetPtr->IsValid()) ? SourceAssetPtr->Get() : nullptr;
}

void FLookDevAssetToolkit::SelectPreviewActors(const TArray<AActor*>& Actors)
{
	if (!EditorModeManager.IsValid())
	{
		return;
	}

	// 选中各 Actor 的根组件（与视口点选 Actor 等价）；USelection::Select 不广播，
	// 末尾 NoteSelectionChanged 手动广播一次，详情 / 组件树 / gizmo 枢轴 / 选择描边由 OnPreviewSelectionChanged 统一刷新
	USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
	SelectedComponents->DeselectAll();
	for (AActor* Actor : Actors)
	{
		if (Actor != nullptr && PreviewActors.Contains(Actor))
		{
			SelectedComponents->Select(Actor->GetRootComponent());
		}
	}
	SelectedComponents->NoteSelectionChanged();
}

void FLookDevAssetToolkit::FocusOnSelection()
{
	// 保持当前视口朝向聚焦选中项：ComputeFocusBounds 优先选中的预览组件，与 F 键聚焦同一套逻辑
	if (const TSharedPtr<FLookDevViewportClient> LookDevViewportClient = StaticCastSharedPtr<FLookDevViewportClient>(ViewportClient))
	{
		LookDevViewportClient->FocusFromViewDirection(LookDevViewportClient->GetViewRotation());
	}
}

void FLookDevAssetToolkit::CaptureHighResScreenshot()
{
	// 编辑器视口的 FViewport 实例必为 FSceneViewport；引擎正在处理上一张高清截图时不并发
	FSceneViewport* SceneViewport = (ViewportClient.IsValid() && ViewportClient->Viewport)
		? StaticCast<FSceneViewport*>(ViewportClient->Viewport)
		: nullptr;
	if (!SceneViewport || GIsHighResScreenshot)
	{
		return;
	}

	const FIntPoint ViewportSize = SceneViewport->GetRenderTargetTextureSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return;
	}

	// 视口内居中的 16:9 最大裁剪区（渲染目标像素坐标），输出固定为预设的传统分辨率
	const FIntPoint TargetSize = GetScreenshotResolutionSize(ScreenshotResolutionPreset);
	const float TargetAspect = static_cast<float>(TargetSize.X) / static_cast<float>(TargetSize.Y);
	float CropWidth = static_cast<float>(ViewportSize.X);
	float CropHeight = CropWidth / TargetAspect;
	if (CropHeight > ViewportSize.Y)
	{
		CropHeight = static_cast<float>(ViewportSize.Y);
		CropWidth = CropHeight * TargetAspect;
	}
	const float CropX = (ViewportSize.X - CropWidth) * 0.5f;
	const float CropY = (ViewportSize.Y - CropHeight) * 0.5f;

	// 整视口按倍数高清渲染，CaptureRegion 取放大后的裁剪区
	//（与关卡编辑器 High Resolution Screenshot 对话框的内部调用一致：SetResolution + CaptureRegion + TakeHighResScreenShot）
	const float Multiplier = TargetSize.Y / CropHeight;
	FHighResScreenshotConfig& HighResConfig = GetHighResScreenshotConfig();
	HighResConfig.SetMaskEnabled(false);
	if (!HighResConfig.SetResolution(
		FMath::RoundToInt(ViewportSize.X * Multiplier),
		FMath::RoundToInt(ViewportSize.Y * Multiplier)))
	{
		return;
	}
	HighResConfig.CaptureRegion = FIntRect(
		FMath::RoundToInt(CropX * Multiplier),
		FMath::RoundToInt(CropY * Multiplier),
		FMath::RoundToInt((CropX + CropWidth) * Multiplier),
		FMath::RoundToInt((CropY + CropHeight) * Multiplier));

	// 输出目录与时间戳文件名；FilenameOverride 带路径时引擎原样使用、不追加序号，扩展名由引擎按 HDR 与否补全
	const FString ScreenshotDir = FPaths::ScreenShotDir();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ScreenshotDir))
	{
		PlatformFile.CreateDirectoryTree(*ScreenshotDir);
	}
	const FString BaseFilePath = ScreenshotDir / FString::Printf(TEXT("LookDevScreenshot_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	HighResConfig.SetFilename(BaseFilePath);
	PendingScreenshotPath = BaseFilePath + (SceneViewport->GetSceneHDREnabled() ? TEXT(".exr") : TEXT(".png"));

	// 一次性订阅截图完成委托：引擎写盘结束（完成通知已由其自身弹出）后在回调里叠加水印
	ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddSP(
		StaticCastSharedRef<FLookDevAssetToolkit>(AsShared()), &FLookDevAssetToolkit::OnScreenshotProcessed);

	SceneViewport->TakeHighResScreenShot();
}

bool FLookDevAssetToolkit::IsScreenshotResolutionPreset(ELookDevScreenshotResolution Preset) const
{
	return ScreenshotResolutionPreset == Preset;
}

void FLookDevAssetToolkit::SetScreenshotResolutionPreset(ELookDevScreenshotResolution Preset)
{
	ScreenshotResolutionPreset = Preset;
}

void FLookDevAssetToolkit::ToggleScreenshotWatermark()
{
	bScreenshotWatermarkEnabled = !bScreenshotWatermarkEnabled;
}

void FLookDevAssetToolkit::AddCameraBookmark(const FString& DesiredName)
{
	if (EditorCacheKey.IsEmpty() || !ViewportClient.IsValid())
	{
		return;
	}

	FLookDevCameraBookmark Bookmark;
	Bookmark.Name = MakeUniqueCameraBookmarkName(DesiredName, INDEX_NONE);
	Bookmark.Location = ViewportClient->GetViewLocation();
	Bookmark.Rotation = ViewportClient->GetViewRotation();
	Bookmark.FOV = ViewportClient->ViewFOV;
	Bookmark.ThumbnailFileName = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png");
	CameraBookmarks.Add(Bookmark);

	// 以当前视口画面捕获缩略图；失败时保留条目（面板对缺失文件显示占位图）
	CaptureCameraBookmarkThumbnail(CameraBookmarks.Last());

	SaveCameraBookmarks();
}

void FLookDevAssetToolkit::RemoveCameraBookmark(int32 Index)
{
	if (!CameraBookmarks.IsValidIndex(Index))
	{
		return;
	}

	const FString ThumbnailPath = FLookDevEditorCache::GetCameraBookmarkThumbnailPath(CameraBookmarks[Index]);
	if (!ThumbnailPath.IsEmpty())
	{
		IFileManager::Get().Delete(*ThumbnailPath);
	}
	CameraBookmarks.RemoveAt(Index);

	SaveCameraBookmarks();
}

void FLookDevAssetToolkit::RenameCameraBookmark(int32 Index, const FString& DesiredName)
{
	if (!CameraBookmarks.IsValidIndex(Index))
	{
		return;
	}

	// 重名时自动追加序号；缩略图文件名是 GUID，重命名无需改动文件
	CameraBookmarks[Index].Name = MakeUniqueCameraBookmarkName(DesiredName, Index);

	SaveCameraBookmarks();
}

void FLookDevAssetToolkit::ApplyCameraBookmark(int32 Index)
{
	if (!CameraBookmarks.IsValidIndex(Index) || !ViewportClient.IsValid())
	{
		return;
	}

	const FLookDevCameraBookmark& Bookmark = CameraBookmarks[Index];
	ViewportClient->SetViewLocation(Bookmark.Location);
	ViewportClient->SetViewRotation(Bookmark.Rotation);
	ViewportClient->ViewFOV = Bookmark.FOV;
	ViewportClient->FOVAngle = Bookmark.FOV;

	// 缩略图可能已过时：跳到书签机位后立刻按新画面覆盖写回缩略图
	//（文件名不变、ini 无需重写，但递增版本号让面板重载图片）
	if (CaptureCameraBookmarkThumbnail(Bookmark))
	{
		++CameraBookmarksVersion;
	}
}

void FLookDevAssetToolkit::FillMultiViewAnglesMenu(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->FindOrAddSection("MultiViewAngles", LOCTEXT("MultiViewAnglesSectionLabel", "View Angles"));

	for (int32 SlotArrayIndex = 0; SlotArrayIndex < NumMultiViewCopySlots; ++SlotArrayIndex)
	{
		// 角度标签带正负号（+45° / -45°），纯数字无需本地化
		const FText Label = FText::FromString(FString::Printf(TEXT("%+.0f°"), GetMultiViewCopySlotYaw(SlotArrayIndex)));
		const FName EntryName = *FString::Printf(TEXT("MultiViewAngle%d"), SlotArrayIndex);

		FToolMenuEntry& Entry = Section.AddMenuEntry(
			EntryName,
			Label,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, SlotArrayIndex]()
				{
					SetMultiViewCopySlotEnabled(SlotArrayIndex, !IsMultiViewCopySlotEnabled(SlotArrayIndex));
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, SlotArrayIndex]()
				{
					return IsMultiViewCopySlotEnabled(SlotArrayIndex);
				})),
			EUserInterfaceActionType::ToggleButton);

		// 勾选复选框后不关闭菜单，允许连续调整多个角度
		Entry.bShouldCloseWindowAfterMenuSelection = false;
	}

	// 副本间距输入框：相邻副本包围盒之间的净距离（cm），逐资产序列化进编辑器缓存
	FToolMenuSection& SpacingSection = Menu->FindOrAddSection("MultiViewSpacing", LOCTEXT("MultiViewSpacingSectionLabel", "Spacing"));
	SpacingSection.AddEntry(FToolMenuEntry::InitWidget(
		"MultiViewCopySpacing",
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultiViewCopySpacingLabel", "Spacing (cm)"))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(80.0f)
			[
				SNew(SSpinBox<float>)
				.Value(this, &FLookDevAssetToolkit::GetMultiViewCopySpacing)
				.OnValueChanged(this, &FLookDevAssetToolkit::SetMultiViewCopySpacing)
				.MaxSliderValue(500.0f)
				.Delta(5.0f)
				.ToolTipText(LOCTEXT("MultiViewCopySpacingTooltip", "Distance between adjacent actors' bounding boxes (originals and copies), in centimeters; negative values overlap; changes apply immediately"))
			]
		],
		FText::GetEmpty(),
		/*bInNoIndent=*/ true));
}

void FLookDevAssetToolkit::FillScreenshotSettingsMenu(UToolMenu* Menu)
{
	FToolMenuSection& ResolutionSection = Menu->FindOrAddSection("ScreenshotResolution", LOCTEXT("ScreenshotResolutionSectionLabel", "Resolution"));

	// 标签为纯分辨率数字（1K / 2K / 4K），无需本地化
	struct FScreenshotResolutionOption
	{
		ELookDevScreenshotResolution Preset;
		const TCHAR* Label;
	};
	static const FScreenshotResolutionOption ResolutionOptions[] =
	{
		{ ELookDevScreenshotResolution::Res1K, TEXT("1K (1920 x 1080)") },
		{ ELookDevScreenshotResolution::Res2K, TEXT("2K (2560 x 1440)") },
		{ ELookDevScreenshotResolution::Res4K, TEXT("4K (3840 x 2160)") },
	};
	for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(ResolutionOptions); ++OptionIndex)
	{
		const ELookDevScreenshotResolution Preset = ResolutionOptions[OptionIndex].Preset;
		FToolMenuEntry& Entry = ResolutionSection.AddMenuEntry(
			*FString::Printf(TEXT("ScreenshotResolution%d"), OptionIndex),
			FText::FromString(ResolutionOptions[OptionIndex].Label),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FLookDevAssetToolkit::SetScreenshotResolutionPreset, Preset),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FLookDevAssetToolkit::IsScreenshotResolutionPreset, Preset)),
			EUserInterfaceActionType::RadioButton);

		// 切换预设后不关闭菜单，与角度复选框行为一致
		Entry.bShouldCloseWindowAfterMenuSelection = false;
	}

	FToolMenuSection& WatermarkSection = Menu->FindOrAddSection("ScreenshotWatermark");
	FToolMenuEntry& WatermarkEntry = WatermarkSection.AddMenuEntry(
		"ScreenshotWatermark",
		LOCTEXT("ScreenshotWatermarkLabel", "Watermark"),
		LOCTEXT("ScreenshotWatermarkTooltip", "Overlay the plugin's Resources/LookDevWatermark.png at the bottom-right corner of the screenshot"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FLookDevAssetToolkit::ToggleScreenshotWatermark),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FLookDevAssetToolkit::IsScreenshotWatermarkEnabled)),
		EUserInterfaceActionType::ToggleButton);
	WatermarkEntry.bShouldCloseWindowAfterMenuSelection = false;
}

TSharedPtr<FLookDevAssetToolkit> FLookDevAssetToolkit::GetToolkitFromToolMenu(const UToolMenu* Menu)
{
	if (Menu == nullptr)
	{
		return nullptr;
	}

	if (const UAssetEditorToolkitMenuContext* ToolkitContext = Menu->FindContext<UAssetEditorToolkitMenuContext>())
	{
		if (TSharedPtr<FAssetEditorToolkit> Toolkit = ToolkitContext->Toolkit.Pin())
		{
			if (Toolkit->GetToolkitFName() == LookDevToolkitFName)
			{
				return StaticCastSharedPtr<FLookDevAssetToolkit>(Toolkit);
			}
		}
	}
	return nullptr;
}

TSharedPtr<FLookDevAssetToolkit> FLookDevAssetToolkit::GetToolkitFromToolMenu(const FToolMenuSection& Section)
{
	const UAssetEditorToolkitMenuContext* ToolkitContext = Section.FindContext<UAssetEditorToolkitMenuContext>();
	if (ToolkitContext == nullptr)
	{
		return nullptr;
	}

	if (TSharedPtr<FAssetEditorToolkit> Toolkit = ToolkitContext->Toolkit.Pin())
	{
		if (Toolkit->GetToolkitFName() == LookDevToolkitFName)
		{
			return StaticCastSharedPtr<FLookDevAssetToolkit>(Toolkit);
		}
	}
	return nullptr;
}

void FLookDevAssetToolkit::OnScreenshotProcessed()
{
	// 一次性订阅：触发即退订
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
		ScreenshotProcessedHandle.Reset();
	}

	// 还原本工具集对全局截图配置的改动，避免污染关卡编辑器高清截图对话框的后续行为
	FHighResScreenshotConfig& HighResConfig = GetHighResScreenshotConfig();
	HighResConfig.FilenameOverride.Empty();
	HighResConfig.CaptureRegion = FIntRect();

	if (bScreenshotWatermarkEnabled && !PendingScreenshotPath.IsEmpty())
	{
		ApplyScreenshotWatermark(PendingScreenshotPath);
	}
	PendingScreenshotPath.Empty();
}

void FLookDevAssetToolkit::ApplyScreenshotWatermark(const FString& ScreenshotPath) const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LookDevEnhance"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString WatermarkPath = Plugin->GetBaseDir() / TEXT("Resources/Icon128.png");
	if (!FPaths::FileExists(WatermarkPath))
	{
		UE_LOG(LogLookDevEnhance, Verbose, TEXT("LookDev screenshot watermark skipped: %s not found"), *WatermarkPath);
		return;
	}

	FImage ScreenshotImage;
	FImage WatermarkImage;
	if (!FImageUtils::LoadImage(*ScreenshotPath, ScreenshotImage) || !FImageUtils::LoadImage(*WatermarkPath, WatermarkImage))
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("LookDev screenshot watermark skipped: failed to load %s or %s"), *ScreenshotPath, *WatermarkPath);
		return;
	}

	// 统一到 BGRA8 像素域做合成
	FImage ScreenshotBGRA;
	ScreenshotImage.CopyTo(ScreenshotBGRA, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FImage WatermarkBGRA;
	WatermarkImage.CopyTo(WatermarkBGRA, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	// 水印高度取截图短边的 4%，右 / 下边距取短边的 2%（高分辨率预设下水印同步放大）
	const int32 ShortSide = FMath::Min(ScreenshotBGRA.SizeX, ScreenshotBGRA.SizeY);
	const int32 Margin = FMath::Max(2, FMath::RoundToInt(ShortSide * 0.02f));
	const int32 TargetHeight = FMath::Max(8, FMath::RoundToInt(ShortSide * 0.04f));
	const float WatermarkAspect = static_cast<float>(WatermarkBGRA.SizeX) / FMath::Max(1, WatermarkBGRA.SizeY);
	const int32 TargetWidth = FMath::Clamp(FMath::RoundToInt(TargetHeight * WatermarkAspect), 1, ScreenshotBGRA.SizeX / 2);

	FImage ResizedWatermark;
	WatermarkBGRA.ResizeTo(ResizedWatermark, TargetWidth, TargetHeight, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	const int32 OriginX = ScreenshotBGRA.SizeX - TargetWidth - Margin;
	const int32 OriginY = ScreenshotBGRA.SizeY - TargetHeight - Margin;
	if (OriginX < 0 || OriginY < 0)
	{
		return;
	}

	// 右下角 alpha 混合（over 算子），输出不透明
	FColor* DstPixels = reinterpret_cast<FColor*>(ScreenshotBGRA.RawData.GetData());
	const FColor* SrcPixels = reinterpret_cast<const FColor*>(ResizedWatermark.RawData.GetData());
	for (int32 Y = 0; Y < TargetHeight; ++Y)
	{
		for (int32 X = 0; X < TargetWidth; ++X)
		{
			const FColor& Src = SrcPixels[Y * TargetWidth + X];
			if (Src.A == 0)
			{
				continue;
			}

			FColor& Dst = DstPixels[(OriginY + Y) * ScreenshotBGRA.SizeX + (OriginX + X)];
			if (Src.A == 255)
			{
				Dst = Src;
				Dst.A = 255;
				continue;
			}

			const float Alpha = Src.A / 255.0f;
			Dst.R = FMath::RoundToInt(Src.R * Alpha + Dst.R * (1.0f - Alpha));
			Dst.G = FMath::RoundToInt(Src.G * Alpha + Dst.G * (1.0f - Alpha));
			Dst.B = FMath::RoundToInt(Src.B * Alpha + Dst.B * (1.0f - Alpha));
			Dst.A = 255;
		}
	}

	FImageView ResultView(ScreenshotBGRA.RawData.GetData(), ScreenshotBGRA.SizeX, ScreenshotBGRA.SizeY, ERawImageFormat::BGRA8);
	if (!FImageUtils::SaveImageByExtension(*ScreenshotPath, ResultView))
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("LookDev screenshot watermark failed: could not rewrite %s"), *ScreenshotPath);
	}
}

FIntPoint FLookDevAssetToolkit::GetScreenshotResolutionSize(ELookDevScreenshotResolution Preset)
{
	switch (Preset)
	{
		case ELookDevScreenshotResolution::Res1K:
			return FIntPoint(1920, 1080);
		case ELookDevScreenshotResolution::Res4K:
			return FIntPoint(3840, 2160);
		case ELookDevScreenshotResolution::Res2K:
		default:
			return FIntPoint(2560, 1440);
	}
}

FString FLookDevAssetToolkit::MakeUniqueCameraBookmarkName(const FString& DesiredName, int32 ExcludeIndex) const
{
	FString BaseName = DesiredName.TrimStartAndEnd();
	if (BaseName.IsEmpty())
	{
		BaseName = LOCTEXT("DefaultBookmarkName", "Bookmark").ToString();
	}

	auto IsNameTaken = [this, ExcludeIndex](const FString& Name)
	{
		for (int32 Index = 0; Index < CameraBookmarks.Num(); ++Index)
		{
			if (Index != ExcludeIndex && CameraBookmarks[Index].Name == Name)
			{
				return true;
			}
		}
		return false;
	};

	if (!IsNameTaken(BaseName))
	{
		return BaseName;
	}
	for (int32 Suffix = 2;; ++Suffix)
	{
		const FString Candidate = FString::Printf(TEXT("%s (%d)"), *BaseName, Suffix);
		if (!IsNameTaken(Candidate))
		{
			return Candidate;
		}
	}
}

bool FLookDevAssetToolkit::CaptureCameraBookmarkThumbnail(const FLookDevCameraBookmark& Bookmark) const
{
	const FString ThumbnailPath = FLookDevEditorCache::GetCameraBookmarkThumbnailPath(Bookmark);
	if (ThumbnailPath.IsEmpty() || !ViewportClient.IsValid() || ViewportClient->Viewport == nullptr)
	{
		return false;
	}

	// 编辑器视口的 FViewport 实例必为 FSceneViewport
	FSceneViewport* SceneViewport = StaticCast<FSceneViewport*>(ViewportClient->Viewport);
	const FIntPoint ViewportSize = SceneViewport->GetRenderTargetTextureSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}

	// 与 FEditorViewportClient::TakeScreenshot 相同的同步捕获路径：先重绘再读像素，
	// 在按钮点击等渲染帧外的时机调用，无需等待下一帧
	ViewportClient->Invalidate(false, true);
	SceneViewport->Draw();

	TArray<FColor> RawPixels;
	RawPixels.SetNum(ViewportSize.X * ViewportSize.Y);
	if (!SceneViewport->ReadPixels(RawPixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect(0, 0, ViewportSize.X, ViewportSize.Y)))
	{
		return false;
	}

	// 视口中心正方形裁剪，避免直接缩放把非方形视口的画面压扁
	const int32 SquareSide = FMath::Min(ViewportSize.X, ViewportSize.Y);
	const int32 OriginX = (ViewportSize.X - SquareSide) / 2;
	const int32 OriginY = (ViewportSize.Y - SquareSide) / 2;

	FImage SquareImage;
	SquareImage.Init(SquareSide, SquareSide, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FColor* SquarePixels = reinterpret_cast<FColor*>(SquareImage.RawData.GetData());
	for (int32 Y = 0; Y < SquareSide; ++Y)
	{
		FMemory::Memcpy(SquarePixels + Y * SquareSide, RawPixels.GetData() + (OriginY + Y) * ViewportSize.X + OriginX, SquareSide * sizeof(FColor));
	}
	// 视口读回的 alpha 不可靠，强制不透明
	for (int32 Index = 0; Index < SquareSide * SquareSide; ++Index)
	{
		SquarePixels[Index].A = 255;
	}

	FImage ThumbnailImage;
	SquareImage.ResizeTo(ThumbnailImage, CameraBookmarkThumbnailSize, CameraBookmarkThumbnailSize, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ThumbnailPath), /*Tree=*/ true);
	const FImageView ThumbnailView(ThumbnailImage.RawData.GetData(), ThumbnailImage.SizeX, ThumbnailImage.SizeY, ERawImageFormat::BGRA8);
	if (!FImageUtils::SaveImageByExtension(*ThumbnailPath, ThumbnailView))
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("Failed to save camera bookmark thumbnail: %s"), *ThumbnailPath);
		return false;
	}
	return true;
}

void FLookDevAssetToolkit::SaveCameraBookmarks()
{
	FLookDevEditorCache::SaveCameraBookmarks(EditorCacheKey, CameraBookmarks);
	++CameraBookmarksVersion;
}

void FLookDevAssetToolkit::OnPreviewSelectionChanged(UObject* ChangedObject)
{
	// 组件树 -> 视口方向的同步进行中（OnComponentTreeSelectionUpdated 内修改选择集），防止互相递归
	if (bSelectionSyncGuard || !SelectedActorDetailsView.IsValid() || !EditorModeManager.IsValid())
	{
		return;
	}

	// 选中组件所属的全部预览 Actor，按 Owner 去重（场景 Actor 在视口客户端中被锁定，不会进入选择集）
	TArray<AActor*> SelectedActors;
	FVector PivotSum(ForceInitToZero);
	int32 PivotCount = 0;
	USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
	for (FSelectionIterator It(*SelectedComponents); It; ++It)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(*It))
		{
			AActor* Owner = Component->GetOwner();
			if (Owner != nullptr && !SelectedActors.Contains(Owner))
			{
				SelectedActors.Add(Owner);
			}
			if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				PivotSum += SceneComponent->GetComponentLocation();
				++PivotCount;
			}
		}
	}

	// 对齐关卡编辑器：多选时详情展示所有选中 Actor 的共有属性（SetObjects 多对象编辑）
	TArray<UObject*> DetailObjects;
	DetailObjects.Reserve(SelectedActors.Num());
	for (AActor* Actor : SelectedActors)
	{
		DetailObjects.Add(Actor);
	}
	SelectedActorDetailsView->SetObjects(DetailObjects);

	// 组件树只在恰好选中单个 Actor 时显示（对齐 SActorDetails::GetObjectContext 的单选语义，多选时树隐藏）
	SelectedPreviewActor = SelectedActors.Num() == 1 ? SelectedActors[0] : nullptr;

	// 组件树跟随选中 Actor 重建，并把树内选中同步到视口选中的组件
	//（对齐 SActorDetails::RefreshSelection 的调用序，SActorDetails.cpp:737-743）
	if (SelectedActorComponentTree.IsValid())
	{
		TGuardValue<bool> SelectionGuard(bSelectionSyncGuard, true);
		// PrePotentialTargetChange 被 Epic 标注为实验性 API（SActorDetails::RefreshSelection 同款调用），局部屏蔽弃用警告
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SelectedActorComponentTree->PrePotentialTargetChange();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		SelectedActorComponentTree->UpdateTree();
		SyncComponentTreeSelectionFromViewport();
	}

	// 变换 gizmo 的位置取 ModeTools->GetWidgetLocation()，最终落到 PivotLocation 上；
	// 关卡编辑器由 UpdatePivotLocationForSelection 维护它（EditorSelectUtils.cpp），
	// 资产编辑器没人代劳，不更新的话 gizmo 会停留在原点
	if (PivotCount > 0)
	{
		EditorModeManager->SetPivotLocation(PivotSum / PivotCount, /*bIncGridBase=*/ false);
	}

	// 选择变化后重推选择描边状态（渲染代理上的选中标记不会随选择集自动更新）
	if (const TSharedPtr<FLookDevViewportClient> LookDevViewportClient = StaticCastSharedPtr<FLookDevViewportClient>(ViewportClient))
	{
		LookDevViewportClient->RefreshSelectionOutlineProxies();
	}

	// 通知大纲面板同步选中行（Tick 轮询版本号）
	++PreviewSelectionVersion;
}

void FLookDevAssetToolkit::OnComponentTreeSelectionUpdated(const TArray<TSharedPtr<FSubobjectEditorTreeNode>>& SelectedNodes)
{
	if (bSelectionSyncGuard || !SelectedActorDetailsView.IsValid())
	{
		return;
	}

	AActor* ContextActor = SelectedPreviewActor.Get();
	if (ContextActor == nullptr)
	{
		return;
	}

	// 选中节点映射为组件实例（对齐 SActorDetails::RefreshSubobjectTreeElements）；
	// 根 Actor 节点选中时忽略组件选择，详情回退到整个 Actor
	TArray<UObject*> DetailObjects;
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : SelectedNodes)
	{
		const FSubobjectData* Data = Node.IsValid() ? Node->GetDataSource() : nullptr;
		if (Data == nullptr)
		{
			continue;
		}
		if (Data->IsRootActor())
		{
			DetailObjects.Reset();
			break;
		}
		if (Data->IsComponent())
		{
			if (UActorComponent* Component = const_cast<UActorComponent*>(Data->FindComponentInstanceInActor(ContextActor)))
			{
				DetailObjects.Add(Component);
			}
		}
	}

	TGuardValue<bool> SelectionGuard(bSelectionSyncGuard, true);

	if (DetailObjects.Num() > 0)
	{
		SelectedActorDetailsView->SetObjects(DetailObjects);
	}
	else
	{
		SelectedActorDetailsView->SetObject(ContextActor);
	}

	// 组件树的选中同步进视口选择集（gizmo / 选择描边的作用目标）；
	// 根 Actor 节点选中时等价于视口点选 Actor：选中其根组件
	if (EditorModeManager.IsValid())
	{
		USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
		SelectedComponents->DeselectAll();

		FVector PivotSum(ForceInitToZero);
		int32 PivotCount = 0;
		auto SelectSceneComponent = [&SelectedComponents, &PivotSum, &PivotCount](USceneComponent* SceneComponent)
		{
			if (SceneComponent != nullptr)
			{
				SelectedComponents->Select(SceneComponent);
				PivotSum += SceneComponent->GetComponentLocation();
				++PivotCount;
			}
		};

		if (DetailObjects.Num() > 0)
		{
			for (UObject* DetailObject : DetailObjects)
			{
				SelectSceneComponent(Cast<USceneComponent>(DetailObject));
			}
		}
		else
		{
			SelectSceneComponent(ContextActor->GetRootComponent());
		}

		// USelection::Select 不广播 SelectionChangedEvent，手动广播一次（见 ProcessClick 末尾的同款处理）
		SelectedComponents->NoteSelectionChanged();

		if (PivotCount > 0)
		{
			EditorModeManager->SetPivotLocation(PivotSum / PivotCount, /*bIncGridBase=*/ false);
		}
	}

	if (const TSharedPtr<FLookDevViewportClient> LookDevViewportClient = StaticCastSharedPtr<FLookDevViewportClient>(ViewportClient))
	{
		LookDevViewportClient->RefreshSelectionOutlineProxies();
	}

	// 组件树 -> 视口方向的同步同样改变了选择集，通知大纲面板刷新选中行
	++PreviewSelectionVersion;
}

void FLookDevAssetToolkit::SyncComponentTreeSelectionFromViewport()
{
	if (!SelectedActorComponentTree.IsValid() || !EditorModeManager.IsValid())
	{
		return;
	}

	// 对齐 SActorDetails::UpdateComponentTreeFromEditorSelection（SActorDetails.cpp:1068-1082）
	const TSharedPtr<SSubobjectEditorDragDropTree> TreeWidget = SelectedActorComponentTree->GetDragDropTree();
	if (!TreeWidget.IsValid())
	{
		return;
	}

	SelectedActorComponentTree->ClearSelection();
	USelection* SelectedComponents = EditorModeManager->GetSelectedComponents();
	for (FSelectionIterator It(*SelectedComponents); It; ++It)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(*It))
		{
			if (const FSubobjectEditorTreeNodePtrType Node = SelectedActorComponentTree->FindSlateNodeForObject(Component, /*bIncludeAttachmentComponents=*/ false))
			{
				TreeWidget->SetItemSelection(Node, true);
			}
		}
	}
}

void FLookDevAssetToolkit::LoadLookDevScene() const
{
	if (PreviewScenePtr == nullptr)
	{
		return;
	}

	const ULookDevEnhanceSettings* Settings = GetDefault<ULookDevEnhanceSettings>();
	if (Settings == nullptr || Settings->LookDevScene.IsNull())
	{
		return;
	}

	bool bLoaded = false;
	ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		PreviewScenePtr->GetWorld(),
		Settings->LookDevScene,
		FTransform::Identity,
		bLoaded,
		TEXT(""),
		nullptr,
		/*bLoadAsTempPackage*/ true);

	if (!bLoaded || StreamingLevel == nullptr)
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("Failed to load LookDev scene '%s'. Falling back to default lighting."),
			*Settings->LookDevScene.ToString());
		AddFallbackLighting();
		return;
	}

	StreamingLevel->SetShouldBeVisibleInEditor(true);
	PreviewScenePtr->GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
	if (LoadedLevel == nullptr)
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("LookDev scene '%s' streamed in but has no loaded level. Falling back to default lighting."),
			*Settings->LookDevScene.ToString());
		AddFallbackLighting();
		return;
	}

	// 临时包加载会一直占用 FLinkerLoad 文件句柄；重置加载器使源 .umap 在本编辑器打开期间仍可保存。
	// 外部 Actor 包（OFPA）需要同样的处理
	ResetLoaders(StreamingLevel->GetWorldAsset().LoadSynchronous());
	TSet<UObject*> ExternalActorPackages;
	for (const AActor* Actor : LoadedLevel->Actors)
	{
		if (UPackage* ExternalPackage = Actor ? Actor->GetExternalPackage() : nullptr)
		{
			ExternalActorPackages.Add(ExternalPackage);
		}
	}
	if (!ExternalActorPackages.IsEmpty())
	{
		ResetLoaders(ExternalActorPackages.Array());
	}

	if (const UWorld* InnerWorld = LoadedLevel->GetTypedOuter<UWorld>())
	{
		if (InnerWorld->IsPartitionedWorld())
		{
			UE_LOG(LogLookDevEnhance, Warning, TEXT("LookDev scene '%s' is a World Partition map, which is not supported as a preview environment."),
				*Settings->LookDevScene.ToString());
		}
	}

	// 在场景中找到预览环境 Actor，细节面板将展示其可编辑属性
	PreviewEnvironmentActor.Reset();
	for (AActor* Actor : LoadedLevel->Actors)
	{
		if (AAssetsPreviewActor* EnvironmentActor = Cast<AAssetsPreviewActor>(Actor))
		{
			PreviewEnvironmentActor = EnvironmentActor;
			break;
		}
	}
	if (!PreviewEnvironmentActor.IsValid())
	{
		UE_LOG(LogLookDevEnhance, Log, TEXT("LookDev scene '%s' contains no AAssetsPreviewActor; the details panel will be empty."),
			*Settings->LookDevScene.ToString());
	}

	// 预览场景的首个渲染帧会首次创建 MaterialSBT；同帧立即 dispatch RT 阴影会导致 GPU 挂死，
	// 因此流送后先临时禁用，待视口渲染过两帧后由 FLookDevViewportClient::Tick 恢复
	if (PreviewEnvironmentActor.IsValid() && PreviewEnvironmentActor->bRaytracingShadow)
	{
		PreviewEnvironmentActor->ApplyRayTracingShadowMode(false);

		DeferredRTShadows = MakeShared<FLookDevDeferredRTShadowState>();
		DeferredRTShadows->EnvironmentActor = PreviewEnvironmentActor;
		DeferredRTShadows->EnableFrame = GFrameCounter + 2;
	}

	LookDevStreamingLevel = StreamingLevel;

	// 立即处理一次天光捕获（SLS_SpecifiedCubemap 的天光要经此创建 scene proxy 才会渲染/进入 Lumen；
	// 纹理未就绪时由视口 Tick 里的每帧调用兜底完成）
	PreviewScenePtr->UpdateCaptureContents();
}

void FLookDevAssetToolkit::AddFallbackLighting() const
{
	if (PreviewScenePtr == nullptr || bHasFallbackLighting)
	{
		return;
	}

	UDirectionalLightComponent* DirectionalLight = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	DirectionalLight->Intensity = 4.0f;
	PreviewScenePtr->AddComponent(DirectionalLight, FTransform(FRotator(-40.f, -67.5f, 0.f)));

	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	PreviewScenePtr->AddComponent(SkyLight, FTransform::Identity);

	bHasFallbackLighting = true;
}

void FLookDevAssetToolkit::UnloadLookDevScene()
{
	if (PreviewScenePtr == nullptr)
	{
		return;
	}

	UWorld* PreviewWorld = PreviewScenePtr->GetWorld();

	// 卸载前先取出已加载的 ULevel，以便 Flush 之后手动把它从 PreviewWorld->Levels[] 中移除。
	// UWorld::RemoveFromWorld 只为游戏 World 处理 Levels[]，预览 World 必须自行移除
	ULevel* LoadedLevel = nullptr;
	if (ULevelStreamingDynamic* StreamingLevel = LookDevStreamingLevel.Get())
	{
		LoadedLevel = StreamingLevel->GetLoadedLevel();
		UE::LookDevEnhance::RequestStreamingLevelUnload(StreamingLevel);
	}
	LookDevStreamingLevel.Reset();

	PreviewWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	if (IsValid(LoadedLevel))
	{
		PreviewWorld->RemoveLevel(LoadedLevel);

		// 在 GC 之前拆除内部 World（释放场景与物理资源），与 UEditorLevelUtils::PrivateDestroyLevel 的处理一致
		if (UWorld* InnerWorld = LoadedLevel->GetTypedOuter<UWorld>())
		{
			if (InnerWorld != PreviewWorld)
			{
				InnerWorld->CleanupWorld();
				InnerWorld->MarkAsGarbage();
			}
		}
		LoadedLevel->MarkAsGarbage();
	}

	PreviewScenePtr = nullptr;
}

void FLookDevAssetToolkit::RelayoutMultiViewCopies()
{
	// 无副本时退化为原型重排
	if (MultiViewCopyActors.Num() == 0)
	{
		RelayoutOriginalPreviewActors();
		return;
	}

	// 按原型分组副本（布局记录在生成时写入；源顺序跟随 PreviewActors，保证行带顺序稳定）
	TArray<AActor*> Sources;
	TMap<AActor*, TArray<int32>> SourceSlotIndices;
	for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
	{
		AActor* CopyActor = Actor.Get();
		if (CopyActor == nullptr || !MultiViewCopyActors.Contains(CopyActor))
		{
			continue;
		}
		const FLookDevMultiViewCopyLayout* Layout = MultiViewCopyLayouts.Find(CopyActor);
		AActor* Source = (Layout != nullptr) ? Layout->SourceActor.Get() : nullptr;
		if (Source == nullptr)
		{
			continue;
		}
		if (!Sources.Contains(Source))
		{
			Sources.Add(Source);
		}
		SourceSlotIndices.FindOrAdd(Source).Add(Layout->PackedSlotIndex);
	}
	if (Sources.IsEmpty())
	{
		return;
	}

	// 原预览 Actor 数（排除副本），判断源是否全覆盖：全覆盖 = 全局行带排布（与生成时口径一致），否则各源原位
	int32 OriginalCount = 0;
	for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
	{
		AActor* ValidActor = Actor.Get();
		if (ValidActor != nullptr && !MultiViewCopyActors.Contains(ValidActor))
		{
			++OriginalCount;
		}
	}
	const bool bGlobalLayout = OriginalCount > 0 && Sources.Num() == OriginalCount;

	// HalfSpacing 测量口径与生成时一致（世界包围盒，含当前旋转）；间距允许负值，钳最小 1cm 防止槽位坍缩
	auto ComputeHalfSpacing = [this](const AActor* Actor)
	{
		const FBox Bounds = Actor->GetComponentsBoundingBox(/*bNonColliding=*/ true);
		return FMath::Max(FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y) + MultiViewCopySpacing * 0.5f, 1.0f);
	};

	// 行带按各源实际槽位的左右伸展量计算（与生成时同一公式）：
	// 左伸展 = (-Min * 2 + 1) * H，行宽 = ((Max - Min) * 2 + 2) * H——不按对称最大序号，
	// 否则奇数副本数（如去掉 -180° 后 7 个副本为 -3..+4）行带一侧留出空带，多资产拼接处出现空位
	struct FRowExtents
	{
		float LeftExtent = 0.0f;
		float RowWidth = 0.0f;
	};
	TMap<AActor*, FRowExtents> SourceRowExtents;
	float TotalRowWidth = 0.0f;
	for (AActor* Source : Sources)
	{
		int32 MinIndex = MAX_int32;
		int32 MaxIndex = MIN_int32;
		for (const int32 SlotIndex : SourceSlotIndices[Source])
		{
			MinIndex = FMath::Min(MinIndex, SlotIndex);
			MaxIndex = FMath::Max(MaxIndex, SlotIndex);
		}
		const float HalfSpacing = ComputeHalfSpacing(Source);
		FRowExtents Extents;
		Extents.LeftExtent = (-MinIndex * 2 + 1) * HalfSpacing;
		Extents.RowWidth = ((MaxIndex - MinIndex) * 2 + 2) * HalfSpacing;
		SourceRowExtents.Add(Source, Extents);
		TotalRowWidth += Extents.RowWidth;
	}

	// 只挪 Y 轴：保留会话中手动的 X/Z 调整、旋转与动画状态
	float BandCursor = -TotalRowWidth * 0.5f;
	for (AActor* Source : Sources)
	{
		const FRowExtents& Extents = SourceRowExtents[Source];
		const float HalfSpacing = ComputeHalfSpacing(Source);

		float SourceY = Source->GetActorLocation().Y;
		if (bGlobalLayout)
		{
			SourceY = BandCursor + Extents.LeftExtent;
			BandCursor += Extents.RowWidth;
			FVector SourceLocation = Source->GetActorLocation();
			SourceLocation.Y = SourceY;
			Source->SetActorLocation(SourceLocation);
		}

		for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
		{
			AActor* CopyActor = Actor.Get();
			if (CopyActor == nullptr)
			{
				continue;
			}
			const FLookDevMultiViewCopyLayout* Layout = MultiViewCopyLayouts.Find(CopyActor);
			if (Layout == nullptr || Layout->SourceActor.Get() != Source)
			{
				continue;
			}
			FVector CopyLocation = CopyActor->GetActorLocation();
			CopyLocation.Y = SourceY + Layout->PackedSlotIndex * 2.0f * HalfSpacing;
			CopyActor->SetActorLocation(CopyLocation);
		}
	}
}

void FLookDevAssetToolkit::RelayoutOriginalPreviewActors() const
{
	// 只排原预览 Actor（排除多视图副本；副本此时应已清除）
	TArray<AActor*> OriginalActors;
	for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
	{
		AActor* ValidActor = Actor.Get();
		if (ValidActor != nullptr && !MultiViewCopyActors.Contains(ValidActor))
		{
			OriginalActors.Add(ValidActor);
		}
	}
	if (OriginalActors.IsEmpty())
	{
		return;
	}

	// 第一遍：按 Actor 局部包围盒（不含当前旋转，与初始排布的测量口径一致）测量占位宽度
	TArray<float> HalfSpacings;
	HalfSpacings.Reserve(OriginalActors.Num());
	float TotalWidth = 0.f;
	for (const AActor* Actor : OriginalActors)
	{
		const FBox LocalBounds = Actor->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding=*/ true);
		// HalfSpacing = 局部包围盒半径 + 排布间距的一半；间距允许负值，钳最小 1cm 防止槽位坍缩
		const float HalfSpacing = FMath::Max(FMath::Max(LocalBounds.GetExtent().X, LocalBounds.GetExtent().Y) + MultiViewCopySpacing * 0.5f, 1.0f);
		HalfSpacings.Add(HalfSpacing);
		TotalWidth += 2.f * HalfSpacing;
	}

	// 第二遍：以原点为中心排成一行，包围盒底部中心点对齐各自槽位中心；
	// 只恢复位置，保留用户会话中用 Gizmo 调整过的旋转
	float Cursor = -TotalWidth * 0.5f;
	for (int32 Index = 0; Index < OriginalActors.Num(); ++Index)
	{
		AActor* Actor = OriginalActors[Index];
		const float Center = Cursor + HalfSpacings[Index];
		Cursor += 2.f * HalfSpacings[Index];

		const FBox LocalBounds = Actor->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding=*/ true);
		const FVector LocalBottomCenter(LocalBounds.GetCenter().X, LocalBounds.GetCenter().Y, LocalBounds.Min.Z);
		// 局部底部中心经当前变换（含旋转/缩放）转到世界，再补偿到槽位中心
		const FVector WorldBottomCenter = Actor->GetActorTransform().TransformPosition(LocalBottomCenter);
		Actor->SetActorLocation(Actor->GetActorLocation() + (FVector(0.f, Center, 0.f) - WorldBottomCenter));
	}
}

void FLookDevAssetToolkit::SpawnPreviewForAssets(const TArray<UObject*>& Assets)
{
	if (PreviewScenePtr == nullptr)
	{
		return;
	}

	// 第一遍：在原点生成所有预览 Actor，并测量每个组件的包围盒以计算占位宽度
	TArray<AActor*> SpawnedActors;
	// 布局测量用的预览组件（局部数组，不入成员——跟踪以 Actor 为单位，见 PreviewActors）
	TArray<USceneComponent*> LayoutComponents;
	TArray<float> HalfSpacings;
	float TotalWidth = 0.f;
	for (UObject* Asset : Assets)
	{
		AActor* PreviewActor = nullptr;
		USceneComponent* PreviewComponent = CreatePreviewActorForAsset(Asset, PreviewActor);
		if (PreviewComponent == nullptr || PreviewActor == nullptr)
		{
			continue;
		}

		const FBox Bounds = PreviewComponent->CalcBounds(FTransform::Identity).GetBox();
		const float HalfSpacing = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y) + 25.f;
		HalfSpacings.Add(HalfSpacing);
		TotalWidth += 2.f * HalfSpacing;

		SpawnedActors.Add(PreviewActor);
		LayoutComponents.Add(PreviewComponent);
		PreviewActors.Add(PreviewActor);
		// 登记 Actor -> 源资产映射，GenerateMultiViewCopies 复用生成链路时据此找回资产
		PreviewActorAssets.Add(PreviewActor, Asset);
	}

	// 第二遍：以原点为中心把 Actor 排成一行，按包围盒底部中心点对齐各自的槽位中心
	FBox CombinedBounds(EForceInit::ForceInit);
	float Cursor = -TotalWidth * 0.5f;
	for (int32 Index = 0; Index < SpawnedActors.Num(); ++Index)
	{
		USceneComponent* PreviewComponent = LayoutComponents[Index];
		AActor* PreviewActor = SpawnedActors[Index];
		if (PreviewComponent == nullptr || PreviewActor == nullptr)
		{
			continue;
		}

		const float Center = Cursor + HalfSpacings[Index];
		Cursor += 2.f * HalfSpacings[Index];

		// 包围盒底部中心点（水平居中 + 底部落地）对齐槽位中心，单个资产即落在世界原点；
		// 不按枢轴放置——骨骼模型的枢轴常与包围盒中心不重合，直接放枢轴会水平偏移或悬空/陷地
		const FBox OriginBounds = PreviewComponent->CalcBounds(FTransform::Identity).GetBox();
		const FVector BoundsBottomCenter(OriginBounds.GetCenter().X, OriginBounds.GetCenter().Y, OriginBounds.Min.Z);
		const FVector Offset = FVector(0.f, Center, 0.f) - BoundsBottomCenter;
		PreviewActor->SetActorLocation(Offset);
		// 生成时即应用环境配置的预览旋转偏移（正面不朝 +X 的资产的朝向校正）
		PreviewActor->SetActorRotation(GetPreviewRotationOffset());
		CombinedBounds += OriginBounds.ShiftBy(Offset);
	}

	// 记录本次已应用的偏移，作为之后属性变化时增量应用的基准
	LastAppliedPreviewRotationOffset = GetPreviewRotationOffset();

	if (ViewportClient.IsValid())
	{
		if (bHasLoadedCacheEntry && LoadedCacheEntry.bHasCameraPose)
		{
			// 有缓存的相机姿态：直接恢复上次的视角与 FOV，跳过默认聚焦
			ViewportClient->SetViewLocation(LoadedCacheEntry.CameraLocation);
			ViewportClient->SetViewRotation(LoadedCacheEntry.CameraRotation);
			ViewportClient->ViewFOV = LoadedCacheEntry.ViewFOV;
			ViewportClient->FOVAngle = LoadedCacheEntry.ViewFOV;
		}
		else if (CombinedBounds.IsValid)
		{
			// 打开编辑器时的默认相机姿态：略微俯视的正面视角（资产沿 Y 轴横排，正对 +X 方向观看），
			// 聚焦只决定距离，方向由这里的默认旋转决定——参照 MetaHuman 编辑器在构造时设置初始机位的做法
			ViewportClient->SetViewRotation(FLookDevViewportClient::DefaultViewRotation);
			ViewportClient->FocusViewportOnBox(CombinedBounds, /*bInstant=*/ true);
		}
	}

	// 通知大纲面板重建列表（Tick 轮询版本号）
	++PreviewActorsVersion;
}

USceneComponent* FLookDevAssetToolkit::CreatePreviewActorForAsset(UObject* Asset, AActor*& OutPreviewActor) const
{
	OutPreviewActor = nullptr;

	if (Asset == nullptr || PreviewScenePtr == nullptr)
	{
		return nullptr;
	}

	// 优先引擎 ActorFactory（与资产拖入关卡相同的转换逻辑），工厂不支持时回退自定义生成
	USceneComponent* PreviewComponent = TrySpawnPreviewActorWithFactory(Asset, OutPreviewActor);
	if (PreviewComponent == nullptr)
	{
		PreviewComponent = SpawnCustomPreviewActor(Asset, OutPreviewActor);
	}

	// 两条路径成功后统一经过特殊化处理钩子
	if (PreviewComponent != nullptr)
	{
		PostSpawnPreviewActor(Asset, OutPreviewActor, PreviewComponent);
	}
	return PreviewComponent;
}

USceneComponent* FLookDevAssetToolkit::TrySpawnPreviewActorWithFactory(UObject* Asset, AActor*& OutPreviewActor) const
{
	OutPreviewActor = nullptr;

	// 与内容浏览器把资产拖进关卡相同的匹配逻辑：取第一个 CanCreateActorFrom 通过的工厂
	UActorFactory* Factory = FActorFactoryAssetProxy::GetFactoryForAssetObject(Asset);
	if (Factory == nullptr)
	{
		return nullptr;
	}

	const UWorld* PreviewWorld = PreviewScenePtr->GetWorld();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	AActor* SpawnedActor = Factory->CreateActor(Asset, PreviewWorld->GetCurrentLevel(), FTransform::Identity, SpawnParams);
	if (SpawnedActor == nullptr || SpawnedActor->GetRootComponent() == nullptr)
	{
		if (SpawnedActor != nullptr)
		{
			SpawnedActor->Destroy();
		}
		return nullptr;
	}

	UE_LOG(LogLookDevEnhance, Verbose, TEXT("Asset '%s' preview spawned via actor factory '%s'."), *Asset->GetName(), *Factory->GetName());

	// 工厂 Actor 的组件随生成注册进预览世界；跟踪根组件即可，
	// 点击到子组件时由 FLookDevViewportClient::ProcessClick 按 Owner 归属解析回根组件
	OutPreviewActor = SpawnedActor;
	return SpawnedActor->GetRootComponent();
}

USceneComponent* FLookDevAssetToolkit::SpawnCustomPreviewActor(UObject* Asset, AActor*& OutPreviewActor) const
{
	OutPreviewActor = nullptr;

	UWorld* PreviewWorld = PreviewScenePtr->GetWorld();

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (PreviewActor == nullptr)
	{
		return nullptr;
	}

	USceneComponent* RootComponent = NewObject<USceneComponent>(PreviewActor, NAME_None, RF_Transient);
	PreviewActor->SetRootComponent(RootComponent);
	RootComponent->RegisterComponent();

	UMeshComponent* PreviewMeshComponent = nullptr;

	if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
	{
		if (UStaticMesh* PreviewMesh = GetDefault<ULookDevEnhanceSettings>()->MaterialPreviewMesh.LoadSynchronous())
		{
			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(PreviewActor, NAME_None, RF_Transient);
			MeshComponent->SetStaticMesh(PreviewMesh);
			MeshComponent->SetMaterial(0, Material);
			PreviewMeshComponent = MeshComponent;
		}
		else
		{
			UE_LOG(LogLookDevEnhance, Warning, TEXT("No material preview mesh configured in LookDev Enhance settings; '%s' will not be shown."), *Asset->GetName());
		}
	}
	else
	{
		UE_LOG(LogLookDevEnhance, Warning, TEXT("Asset '%s' has no preview representation; showing the LookDev scene only."), *Asset->GetName());
	}

	if (PreviewMeshComponent == nullptr)
	{
		PreviewActor->Destroy();
		return nullptr;
	}

	PreviewMeshComponent->SetupAttachment(RootComponent);
	PreviewScenePtr->AddComponent(PreviewMeshComponent, FTransform::Identity);

	OutPreviewActor = PreviewActor;
	return PreviewMeshComponent;
}

void FLookDevAssetToolkit::PostSpawnPreviewActor(UObject* Asset, AActor* PreviewActor, USceneComponent* PreviewComponent) const
{
	// 特殊化处理集中在这里：按资产 / 组件类型对生成结果做统一调整（当前无处理项）
	if (!Asset || !PreviewActor || !PreviewComponent)
	{
		return;
	}

	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PreviewComponent))
		{
			SkeletalMeshComponent->bComponentUseFixedSkelBounds = true;
			SkeletalMeshComponent->SetUpdateClothInEditor(true);
		}
	}
	else if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset))
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PreviewComponent))
		{
			SkeletalMeshComponent->bComponentUseFixedSkelBounds = true;
			SkeletalMeshComponent->SetUpdateAnimationInEditor(true);
			SkeletalMeshComponent->SetUpdateClothInEditor(true);
		}
	}
	else if (UAnimMontage* AnimMontage = Cast<UAnimMontage>(Asset))
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PreviewComponent))
		{
			SkeletalMeshComponent->bComponentUseFixedSkelBounds = true;
			SkeletalMeshComponent->SetUpdateAnimationInEditor(true);
			SkeletalMeshComponent->SetUpdateClothInEditor(true);
		}
	}
	else if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Asset))
	{
		PreviewActor->AddActorWorldOffset(FVector(0.0f, 0.0f, 75.0f));
	}

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(PreviewComponent))
	{
		//PrimitiveComponent->SetBoundsScale(1.2f);
	}
}

FRotator FLookDevAssetToolkit::GetPreviewRotationOffset() const
{
	if (const AAssetsPreviewActor* EnvironmentActor = PreviewEnvironmentActor.Get())
	{
		return EnvironmentActor->PreviewRotationOffset;
	}
	return FRotator::ZeroRotator;
}

void FLookDevAssetToolkit::ApplyPreviewRotationOffsetDelta(const FRotator& Delta)
{
	if (Delta.IsNearlyZero())
	{
		return;
	}

	// 跟踪以 Actor 为单位（原预览与多视图副本都在 PreviewActors 里），逐个统一叠加
	for (const TWeakObjectPtr<AActor>& Actor : PreviewActors)
	{
		if (AActor* ValidActor = Actor.Get())
		{
			ValidActor->SetActorRotation(ValidActor->GetActorRotation() + Delta);
		}
	}
}

void FLookDevAssetToolkit::OnEnvironmentPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (Object == nullptr || Object != PreviewEnvironmentActor.Get())
	{
		return;
	}

	// 编辑结构体子字段（如 Rotator 的 Yaw）时，Property 是叶子属性（Yaw），
	// MemberProperty 才是对象的直接成员属性（PreviewRotationOffset），过滤必须以 MemberProperty 为准
	const FProperty* MemberProperty = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty : PropertyChangedEvent.Property;
	if (MemberProperty == nullptr || MemberProperty->GetFName() != GET_MEMBER_NAME_CHECKED(AAssetsPreviewActor, PreviewRotationOffset))
	{
		return;
	}

	// 以增量方式应用，保留用户用 Gizmo 手动调整过的旋转
	const FRotator NewOffset = PreviewEnvironmentActor->PreviewRotationOffset;
	const FRotator Delta = NewOffset - LastAppliedPreviewRotationOffset;
	LastAppliedPreviewRotationOffset = NewOffset;
	ApplyPreviewRotationOffsetDelta(Delta);
}

void FLookDevAssetToolkit::ApplyCachedEnvironment(const FLookDevEditorCacheEntry& Entry)
{
	AAssetsPreviewActor* EnvironmentActor = PreviewEnvironmentActor.Get();
	if (EnvironmentActor == nullptr)
	{
		return;
	}

	EnvironmentActor->PreviewRotationOffset = Entry.PreviewRotationOffset;
	EnvironmentActor->LightingScenarios = Entry.LightingScenarios;
	EnvironmentActor->ExposureCompensation = Entry.ExposureCompensation;
	EnvironmentActor->BackgroundValueCompensation = Entry.BackgroundValueCompensation;
	EnvironmentActor->bRaytracingShadow = Entry.bRaytracingShadow;
	EnvironmentActor->bEnableTransmission = Entry.bEnableTransmission;
	EnvironmentActor->bTurnLight = Entry.bTurnLight;
	EnvironmentActor->TurnLighting = Entry.TurnLighting;
	EnvironmentActor->bCastGroundShadow = Entry.bCastGroundShadow;
	EnvironmentActor->bShowCalibrator = Entry.bShowCalibrator;
	EnvironmentActor->bEnableChecker = Entry.bEnableChecker;

	// 程序赋值不触发 OnConstruction，手动全量刷新使缓存值生效（RT 阴影由 Update 按缓存值直接开关，不做首开特殊处理）
	EnvironmentActor->Update();

	// 缓存为关时取消 LoadLookDevScene 可能已武装的延迟恢复（场景默认开 RT 阴影时它会在两帧后重新启用，覆盖缓存值）；
	// 共享对象可能已被视口客户端持有，置空环境 Actor 即取消，不 Reset 成员
	if (!Entry.bRaytracingShadow && DeferredRTShadows.IsValid())
	{
		DeferredRTShadows->EnvironmentActor.Reset();
	}
}

FLookDevEditorCacheEntry FLookDevAssetToolkit::CaptureCacheEntry() const
{
	// 以已加载条目为底：环境 Actor 不存在时保留加载值，避免把默认值写回覆盖历史缓存
	FLookDevEditorCacheEntry Entry = bHasLoadedCacheEntry ? LoadedCacheEntry : FLookDevEditorCacheEntry();

	if (const AAssetsPreviewActor* EnvironmentActor = PreviewEnvironmentActor.Get())
	{
		Entry.PreviewRotationOffset = EnvironmentActor->PreviewRotationOffset;
		Entry.LightingScenarios = EnvironmentActor->LightingScenarios;
		Entry.ExposureCompensation = EnvironmentActor->ExposureCompensation;
		Entry.BackgroundValueCompensation = EnvironmentActor->BackgroundValueCompensation;
		Entry.bRaytracingShadow = EnvironmentActor->bRaytracingShadow;
		Entry.bEnableTransmission = EnvironmentActor->bEnableTransmission;
		Entry.bTurnLight = EnvironmentActor->bTurnLight;
		Entry.TurnLighting = EnvironmentActor->TurnLighting;
		Entry.bCastGroundShadow = EnvironmentActor->bCastGroundShadow;
		Entry.bShowCalibrator = EnvironmentActor->bShowCalibrator;
		Entry.bEnableChecker = EnvironmentActor->bEnableChecker;
	}

	if (ViewportClient.IsValid())
	{
		Entry.ViewFOV = ViewportClient->ViewFOV;
		Entry.bHasCameraPose = true;
		Entry.CameraLocation = ViewportClient->GetViewLocation();
		Entry.CameraRotation = ViewportClient->GetViewRotation();
	}

	Entry.MultiViewSlotMask = MultiViewSlotMask;
	Entry.MultiViewCopySpacing = MultiViewCopySpacing;
	Entry.bHasMultiViewCopies = MultiViewCopyActors.Num() > 0;

	return Entry;
}

bool FLookDevAssetToolkit::TickEditorCache(float DeltaTime)
{
	if (EditorCacheKey.IsEmpty())
	{
		return true;
	}

	const FLookDevEditorCacheEntry CurrentEntry = CaptureCacheEntry();
	if (!bHasLastSavedCacheEntry || !CurrentEntry.Equals(LastSavedCacheEntry))
	{
		FLookDevEditorCache::SaveEntry(EditorCacheKey, CurrentEntry);
		LastSavedCacheEntry = CurrentEntry;
		bHasLastSavedCacheEntry = true;
	}
	return true;
}

void FLookDevAssetToolkit::FlushEditorCache()
{
	TickEditorCache(0.0f);
}

#undef LOCTEXT_NAMESPACE
