// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "LookDevEditor/LookDevEditorCache.h"
#include "Tools/BaseAssetToolkit.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class AAssetsPreviewActor;
class FPreviewScene;
class FSubobjectEditorTreeNode;
class SSubobjectEditor;
class ULevelStreamingDynamic;
class UToolMenu;

namespace PropertyEditorPolicy
{
	class IArchetypePolicy;
}

struct FLookDevDeferredRTShadowState;
struct FToolMenuSection;

// 浮空详情抽屉的页面枚举（None = 抽屉收起）
enum class ELookDevDetailsPanel : uint8
{
	None,
	// 预览环境 Actor 详情
	Environment,
	// 视口选中 Actor 详情
	SelectedActor,
	// 预览 Actor 大纲（轻量 WorldOutliner，用于选中视口中难以点选的 Actor，如特效）
	Outliner,
	// 摄像机书签
	CameraBookmarks,
};

// 高清截图分辨率预设（16:9 传统分辨率，视口居中裁剪到 16:9 后缩放到目标尺寸）
enum class ELookDevScreenshotResolution : uint8
{
	// 1920 x 1080
	Res1K,
	// 2560 x 1440
	Res2K,
	// 3840 x 2160
	Res4K,
};

// 多视图副本的排布记录（副本 -> 原型与紧凑槽位序号）；
// 间距变化时据此纯位置重排（RelayoutMultiViewCopies），不参与生成决策
struct FLookDevMultiViewCopyLayout
{
	TWeakObjectPtr<AActor> SourceActor;
	int32 PackedSlotIndex = 0;
};

/**
 * LookDev 编辑器工具集
 *
 * 基于 FBaseAssetToolkit 的编辑器界面载体：在 FPreviewScene 预览世界中
 * 流送加载配置好的 LookDev 场景，并为被编辑资产生成预览 Actor（居中排成一行）。
 * 视口客户端使用 FLookDevViewportClient 将点击选择限制在预览 Actor 上（选中其根组件供 gizmo / 拖拽），
 * 视口控件使用 SLookDevEditorViewport 提供工具栏。
 * 细节面板展示 LookDev 场景中 AAssetsPreviewActor 的可编辑属性，用于实时调整预览环境；
 * 环境 / 选中 Actor 详情、预览 Actor 大纲与摄像机书签四个面板以浮空抽屉形式覆盖在视口右侧（SLookDevDetailsDrawer），
 * 不参与停靠布局，视口始终保持完整宽高比；抽屉显隐与当前页面由 ActiveDetailsPanel 驱动。
 */
class FLookDevAssetToolkit : public FBaseAssetToolkit
{

private:
	// 预览场景的裸指针，仅用于场景搭建与拆除；FAssetEditorModeManager 只持有裸指针而不负责删除（与 FBaseAssetToolkit 行为一致），场景在本工具集销毁前一直有效
	mutable FPreviewScene* PreviewScenePtr = nullptr;

	// 流送进来的 LookDev 场景关卡，UnloadLookDevScene 据此卸载
	mutable TWeakObjectPtr<ULevelStreamingDynamic> LookDevStreamingLevel;

	// LookDev 场景中找到的预览环境 Actor，细节面板展示其可编辑属性
	mutable TWeakObjectPtr<AAssetsPreviewActor> PreviewEnvironmentActor;

	// 延迟开启 RT 阴影的共享状态（场景流送后首帧禁用、到帧恢复），为空表示无需延迟
	mutable TSharedPtr<FLookDevDeferredRTShadowState> DeferredRTShadows;

	// 是否已添加兜底照明，防止重复添加
	mutable bool bHasFallbackLighting = false;

	// 已生成的预览 Actor（原预览与多视图副本），是用户在视口中唯一允许选中和变换的对象
	TArray<TWeakObjectPtr<AActor>> PreviewActors;

	// 本工具集正在编辑的全部资产
	TArray<TWeakObjectPtr<UObject>> EditedAssets;

	// "选中 Actor"详情面板，展示视口中选中预览组件所属的 Actor
	TSharedPtr<IDetailsView> SelectedActorDetailsView;

