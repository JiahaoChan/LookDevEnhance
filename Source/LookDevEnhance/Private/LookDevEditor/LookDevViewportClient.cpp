// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/LookDevViewportClient.h"

#include "AssetsPreviewActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Selection.h"
#include "ShaderCompiler.h"
#include "UnrealWidget.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

FLookDevViewportClient::FLookDevViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TArray<TWeakObjectPtr<AActor>>* InPreviewActors, TSharedPtr<FLookDevDeferredRTShadowState> InDeferredRTShadows)
	: FEditorViewportClient(InModeTools, InPreviewScene, TWeakPtr<SEditorViewport>())
	, PreviewActors(InPreviewActors)
	, DeferredRTShadows(MoveTemp(InDeferredRTShadows))
{
	// 实时覆盖保证视口不活跃时世界也会 Tick，否则光照 / 纹理不会更新（与 MetaHuman 编辑器一致）
	AddRealtimeOverride(true, LOCTEXT("RealTimeOverride", "LookDevRealTimeOverride"));
	SetRealtime(true);

	// 预览场景默认关闭部分高级特性，先开启再切游戏视图（顺序参考 MetaHuman 编辑器）
	EngineShowFlags.EnableAdvancedFeatures();

	// 游戏视图（相当于在关卡编辑器中按 G）：隐藏公告板、灯光图标等仅编辑器可见的辅助元素
	SetGameView(true);

	// 恢复被 SetGameView 关闭的编辑器辅助功能：
	// bDrawAxesGame 默认 false，游戏视图中不画左下角坐标轴指示器（EditorViewportClient.cpp:5006）；
	// SetGameView 内部会 ShowWidget(false)，而 gizmo 可见性检查 ViewportClient->GetShowWidget()
	//（EditorTransformGizmoSource.cpp:75），不恢复的话变换 gizmo 永远不显示
	bDrawAxesGame = true;
	ShowWidget(true);

	// 选择描边：SetGameView(true) 会关掉 Selection 与 SelectionOutline（EditorViewportClient.cpp 的 SetGameView 分支），
	// 而描边后处理的启用条件要求两者都开（PostProcessing.cpp 的 EPass::SelectionOutline 门控），这里恢复；
	// SelectionOutline 的取值对齐关卡编辑器（读用户的编辑器偏好，FStaticMeshEditorViewportClient 同款做法）
	EngineShowFlags.SetSelection(true);
	EngineShowFlags.SetSelectionOutline(GetDefault<ULevelEditorViewportSettings>()->bUseSelectionOutline);

	// 变换 gizmo 的默认模式：FEditorModeTools 的 WidgetMode 初值是 WM_None（不画任何 gizmo），
	// 关卡编辑器在视口构造函数里显式设为平移（LevelEditorViewport.cpp:2380），这里对齐
	if (ModeTools)
	{
		ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
	}
}

void FLookDevViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

	// 引擎的两条点击路径（ITF 点击行为与旧版追踪）都是在按键松开时派发 ProcessClick
	if (Event != IE_Released || Key != EKeys::LeftMouseButton || ModeTools == nullptr)
	{
		return;
	}

	USceneComponent* HitComponent = nullptr;
	if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorProxy = static_cast<HActor*>(HitProxy);
		HitComponent = const_cast<UPrimitiveComponent*>(ActorProxy->PrimComponent.Get());
	}
	else if (HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType()))
	{
		// 点击落在变换 gizmo 的轴向上：不改变选择，保留当前选中供拖拽
		return;
	}

	// 命中组件按 Owner 归属解析为被跟踪的预览 Actor；选中其根组件供 gizmo / 拖拽 /
	// 详情面板使用（跟踪以 Actor 为单位，选择机制仍走组件选择集）
	AActor* PreviewHitActor = nullptr;
	if (HitComponent != nullptr && PreviewActors != nullptr)
	{
		AActor* HitOwner = HitComponent->GetOwner();
		if (HitOwner != nullptr && PreviewActors->Contains(HitOwner))
		{
			PreviewHitActor = HitOwner;
		}
	}

	USelection* SelectedComponents = ModeTools->GetSelectedComponents();
	USceneComponent* PreviewHitRoot = PreviewHitActor ? PreviewHitActor->GetRootComponent() : nullptr;

	if (PreviewHitRoot)
	{
		if (IsCtrlPressed())
		{
			// Ctrl + 点击切换选中状态，与关卡编辑器的操作惯例一致
			if (SelectedComponents->IsSelected(PreviewHitRoot))
			{
				SelectedComponents->Deselect(PreviewHitRoot);
			}
			else
			{
				SelectedComponents->Select(PreviewHitRoot);
			}
		}
		else if (IsShiftPressed())
		{
			// Shift + 点击追加选中，不改变已选中的其他组件
			SelectedComponents->Select(PreviewHitRoot);
		}
		else
		{
			SelectedComponents->DeselectAll();
			SelectedComponents->Select(PreviewHitRoot);
		}
	}
	else if (!IsCtrlPressed() && !IsShiftPressed())
	{
		// 点击 LookDev 场景或空白处不会选中任何对象，场景 Actor 保持锁定
		SelectedComponents->DeselectAll();
	}

	// USelection::Select 走元素集 Added 同步，只广播 SelectObjectEvent，不广播 SelectionChangedEvent
	//（Selection.cpp 的 FLegacySync 通知分支），详情面板监听的 SelectionChangedEvent 收不到新增选中；
	// 选择集只在本函数里被修改，统一在末尾手动广播一次选择变化
	SelectedComponents->NoteSelectionChanged();
}

