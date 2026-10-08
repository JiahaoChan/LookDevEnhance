// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class AAssetsPreviewActor;
class UPrimitiveComponent;
class USkeletalMeshComponent;

/**
 * 延迟开启 RT 阴影的共享状态
 *
 * 预览场景的首个渲染帧会首次创建 MaterialSBT；同帧立即 dispatch RT 阴影会导致
 * GPU 挂死。因此场景流送后先禁用 RT 阴影，待视口渲染过 EnableFrame 之后
 * 再按资产上的 bRaytracingShadow 设置恢复。由工具集填充、视口客户端消费。
 */
struct FLookDevDeferredRTShadowState
{
	// 预览环境 Actor，到帧后对其调用 ApplyRayTracingShadowMode(true)
	TWeakObjectPtr<AAssetsPreviewActor> EnvironmentActor;

	// 可以恢复 RT 阴影的帧号（GFrameCounter 比较）
	uint64 EnableFrame = 0;
};

/**
 * LookDev 编辑器的视口客户端
 *
 * 点击选择被限制在生成的预览 Actor 上（选中其根组件）；从 LookDev 场景流送进来的
 * Actor 永远无法被选中或变换。选择结果写入 ModeManager 的组件选择集，
 * 作为编辑器变换 Gizmo（平移 / 旋转 / 缩放）的作用目标。
 */
class FLookDevViewportClient : public FEditorViewportClient
{

private:
    // 用户允许选择和变换的预览 Actor；除此之外的对象（LookDev 场景）全部锁定
    const TArray<TWeakObjectPtr<AActor>>* PreviewActors;

    // 延迟开启 RT 阴影的共享状态，到帧后恢复并重置
    TSharedPtr<FLookDevDeferredRTShadowState> DeferredRTShadows;

    // 预览辅助显示开关（G 键切换）：选中高亮描边 + 变换 gizmo + 视口左下角坐标轴指示器
    bool bPreviewHelpersEnabled = true;

public:
	// 打开编辑器时的默认相机朝向（略微俯视的正面视角），SpawnPreviewForAssets 初始机位与 ViewDirection 菜单的 Default 项共用
	inline static const FRotator DefaultViewRotation = FRotator(-15.0, -155.0, 0.0);

public:
	FLookDevViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TArray<TWeakObjectPtr<AActor>>* InPreviewActors, TSharedPtr<FLookDevDeferredRTShadowState> InDeferredRTShadows = nullptr);

	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;

	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;

	virtual void Tick(float InDeltaSeconds) override;

	void SetEditorViewportWidget(const TSharedPtr<SEditorViewport>& InEditorViewportWidget) { EditorViewportWidget = InEditorViewportWidget; }

	/**
	 *	重推全部预览组件的选择描边状态
	 *
	 *	选择集变化后渲染代理上的选中标记不会自动更新（关卡编辑器由 TypedElement 选择定制器代劳，
	 *	资产编辑器需自行推送），由工具集在选择变化回调中调用
	 */
	void RefreshSelectionOutlineProxies();

	/**
	 *	解除全部预览组件上的选中判定委托绑定（预览世界销毁前调用，防止野委托回调）
	 */
	void ClearSelectionOutlineOverrides();

	/**
	 *	开关预览辅助显示（G 键）：选中高亮描边 + 变换 gizmo + 视口左下角坐标轴指示器
	 *
	 *	Selection 与 SelectionOutline 两个 show flags 同时开关（描边后处理的启用条件要求两者都开，
	 *	PostProcessing.cpp 的 EPass::SelectionOutline 门控），与构造函数里恢复 show flags 的分支一致，
	 *	开启时 SelectionOutline 对齐关卡编辑器读用户的编辑器偏好；
	 *	ShowWidget 控制变换 gizmo 的绘制，bDrawAxesGame 控制游戏视图下左下角坐标轴指示器
	 *
	 *	@param	bEnabled	是否启用
	 */
	void SetPreviewHelpersEnabled(bool bEnabled);

	/**
	 *	预览辅助显示当前状态（G 键切换）
	 */
	bool IsPreviewHelpersEnabled() const { return bPreviewHelpersEnabled; }

	/**
	 *	将摄像机归位到指定朝向并聚焦预览资产
	 *
	 *	先把视口旋转设为目标朝向，再按 F 键聚焦的同一套包围盒逻辑平滑聚焦过去
	 *
	 *	@param	InViewRotation	目标视口旋转
	 */
	void FocusFromViewDirection(const FRotator& InViewRotation);

	/**
	 *	聚焦骨骼模型预览的 head 骨骼（正脸视角的聚焦目标）
	 *
	 *	参考 Persona 骨骼选择模式的相机目标（SkeletonSelectionEditMode::GetCameraTarget）：
	 *	以骨骼当前位置为中心、30cm 半径构造聚焦包围盒；仅在 CanFocusHeadBone 成立时生效
	 */
	void FocusOnHeadBone();

	/**
	 *	判断当前是否可以聚焦 head 骨骼
	 *
	 *	优先当前选中的预览 Actor（多资产同开时先选中要看的角色），未选中时回退到
	 *	"恰好单个预览 Actor"的情况；目标须是骨骼模型且骨架中含 head 骨骼
	 */
	bool CanFocusHeadBone() const;

private:
	/**
	 *	计算聚焦包围盒：优先选中的预览组件，未选中时取全部预览 Actor
	 *
	 *	@return	聚焦包围盒，无可聚焦内容时返回无效 Box
	 */
	FBox ComputeFocusBounds() const;

	/**
	 *	找回可用于 head 骨骼聚焦的骨骼网格组件
	 *
	 *	优先当前选中的预览 Actor 的骨骼组件（多资产同开时先选中要看的角色），
	 *	未选中时回退到"恰好单个预览 Actor"的情况；组件骨架须含 head 骨骼，否则返回 nullptr
	 *
	 *	@return	含 head 骨骼的骨骼网格组件，无可用目标时返回 nullptr
	 */
	USkeletalMeshComponent* GetHeadFocusSkeletalComponent() const;

	/**
	 *	选中判定委托回调：组件是否在当前组件选择集中
	 *
	 *	绑定在预览组件的 SelectionOverrideDelegate 上（对齐静态网格编辑器的做法），
	 *	渲染线程画选择描边时经 IsComponentIndividuallySelected 查询本回调
	 *
	 *	@param	InComponent	要判定的预览组件
	 */
	bool IsPreviewComponentSelected(const UPrimitiveComponent* InComponent) const;

	/**
	 *	给尚未绑定选中判定委托的预览 PrimitiveComponent 补绑并推送一次选中状态
	 *
	 *	在 Tick 中轮询（对齐 FSCSEditorViewportClient::Tick 的做法），
	 *	覆盖后生成的预览 Actor 与多视图副本
	 */
	void EnsureSelectionOutlineOverrides();
};