	// "选中 Actor"面板的组件树（对齐关卡编辑器 SActorDetails 的 SSubobjectInstanceEditor），展示选中预览 Actor 的组件列表；
	// 成员类型放宽到基类 SSubobjectEditor：SSubobjectInstanceEditor 把 UpdateTree 重声明成了 protected，基类接口才是 public
	TSharedPtr<SSubobjectEditor> SelectedActorComponentTree;

	// 视口当前选中的预览 Actor，作为组件树的 ObjectContext
	TWeakObjectPtr<AActor> SelectedPreviewActor;

	// 视口选择 <-> 组件树选择双向同步的递归保护
	bool bSelectionSyncGuard = false;

	// 多视图副本 Actor，供 ClearMultiViewCopies 清除
	TArray<TWeakObjectPtr<AActor>> MultiViewCopyActors;

	// 副本排布记录：副本 Actor -> 原型与紧凑槽位序号（GenerateMultiViewCopies 写入、ClearMultiViewCopies 清空；
	// 间距变化时 RelayoutMultiViewCopies 据此纯位置重排，不参与生成决策）
	TMap<TWeakObjectPtr<AActor>, FLookDevMultiViewCopyLayout> MultiViewCopyLayouts;

	// 预览 Actor -> 源资产映射（原预览与多视图副本都登记），GenerateMultiViewCopies 复用生成链路时据此找回资产
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UObject>> PreviewActorAssets;

	// 多视图副本角度槽位启用位掩码（bit i 对应槽位数组下标；默认 ±135° 不勾选、其余全开，即 0b10111101；由工具栏下拉复选框修改）
	uint8 MultiViewSlotMask = 0xBD;

	// 预览排布间距（相邻原型 / 副本包围盒之间的净距离，单位 cm；同时驱动原型行与副本行带，允许负值——重叠压紧；
	// 由工具栏下拉输入框修改，逐资产序列化进缓存）
	float MultiViewCopySpacing = 50.0f;

	// 已应用到预览 Actor 的旋转偏移，作为环境属性变化时增量应用的基准
	FRotator LastAppliedPreviewRotationOffset = FRotator::ZeroRotator;

	// 预览环境 Actor 的属性变化监听句柄（OnObjectPropertyChanged）
	FDelegateHandle EnvironmentPropertyChangedHandle;

	// 编辑器状态缓存的 Key（资产集合排序拼接，见 FLookDevEditorCache::MakeCacheKey），为空表示不启用缓存
	FString EditorCacheKey;

	// 打开编辑器时从缓存读回的条目（bHasLoadedCacheEntry 为 true 时有效），驱动环境属性 / 相机 / 副本的恢复
	FLookDevEditorCacheEntry LoadedCacheEntry;

	// 是否存在已加载的缓存条目
	bool bHasLoadedCacheEntry = false;

	// 最近一次写入磁盘的缓存快照，防抖轮询据此判断是否有变化
	FLookDevEditorCacheEntry LastSavedCacheEntry;

	// 是否已有基线快照（false 时下次轮询无条件写一次）
	bool bHasLastSavedCacheEntry = false;

	// 缓存防抖轮询的 Ticker 句柄（0.5s 间隔，快照有变化才写盘）
	FTSTicker::FDelegateHandle EditorCacheTickerHandle;

	// revert 基准快照：预览环境 Actor 打开时（缓存应用之前）的副本，强引用防 GC；
	// 详情面板的 revert 箭头与回写以它为基准（场景实例的配置值），而不是蓝图类默认值
	TStrongObjectPtr<AAssetsPreviewActor> EnvironmentSnapshot;

	// 属性编辑器 Archetype 策略：把 PreviewEnvironmentActor 的 revert 基准重定向到 EnvironmentSnapshot
	//（PropertyEditorPolicy::RegisterArchetypePolicy 全局机制，LevelInstance 属性覆盖同款做法）；
	// 用 TSharedPtr 持有：IArchetypePolicy 析构非虚，TSharedPtr 的类型擦除删除器能正确析构派生类
	TSharedPtr<PropertyEditorPolicy::IArchetypePolicy> EnvironmentArchetypePolicy;