bool FLookDevViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// F 键聚焦：优先聚焦选中的预览组件，未选中时聚焦全部预览 Actor
	if (EventArgs.Key == EKeys::F && EventArgs.Event == IE_Pressed)
	{
		const FBox Bounds = ComputeFocusBounds();
		if (Bounds.IsValid)
		{
			FocusViewportOnBox(Bounds);
			return true;
		}
	}

	// G 键切换预览辅助显示（选中高亮描边 + 变换 gizmo + 左下角坐标轴指示器）。
	// 本编辑器恒为 GameView，重新赋予 G 键语义；必须拦截在基类之前，
	// 否则基类会把 G 当作 GameView 总开关（FEditorViewportClient::InputKey 的 SetGameView 分支）
	if (EventArgs.Key == EKeys::G && EventArgs.Event == IE_Pressed)
	{
		SetPreviewHelpersEnabled(!bPreviewHelpersEnabled);
		return true;
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

bool FLookDevViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (ModeTools == nullptr || PreviewActors == nullptr)
	{
		return false;
	}

	USelection* SelectedComponents = ModeTools->GetSelectedComponents();

	// 选中组件按 Owner 去重（同一 Actor 多个组件被选中时只动一次）；
	// 只动被跟踪的预览 Actor，LookDev 场景 Actor 永远锁定
	const FVector Pivot = ModeTools->PivotLocation;
	const FQuat RotQuat = Rot.Quaternion();

	TSet<AActor*> MovedActors;
	for (FSelectionIterator It(*SelectedComponents); It; ++It)
	{
		USceneComponent* Component = Cast<USceneComponent>(*It);
		AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (Owner == nullptr || MovedActors.Contains(Owner) || !PreviewActors->Contains(Owner))
		{
			continue;
		}
		MovedActors.Add(Owner);

		if (!Drag.IsZero())
		{
			Owner->AddActorWorldOffset(Drag, /*bSweep=*/ false);
		}
		// 旋转 / 缩放围绕 gizmo 枢轴（PivotLocation），与关卡编辑器的多选变换行为一致
		if (!Rot.IsZero())
		{
			Owner->SetActorLocation(Pivot + RotQuat.RotateVector(Owner->GetActorLocation() - Pivot));
			Owner->AddActorWorldRotation(RotQuat);
		}
		if (!Scale.IsZero())
		{
			Owner->SetActorScale3D(Owner->GetActorScale3D() + Scale);
			Owner->SetActorLocation(Pivot + (Owner->GetActorLocation() - Pivot) * (FVector::OneVector + Scale));
		}
	}

	if (MovedActors.Num() == 0)
	{
		return false;
	}

	// 与基类 InputWidgetDelta 一致：平移同步枢轴与吸附基准，旋转刷新旋转 gizmo 的视觉反馈
	ModeTools->PivotLocation += Drag;
	ModeTools->SnappedLocation += Drag;
	ApplyDeltaToRotateWidget(Rot);
	return true;
}