	// 浮空详情抽屉当前页面；默认收起，由用户从视口右缘入口条按需展开
	ELookDevDetailsPanel ActiveDetailsPanel = ELookDevDetailsPanel::None;

	// 高清截图分辨率预设（工具栏截图下拉菜单修改；会话级状态，不持久化）
	ELookDevScreenshotResolution ScreenshotResolutionPreset = ELookDevScreenshotResolution::Res2K;

	// 截图是否叠加右下角水印（会话级状态，不持久化）
	bool bScreenshotWatermarkEnabled = true;

	// 进行中的截图输出路径；引擎在广播截图完成委托前已 Reset FScreenshotRequest，文件名需自行记录
	FString PendingScreenshotPath;

	// 截图完成委托句柄（截图时一次性订阅，回调触发后立即退订）
	FDelegateHandle ScreenshotProcessedHandle;

	// 当前资产 Key 下的摄像机书签列表（逐资产序列化，见 FLookDevEditorCache::SaveCameraBookmarks）
	TArray<FLookDevCameraBookmark> CameraBookmarks;

	// 书签列表变更计数器，书签面板 Tick 轮询它决定是否重建列表
	int32 CameraBookmarksVersion = 0;

	// 预览 Actor 列表变更计数器，大纲面板 Tick 轮询它决定是否重建列表
	//（SpawnPreviewForAssets / GenerateMultiViewCopies / ClearMultiViewCopies 时递增）
	int32 PreviewActorsVersion = 0;

	// 预览选择集变更计数器，大纲面板 Tick 轮询它同步选中行（OnPreviewSelectionChanged 时递增）
	int32 PreviewSelectionVersion = 0;

public:
	// 多视图副本的角度槽位数（原型占中心 0° 槽位，副本槽位 -180°~+180° 跳过 0°，间隔 45°）
	static constexpr int32 NumMultiViewCopySlots = 8;

	// 书签缩略图边长（正方形）
	static constexpr int32 CameraBookmarkThumbnailSize = 128;

	// 本工具集的类型名，用于编辑器框架识别
    static const FName LookDevToolkitFName;

public:
	FLookDevAssetToolkit(UAssetEditor* InOwningAssetEditor);

	virtual ~FLookDevAssetToolkit() override;

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	virtual void CreateWidgets() override;

	virtual FName GetToolkitFName() const override;

	virtual FText GetBaseToolkitName() const override;

	virtual FText GetToolkitName() const override;

	virtual FText GetToolkitToolTipText() const override;

	virtual void SetEditingObject(UObject* InObject) override;

	virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;

protected:
	virtual void RegisterToolbar() override;

	virtual bool CanSaveAsset() const override { return false; }

	virtual bool CanSaveAssetAs() const override { return false; }

	virtual FLinearColor GetDefaultTabColor() const override;

	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;

	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;

	virtual TSharedRef<IDetailsView> CreateDetailsView() override;

	/**
	 *	创建"选中 Actor"详情视图
	 *
	 *	对齐关卡编辑器 SActorDetails：不做内置类别隐藏，
	 *	保留全局 FActorDetails 定制器注入的 Transform 组合控件（锁 / 重置 / 复制粘贴）
	 */
	TSharedRef<IDetailsView> CreateSelectedActorDetailsView();

public:
	/**
     *	生成多视图旋转副本
     *
     *	为当前选中的预览组件（未选中时为全部预览资产）按固定角度集合生成一圈旋转副本，
     *	以原型为中心沿 Y 轴两侧对称陈列；副本复用 CreatePreviewActorForAsset 链路重新生成
     *	（ActorFactory / 自定义兜底 / PostSpawn 特殊化全部生效），并加入预览 Actor 集合，支持选中与 Gizmo 微调。
     *	源集合覆盖全部预览 Actor 时启用全局行带排布：每个原型占一个 Y 轴行带、整体以原点为中心排开，
     *	原型 Actor 迁就偏移到行带中心，避免多资产的副本行重叠；只选中部分 Actor 时保持各源原位。
     *	唯一化保护：场景中已存在副本时先全部清除再重新生成，角度勾选与间距的修改立即生效
     */
    void GenerateMultiViewCopies();

    /**
     *	清除所有多视图副本
     */
    void ClearMultiViewCopies();

	/**
	 *	槽位数组下标对应的 Yaw 角度（0..7 -> -180°,-135°,-90°,-45°,+45°,+90°,+135°,+180°）
	 *
	 *	@param	SlotArrayIndex	槽位数组下标 [0, NumMultiViewCopySlots)
	 *	@return	该槽位副本的 Yaw 角度
	 */
	static float GetMultiViewCopySlotYaw(int32 SlotArrayIndex);

	/**
	 *	指定角度槽位是否启用，未启用的槽位在 GenerateMultiViewCopies 中跳过
	 *
	 *	@param	SlotArrayIndex	槽位数组下标 [0, NumMultiViewCopySlots)
	 */
	bool IsMultiViewCopySlotEnabled(int32 SlotArrayIndex) const;

	/**
	 *	开关指定角度槽位
	 *
	 *	@param	SlotArrayIndex	槽位数组下标 [0, NumMultiViewCopySlots)
	 *	@param	bEnabled		是否启用
	 */
	void SetMultiViewCopySlotEnabled(int32 SlotArrayIndex, bool bEnabled);

	/**
	 *	当前预览排布间距（相邻原型 / 副本包围盒之间的净距离，单位 cm）
	 */
	float GetMultiViewCopySpacing() const { return MultiViewCopySpacing; }

	/**
	 *	设置预览排布间距（允许负值：包围盒互相重叠、排布压紧）
	 *
	 *	修改立即生效：有副本时纯位置重排（RelayoutMultiViewCopies，不销毁重建，动画与手动调整保留），
	 *	无副本时重排原型行（RelayoutOriginalPreviewActors）
	 *
	 *	@param	InSpacing	目标间距（cm）
	 */
	void SetMultiViewCopySpacing(float InSpacing);

	/**
	 *	切换浮空详情抽屉到指定页面；再次切换当前已打开的页面则收起抽屉
	 *
	 *	@param	Panel	目标页面
	 */
	void ToggleDetailsPanel(ELookDevDetailsPanel Panel);

	ELookDevDetailsPanel GetActiveDetailsPanel() const { return ActiveDetailsPanel; }

	TSharedPtr<IDetailsView> GetEnvironmentDetailsView() const { return DetailsView; }

	TSharedPtr<IDetailsView> GetSelectedActorDetailsView() const { return SelectedActorDetailsView; }

	TSharedPtr<SSubobjectEditor> GetSelectedActorComponentTree() const { return SelectedActorComponentTree; }

	const TArray<TWeakObjectPtr<AActor>>& GetPreviewActors() const { return PreviewActors; }

	int32 GetPreviewActorsVersion() const { return PreviewActorsVersion; }

	int32 GetPreviewSelectionVersion() const { return PreviewSelectionVersion; }

	/**
	 *	视口当前选中的预览 Actor（按 Owner 去重），供大纲面板同步选中行
	 *
	 *	@return	选中的预览 Actor 列表
	 */
	TArray<AActor*> GetSelectedPreviewActors() const;

	/**
	 *	指定 Actor 是否为多视图副本（大纲面板据此在行名后追加相对朝向角）
	 *
	 *	@param	Actor	要查询的预览 Actor
	 */
	bool IsMultiViewCopyActor(const AActor* Actor) const;

	/**
	 *	大纲面板的行显示名：Actor 标签；多视图副本追加相对原型的朝向角（如 "SM_Cube (+45°)"）
	 *
	 *	@param	Actor	预览 Actor
	 *	@return	大纲行显示名
	 */
	FText GetPreviewActorDisplayLabel(const AActor* Actor) const;

	/**
	 *	预览 Actor 的源资产（行 Tooltip 显示资产路径用），未登记时返回 nullptr
	 *
	 *	@param	Actor	预览 Actor
	 */
	UObject* GetPreviewActorSourceAsset(const AActor* Actor) const;