void FLookDevViewportClient::Tick(float InDeltaSeconds)
{
	FEditorViewportClient::Tick(InDeltaSeconds);

	// 恢复预览环境 Actor 的 RT 阴影：首帧已完成 MaterialSBT 创建，
	// 且要等着色器编译队列清空（新编译的 RT 着色器立即 dispatch 会导致 GPU 挂死）；带超时兜底防止编辑器后台编译永远不空
	if (DeferredRTShadows && DeferredRTShadows->EnvironmentActor.IsValid() && GFrameCounter >= DeferredRTShadows->EnableFrame)
	{
		const bool bShadersReady = GShaderCompilingManager == nullptr || !GShaderCompilingManager->IsCompiling();
		const bool bTimedOut = GFrameCounter >= DeferredRTShadows->EnableFrame + 600;
		if (bShadersReady || bTimedOut)
		{
			DeferredRTShadows->EnvironmentActor->ApplyRayTracingShadowMode(true);
			DeferredRTShadows.Reset();
		}
	}

	// 预览世界不会被引擎代为 Tick，由视口驱动
	if (GetPreviewScene() != nullptr)
	{
		GetPreviewScene()->GetWorld()->Tick(LEVELTICK_ViewportsOnly, InDeltaSeconds);

		// 指定 Cubemap 的天光要经 UpdateCaptureContents 处理出纹理后才会创建 scene proxy，
		// 裸 FPreviewScene 不会自动调用（对标 FAdvancedPreviewScene::Tick）
		GetPreviewScene()->UpdateCaptureContents();
	}

	// 给新生成的预览组件补绑选中判定委托（选择描边用，覆盖后生成的多视图副本）
	EnsureSelectionOutlineOverrides();
}

void FLookDevViewportClient::RefreshSelectionOutlineProxies()
{
	if (PreviewActors == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : *PreviewActors)
	{
		AActor* Actor = WeakActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component->SelectionOverrideDelegate.IsBound())
			{
				Component->PushSelectionToProxy();
			}
		}
	}
}

void FLookDevViewportClient::ClearSelectionOutlineOverrides()
{
	if (PreviewActors == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : *PreviewActors)
	{
		AActor* Actor = WeakActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (Component->SelectionOverrideDelegate.IsBound())
			{
				Component->SelectionOverrideDelegate.Unbind();
				Component->PushSelectionToProxy();
			}
		}
	}
}

void FLookDevViewportClient::SetPreviewHelpersEnabled(bool bEnabled)
{
	bPreviewHelpersEnabled = bEnabled;
	// 描边后处理的启用条件要求 Selection 与 SelectionOutline 都开（PostProcessing.cpp 的 EPass::SelectionOutline 门控），
	// 与构造函数里恢复 show flags 的分支一致；开启时 SelectionOutline 对齐关卡编辑器读用户的编辑器偏好
	EngineShowFlags.SetSelection(bEnabled);
	EngineShowFlags.SetSelectionOutline(bEnabled && GetDefault<ULevelEditorViewportSettings>()->bUseSelectionOutline);
	// bDrawAxesGame 控制游戏视图下左下角坐标轴指示器（EditorViewportClient.cpp:5006）；
	// ShowWidget 控制变换 gizmo 的绘制（可见性检查 ViewportClient->GetShowWidget()，EditorTransformGizmoSource.cpp:75）
	bDrawAxesGame = bEnabled;
	ShowWidget(bEnabled);
	Invalidate();
}

void FLookDevViewportClient::FocusFromViewDirection(const FRotator& InViewRotation)
{
	const FBox Bounds = ComputeFocusBounds();
	if (!Bounds.IsValid)
	{
		return;
	}

	// 先归位朝向再聚焦：FocusViewportOnBox 按当前视口朝向反推摄像机位置，默认平滑过渡
	SetViewRotation(InViewRotation);
	FocusViewportOnBox(Bounds);
}