	/**
	 *	把指定预览 Actor 设为视口选择（大纲 -> 视口方向）
	 *
	 *	选中各 Actor 的根组件写入 ModeManager 组件选择集并广播 NoteSelectionChanged，
	 *	详情面板 / 组件树 / gizmo 枢轴 / 选择描边由 OnPreviewSelectionChanged 统一刷新
	 *
	 *	@param	Actors	要选中的预览 Actor（空数组 = 清空选择）
	 */
	void SelectPreviewActors(const TArray<AActor*>& Actors);

	/**
	 *	保持当前视口朝向，平滑聚焦当前选中的预览 Actor（大纲行双击，与 F 键同一套聚焦逻辑）
	 */
	void FocusOnSelection();

	/**
	 *	截取视口高清截图并按设置叠加水印
	 *
	 *	按当前分辨率预设把视口居中裁剪成 16:9 后高清渲染（对齐关卡编辑器 HighResScreenshot 对话框的内部调用：
	 *	FHighResScreenshotConfig::SetResolution + CaptureRegion + FViewport::TakeHighResScreenShot），
	 *	引擎写盘完成后在 OnScreenshotProcessed 回调里加载 PNG 叠加右下角水印并覆盖写回
	 */
	void CaptureHighResScreenshot();

	/**
	 *	指定分辨率预设是否为当前选中项（菜单勾选态）
	 *
	 *	@param	Preset	要查询的分辨率预设
	 */
	bool IsScreenshotResolutionPreset(ELookDevScreenshotResolution Preset) const;

	/**
	 *	设置截图分辨率预设
	 *
	 *	@param	Preset	目标分辨率预设
	 */
	void SetScreenshotResolutionPreset(ELookDevScreenshotResolution Preset);

	/**
	 *	截图水印开关当前状态（菜单勾选态）
	 */
	bool IsScreenshotWatermarkEnabled() const { return bScreenshotWatermarkEnabled; }

	/**
	 *	切换截图水印开关
	 */
	void ToggleScreenshotWatermark();

	const TArray<FLookDevCameraBookmark>& GetCameraBookmarks() const { return CameraBookmarks; }

	int32 GetCameraBookmarksVersion() const { return CameraBookmarksVersion; }

	/**
	 *	新建摄像机书签：以当前视口的位置 / 旋转 / FOV 创建并捕获缩略图
	 *
	 *	@param	DesiredName	期望的书签名，为空时回退默认名，重名时自动追加序号
	 */
	void AddCameraBookmark(const FString& DesiredName);

	/**
	 *	删除指定书签（同时删除其缩略图文件）
	 *
	 *	@param	Index	书签下标
	 */
	void RemoveCameraBookmark(int32 Index);

	/**
	 *	重命名指定书签，重名时自动追加序号保证唯一
	 *
	 *	@param	Index		书签下标
	 *	@param	DesiredName	期望的新名字
	 */
	void RenameCameraBookmark(int32 Index, const FString& DesiredName);

	/**
	 *	应用指定书签：视口跳到书签的位置 / 旋转 / FOV，并按新画面刷新缩略图（缩略图可能过时）
	 *
	 *	@param	Index	书签下标
	 */
	void ApplyCameraBookmark(int32 Index);

private:
	/**
	 *	填充多视图副本的角度复选框菜单（Multi-View Copies 复合按钮的下拉内容）
	 *
	 *	@param	Menu	要填充的菜单
	 */
	void FillMultiViewAnglesMenu(UToolMenu* Menu);

	/**
	 *	填充高清截图设置菜单（Screenshot 复合按钮的下拉内容：分辨率预设单选 + 水印开关）
	 *
	 *	@param	Menu	要填充的菜单
	 */
	void FillScreenshotSettingsMenu(UToolMenu* Menu);

	/**
	 *	填充工具栏的 LookDev 区块（动态条目回调）
	 *
	 *	工具栏菜单（AssetEditor.LookDevEditor.ToolBar）按 ToolkitFName 命名、被所有编辑器实例共享，
	 *	因此整个区块注册为动态条目：每个窗口生成自己的工具栏时本回调各执行一次，
	 *	经 GetToolkitFromToolMenu 按菜单上下文找回当前窗口的工具集实例后再绑定委托
	 *（对齐 FStaticMeshEditor 的工具栏写法）；若注册时直接 CreateSP(this)，
	 *	最后注册的实例会被冻结进共享菜单，多开窗口时按钮串台
	 *
	 *	@param	InSection	要填充的工具栏区块
	 */
	static void FillToolbarSection(FToolMenuSection& InSection);