void FLookDevViewportClient::FocusOnHeadBone()
{
	static const FName HeadBoneName(TEXT("head"));

	USkeletalMeshComponent* SkeletalComponent = GetHeadFocusSkeletalComponent();
	if (SkeletalComponent == nullptr)
	{
		return;
	}

	// 与 Persona 骨骼聚焦一致（FSkeletonSelectionEditMode::GetCameraTarget + FocusViewportOnSphere）：
	// 骨骼当前位置为中心、30cm 半径；包围盒只沿 X 轴展开，使聚焦距离精确等于半径换算值
	constexpr float HeadFocusRadius = 30.0f;
	const FVector HeadLocation = SkeletalComponent->GetBoneLocation(HeadBoneName);
	const FBox HeadBounds(HeadLocation - FVector(HeadFocusRadius, 0.0f, 0.0f), HeadLocation + FVector(HeadFocusRadius, 0.0f, 0.0f));
	FocusViewportOnBox(HeadBounds);
}

bool FLookDevViewportClient::CanFocusHeadBone() const
{
	return GetHeadFocusSkeletalComponent() != nullptr;
}

FBox FLookDevViewportClient::ComputeFocusBounds() const
{
	FBox Bounds(EForceInit::ForceInit);

	if (ModeTools)
	{
		USelection* SelectedComponents = ModeTools->GetSelectedComponents();
		for (FSelectionIterator It(*SelectedComponents); It; ++It)
		{
			if (USceneComponent* Component = Cast<USceneComponent>(*It))
			{
				Bounds += Component->CalcBounds(Component->GetComponentTransform()).GetBox();
			}
		}
	}

	if (!Bounds.IsValid && PreviewActors != nullptr)
	{
		for (const TWeakObjectPtr<AActor>& Actor : *PreviewActors)
		{
			if (const AActor* ValidActor = Actor.Get())
			{
				// 以 Actor 为单位取全部组件的包围盒（含挂在根组件下的预览网格子组件）
				Bounds += ValidActor->GetComponentsBoundingBox(/*bNonColliding=*/ true);
			}
		}
	}

	return Bounds;
}

USkeletalMeshComponent* FLookDevViewportClient::GetHeadFocusSkeletalComponent() const
{
	static const FName HeadBoneName(TEXT("head"));

	// 函数内静态变量无需捕获即可在 lambda 中使用
	auto GetSkeletalComponentWithHeadBone = [](const AActor* Actor) -> USkeletalMeshComponent*
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Component = Actor->FindComponentByClass<USkeletalMeshComponent>();
		return (Component != nullptr && Component->GetBoneIndex(HeadBoneName) != INDEX_NONE) ? Component : nullptr;
	};

	// 优先当前选中的预览 Actor（多资产同开时先选中要看的角色）
	if (ModeTools != nullptr && PreviewActors != nullptr)
	{
		USelection* SelectedComponents = ModeTools->GetSelectedComponents();
		for (FSelectionIterator It(*SelectedComponents); It; ++It)
		{
			const UActorComponent* SelectedComponent = Cast<UActorComponent>(*It);
			const AActor* Owner = SelectedComponent != nullptr ? SelectedComponent->GetOwner() : nullptr;
			if (Owner != nullptr && PreviewActors->Contains(Owner))
			{
				if (USkeletalMeshComponent* Component = GetSkeletalComponentWithHeadBone(Owner))
				{
					return Component;
				}
			}
		}
	}

	// 回退：恰好单个预览 Actor 时无需选择即可聚焦
	if (PreviewActors != nullptr && PreviewActors->Num() == 1)
	{
		return GetSkeletalComponentWithHeadBone(PreviewActors->operator[](0).Get());
	}
	return nullptr;
}

bool FLookDevViewportClient::IsPreviewComponentSelected(const UPrimitiveComponent* InComponent) const
{
	return ModeTools != nullptr && InComponent != nullptr
		&& ModeTools->GetSelectedComponents()->IsSelected(const_cast<UPrimitiveComponent*>(InComponent));
}

void FLookDevViewportClient::EnsureSelectionOutlineOverrides()
{
	if (PreviewActors == nullptr)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : *PreviewActors)
	{
		AActor* Actor = WeakActor.Get();
		if (Actor == nullptr)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			// 预览世界由本编辑器独占，委托只会是本客户端绑的；未绑定才补绑并推送初始状态
			if (!Component->SelectionOverrideDelegate.IsBound())
			{
				Component->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FLookDevViewportClient::IsPreviewComponentSelected);
				Component->PushSelectionToProxy();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