	/**
	 *	从工具栏菜单上下文找回当前窗口的工具集实例
	 *
	 *	FAssetEditorToolkit::GenerateToolbar 会把各实例的 Toolkit 塞进 UAssetEditorToolkitMenuContext
	 *	（AssetEditorToolkit.cpp:1527-1529），动态条目与下拉子菜单生成时继承同一上下文；
	 *	共享菜单的静态委托经此区分操作的是哪个窗口（对齐 FStaticMeshEditor::GetStaticMeshEditorFromMenuContext）
	 *
	 *	@return	当前窗口的工具集实例，上下文无效时为 nullptr
	 */
	static TSharedPtr<FLookDevAssetToolkit> GetToolkitFromToolMenu(const UToolMenu* Menu);
	static TSharedPtr<FLookDevAssetToolkit> GetToolkitFromToolMenu(const FToolMenuSection& Section);

	/**
	 *	截图完成回调：引擎写盘结束后退订委托，按设置叠加水印并弹完成通知
	 */
	void OnScreenshotProcessed();

	/**
	 *	给已写盘的截图叠加右下角水印并覆盖写回
	 *
	 *	水印图固定为插件 Resources/LookDevWatermark.png，缺失时静默跳过（仅记日志）；
	 *	水印高度取截图短边的 4%，与右 / 下边距取短边的 2%
	 *
	 *	@param	ScreenshotPath	已写盘的截图完整路径
	 */
	void ApplyScreenshotWatermark(const FString& ScreenshotPath) const;

	/**
	 *	分辨率预设对应的目标分辨率（16:9）
	 *
	 *	@param	Preset	分辨率预设
	 *	@return	目标分辨率（宽 x 高）
	 */
	static FIntPoint GetScreenshotResolutionSize(ELookDevScreenshotResolution Preset);

	/**
	 *	生成唯一书签名：空名回退默认名，与其他书签重名时追加" (N)"序号
	 *
	 *	@param	DesiredName	期望的名字
	 *	@param	ExcludeIndex	去重时排除的书签下标（重命名自身时传自身下标，否则传 INDEX_NONE）
	 *	@return	唯一的书签名
	 */
	FString MakeUniqueCameraBookmarkName(const FString& DesiredName, int32 ExcludeIndex) const;

	/**
	 *	同步捕获当前视口画面为书签缩略图（中心正方形裁剪后缩放到 128x128）
	 *
	 *	与 FEditorViewportClient::TakeScreenshot 相同的同步捕获路径：Invalidate + Draw + ReadPixels，
	 *	在按钮点击等渲染帧外的时机调用。捕获成功时覆盖写入书签的缩略图文件
	 *
	 *	@param	Bookmark	要捕获缩略图的书签（使用其 ThumbnailFileName 决定输出路径）
	 *	@return	捕获并写盘成功时返回 true
	 */
	bool CaptureCameraBookmarkThumbnail(const FLookDevCameraBookmark& Bookmark) const;

	/**
	 *	把当前书签列表写盘并递增版本号（通知面板重建）
	 */
	void SaveCameraBookmarks();

	/**
	 *	视口选择变化时刷新"选中 Actor"详情面板
	 *
	 *	详情展示选中组件所属的全部预览 Actor（多选时为共有属性，对齐关卡编辑器）；
	 *	组件树仅在恰好选中单个 Actor 时显示
	 *
	 *	@param	ChangedObject	选择集中变化的对象（未使用）
	 */
	void OnPreviewSelectionChanged(UObject* ChangedObject);

	/**
	 *	详情视图创建的公共实现
	 *
	 *	@param	bHideEnvironmentUnrelatedCategories	true=环境详情（隐藏 Actor / Rendering / LevelInstance / Transform 等与 LookDev 无关的内置类别及 FActorDetails 的 Transform 定制行）；
	 *		false=选中 Actor 详情（保留全部类别与定制行，对齐关卡编辑器）
	 */
	TSharedRef<IDetailsView> CreateDetailsViewInternal(bool bHideEnvironmentUnrelatedCategories);

	/**
	 *	组件树的上下文对象：当前视口选中的预览 Actor（无选中时为 nullptr，树自动隐藏）
	 */
	UObject* GetComponentTreeObjectContext() const { return SelectedPreviewActor.Get(); }

	/**
	 *	组件树选中变化回调：详情视图切换为选中组件，并把组件同步进视口选择集（树 -> 视口方向）
	 *
	 *	@param	SelectedNodes	组件树当前选中的节点
	 */
	void OnComponentTreeSelectionUpdated(const TArray<TSharedPtr<FSubobjectEditorTreeNode>>& SelectedNodes);

	/**
	 *	把视口当前选中的组件同步为组件树的选中节点（视口 -> 树方向，须在 bSelectionSyncGuard 内调用）
	 */
	void SyncComponentTreeSelectionFromViewport();

	/**
	 *	将配置好的 LookDev 场景以临时包形式流送加载进预览世界，加载失败时回退到兜底照明
	 */
	void LoadLookDevScene() const;

	/**
	 *	在没有可用 LookDev 场景时添加简单的平行光 + 天空光作为兜底照明
	 */
	void AddFallbackLighting() const;

	/**
	 *	在预览世界销毁前移除流送进来的 LookDev 场景，并清理其内部 World 与 Level
	 */
	void UnloadLookDevScene();

	/**
	 *	按当前排布间距纯位置重排副本与原型（不销毁重建）
	 *
	 *	依据生成时登记的排布记录（MultiViewCopyLayouts）重算行带与槽位坐标，只 SetActorLocation 挪 Y 轴，
	 *	动画进度 / 速率、手动旋转与 X/Z 调整全部保留；源集合覆盖全部原预览 Actor 时用全局行带排布
	 *	（与生成时口径一致），否则保持各源原位；无副本时退化为 RelayoutOriginalPreviewActors
	 */
	void RelayoutMultiViewCopies();

	/**
	 *	把原预览 Actor（排除多视图副本）重新排成以原点为中心的一行
	 *
	 *	与 SpawnPreviewForAssets 的初始排布同一套规则（按 Actor 局部包围盒计算占位、
	 *	相邻包围盒净距 = 当前排布间距、包围盒底部中心点对齐槽位中心）；
	 *	ClearMultiViewCopies 清除副本后调用它恢复原位——全局行带排布曾把原型挪到行带中心。
	 *	只恢复位置，保留用户会话中用 Gizmo 调整过的旋转
	 */
	void RelayoutOriginalPreviewActors() const;

	/**
	 *	为所有被编辑资产生成带网格体组件的预览 Actor，并以原点为中心排成一行
	 *
	 *	@param	Assets	要生成预览的资产列表
	 */
	void SpawnPreviewForAssets(const TArray<UObject*>& Assets);

	/**
	 *	资产 -> 预览 Actor 的统一入口：把单个资产转换为预览 Actor 及其预览组件
	 *
	 *	优先走引擎 ActorFactory（与资产拖入关卡相同的转换逻辑，见 TrySpawnPreviewActorWithFactory），
	 *	工厂不支持该资产时回退到自定义生成逻辑（如材质球预览网格，见 SpawnCustomPreviewActor）。
	 *	后续调整资产转换规则时从本入口进入审核
	 *
	 *	@param	Asset			要预览的资产
	 *	@param	OutPreviewActor	输出生成的预览 Actor，失败时为 nullptr
	 *	@return	预览组件（包围盒测量 / 动画同步等基于它；跟踪与选中以 Actor 为单位），资产无预览形态或生成失败时返回 nullptr
	 *
	 *	@note	组件必须挂在一个 Owner Actor 下才会在视口生成 HActor 命中代理，
	 *		无 Owner 的组件永远无法被点击选中
	 */
	USceneComponent* CreatePreviewActorForAsset(UObject* Asset, AActor*& OutPreviewActor) const;

	/**
	 *	尝试用引擎 ActorFactory 生成预览 Actor（与内容浏览器拖入关卡一致）
	 *
	 *	按资产类型匹配 GEditor->ActorFactories 中第一个 CanCreateActorFrom 通过的工厂，
	 *	用 UActorFactory::CreateActor 把 Actor 生成进预览世界的当前关卡
	 *
	 *	@param	Asset			要预览的资产
	 *	@param	OutPreviewActor	输出生成的预览 Actor，未匹配到工厂或生成失败时为 nullptr
	 *	@return	被跟踪的预览组件（工厂 Actor 的根组件），无可用工厂或生成失败时返回 nullptr
	 */
	USceneComponent* TrySpawnPreviewActorWithFactory(UObject* Asset, AActor*& OutPreviewActor) const;

	/**
	 *	自定义生成预览 Actor：裸 Actor + 手工注册的预览网格组件（ActorFactory 兜底路径）
	 *
	 *	仅覆盖 ActorFactory 不支持的类型（材质套用设置里配置的预览网格体）；
	 *	静态 / 骨骼网格体已由 ActorFactory 涵盖，不走这里
	 *
	 *	@param	Asset			要预览的资产
	 *	@param	OutPreviewActor	输出生成的预览 Actor，失败时为 nullptr
	 *	@return	被跟踪的预览组件，资产无预览形态或生成失败时返回 nullptr
	 */
	USceneComponent* SpawnCustomPreviewActor(UObject* Asset, AActor*& OutPreviewActor) const;

	/**
	 *	预览 Actor 生成后的特殊化处理钩子
	 *
	 *	两条生成路径（ActorFactory / 自定义兜底）成功后都会经过这里，
	 *	需要按资产或组件类型做的统一调整（碰撞、Mobility、渲染标志等）集中加在本函数
	 *
	 *	@param	Asset				被预览的源资产（按资产类型做分支处理时用）
	 *	@param	PreviewActor		已生成的预览 Actor
	 *	@param	PreviewComponent	被跟踪的预览组件（PreviewActor 的根组件或自定义路径的网格组件）
	 */
	void PostSpawnPreviewActor(UObject* Asset, AActor* PreviewActor, USceneComponent* PreviewComponent) const;

	/**
	 *	读取预览环境 Actor 上配置的预览旋转偏移，无环境 Actor 时返回零旋转
	 */
	FRotator GetPreviewRotationOffset() const;

	/**
	 *	把旋转偏移增量叠加到全部预览 Actor（原预览与多视图副本，按 Owner 去重）
	 *
	 *	@param	Delta	要叠加的旋转增量；增量方式可保留用户用 Gizmo 手动调整过的旋转
	 */
	void ApplyPreviewRotationOffsetDelta(const FRotator& Delta);

	/**
	 *	预览环境 Actor 的属性变化回调：PreviewRotationOffset 改变时把增量同步到全部预览 Actor
	 *
	 *	@param	Object					属性被修改的对象
	 *	@param	PropertyChangedEvent	属性变化事件
	 */
	void OnEnvironmentPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	/**
	 *	把缓存条目的环境属性应用到预览环境 Actor
	 *
	 *	逐字段赋值后手动调用 Update() 全量刷新（程序赋值不触发 OnConstruction）；
	 *	缓存关闭 RT 阴影时会取消 LoadLookDevScene 已武装的延迟恢复，防止缓存值被覆盖
	 *
	 *	@param	Entry	已加载的缓存条目
	 */
	void ApplyCachedEnvironment(const FLookDevEditorCacheEntry& Entry);

	/**
	 *	快照当前编辑器状态为缓存条目（环境属性 / 视口 FOV 与相机 / 副本掩码与生成标记）
	 *
	 *	@return	当前状态的缓存条目
	 */
	FLookDevEditorCacheEntry CaptureCacheEntry() const;

	/**
	 *	缓存防抖轮询：快照与最近写入的条目比较，有变化才写盘
	 *
	 *	@param	DeltaTime	Ticker 间隔（未使用）
	 *	@return	恒为 true（持续轮询）
	 */
	bool TickEditorCache(float DeltaTime);

	/**
	 *	缓存有变化时立即写盘（析构兜底，保证防抖窗口内的改动不丢）
	 */
	void FlushEditorCache();
};
