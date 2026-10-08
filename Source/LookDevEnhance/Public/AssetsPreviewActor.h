// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AssetsPreviewActor.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureCube;

/**
 * 灯光氛围档位
 *
 * 对应参考插件中的蓝图枚举 /LookDev/Assets/LightingScenarial，
 * 每个档位决定天光强度、背景材质 Far Value 基准值以及启用哪一盏方向光。
 */
UENUM(BlueprintType)
enum class ELightingScenarial : uint8
{
	// 低对比度：天光强度 2.5，背景 Far Value 0.18，启用 DirectionalLight_Low
	LowContrast,

	// 中对比度：天光强度 1.2，背景 Far Value 0.18，启用 DirectionalLight_Mid
	MidContrast,

	// 高对比度：天光强度 0.42，背景 Far Value 0.10，启用 DirectionalLight_High
	HighContrast
};

/**
 * 资产预览环境 Actor
 *
 * 由参考插件（LookDev）中的蓝图 AssetsPreview_BP 转换为 C++ 实现，作为 LookDev
 * 场景中的资产展示环境：天穹背景、天光、三档方向光、后处理（含 PBR Checker 混合）
 * 以及校准参照物（色卡、人台、平面、圆柱）。
 * 组件与参数的默认值通过构造函数初始化；光照氛围、曝光、背景尺寸等均可在编辑器中调整，
 * 调整后在 OnConstruction 中实时生效。
 *
 * 注意：本类位于 Editor 类型模块中，仅在编辑器内可用，不会进入打包产物。
 */
UCLASS(Blueprintable, HideCategories = ("HLOD", "Replication", "Collision", "Physics", "Networking", "Input", "WorldPartition", "Cooking"))
class LOOKDEVENHANCE_API AAssetsPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	// 根组件
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DefaultSceneRoot;

	// 天穹背景网格组件，运行时由 Mesh 属性指定网格并按 Size 缩放
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Skydome;

	// 天光组件
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> Skylight;

	// 低对比度档位的方向光
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight_Low;

	// 中对比度档位的方向光
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight_Mid;

	// 高对比度档位的方向光
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight_High;

	// 主后处理体积（曝光、对比度、暗角等）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess;

	// PBR Checker 专用的后处理体积，通过 WeightedBlendables 注入校验材质
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess_PBRChecker;

	// 校准参照物的生成位置标记
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SpawnLocation;

	// 校准用平面
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Plane;

	// 校准用圆柱
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Cylinder;

	// 校色卡网格组件
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ColorCalibrator;

	// 角色参考网格组件
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CharacterMesh;

	// 预览资产的朝向偏移：用于校正正面不朝 +X 方向的资产，原预览 Actor 与多视图副本会同步叠加该偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Preview Rotation Offset", ToolTip = "Rotation offset for preview assets whose front does not face the +X axis; applied to both the original preview actors and the multi-view copies"))
	FRotator PreviewRotationOffset;

	// 当前灯光氛围档位
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Lighting Scenarios", ToolTip = "Current lighting scenario preset"))
	ELightingScenarial LightingScenarios;

	// 曝光补偿（写入主后处理的 AutoExposureBias）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (UIMin = "-15", UIMax = "15", DisplayName = "Exposure Compensation", ToolTip = "Exposure compensation written to the main post-process AutoExposureBias"))
	float ExposureCompensation;

	// 背景亮度补偿指数，背景材质实际参数为 Far Value * 2^补偿值
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Background Value Compensation", ToolTip = "Background brightness compensation exponent; the actual backdrop material value is Far Value * 2^compensation"))
	float BackgroundValueCompensation;

	// 是否开启光线追踪阴影（直接设置三盏方向光组件的 CastRaytracedShadow 属性）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Raytracing Shadow", ToolTip = "Enable ray-traced shadows (sets CastRaytracedShadow on the three directional lights)"))
	bool bRaytracingShadow;

	// 是否为三盏方向光启用透射（Transmission）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Enable Transmission", ToolTip = "Enable transmission on the three directional lights"))
	bool bEnableTransmission;

	// 是否使用硬件光线追踪（作为 Command 控制台命令拼接的 0/1 参数）
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Use Hardware Raytracing", ToolTip = "Use hardware ray tracing (appended as the 0/1 argument of the Command console command)"))
	bool bUseHardWareRaytracing;

	// 硬件光线追踪的控制台命令前缀（自带尾随空格），与 bUseHardWareRaytracing 拼接后执行；当前 OnConstruction 中的执行调用已注释停用
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Command", ToolTip = "Hardware ray tracing console command prefix (trailing space included), executed combined with bUseHardWareRaytracing; the execution call in OnConstruction is currently commented out"))
	FString Command;

	// 是否旋转灯光
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Turn Light", ToolTip = "Rotate the lights continuously"))
	bool bTurnLight;

	// 灯光每帧旋转的角度增量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (DisplayName = "Turn Lighting", ToolTip = "Per-frame rotation increment of the lights"))
	float TurnLighting;

	// 自定义背景网格，替换 Skydome 使用的网格体
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Background", meta = (DisplayName = "Mesh", ToolTip = "Custom backdrop mesh replacing the Skydome mesh"))
	TObjectPtr<UStaticMesh> Mesh;

	// 背景网格的目标尺寸（米）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Background", meta = (UIMin = "1.0", ClampMin = "1.0", DisplayName = "Size", ToolTip = "Target size of the backdrop mesh in meters"))
	float Size;

	// 供 Skylight 取样并投射到背景的 HDR 立方体贴图，由蓝图子类接入使用
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Background", meta = (DisplayName = "Cubemap", ToolTip = "HDR cubemap sampled by the Skylight and projected onto the backdrop; assigned by the Blueprint subclass"))
	TObjectPtr<UTextureCube> Cubemap;

	// 背景是否贴地：贴地时 MaskRadius 取 10000，否则取 0.1 并设置 PDO 参数；PBR Checker 开启时强制视为非贴地
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (DisplayName = "Cast Ground Shadow", ToolTip = "Whether the backdrop touches the ground: grounded sets MaskRadius to 10000, otherwise 0.1 with PDO parameters; forced non-grounded while PBR Checker is enabled"))
	bool bCastGroundShadow;

	// 是否显示校准参照物
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (DisplayName = "Show Calibrator", ToolTip = "Show the calibration props"))
	bool bShowCalibrator;

	// 是否启用 PBR Checker（绿=BaseColor 过亮 / 蓝=过暗 / 红=Metallic BaseColor 过暗）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PBR Checker", meta = (DisplayName = "Enable Checker", ToolTip = "Enable the PBR Checker (green = BaseColor too bright / blue = too dark / red = Metallic BaseColor too dark)"))
	bool bEnableChecker;

	// PBR Checker 非金属校验的上限阈值
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "PBR Checker", meta = (DisplayName = "Max Value None Metal", ToolTip = "Upper threshold of the PBR Checker non-metal validation"))
	float MaxValueNoneMetal;

	// PBR Checker 非金属校验的下限阈值
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "PBR Checker", meta = (DisplayName = "Min Value None Metal", ToolTip = "Lower threshold of the PBR Checker non-metal validation"))
	float MinValueNoneMetal;

	// PBR Checker 金属校验的下限阈值
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "PBR Checker", meta = (DisplayName = "Min Value Metal", ToolTip = "Lower threshold of the PBR Checker metal validation"))
	float MinValueMetal;

	// 背景动态材质的源材质，由蓝图子类指定
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Asset Reference", meta = (DisplayName = "Backdrop Material", ToolTip = "Source material of the backdrop dynamic material; assigned by the Blueprint subclass"))
	TObjectPtr<UMaterialInterface> BackDropMaterial;

	// PBR Checker 动态材质的父材质，由蓝图子类指定
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Asset Reference", meta = (DisplayName = "PBR Checker Material", ToolTip = "Parent material of the PBR Checker dynamic material; assigned by the Blueprint subclass"))
	TObjectPtr<UMaterialInterface> PBRCheckerMaterial;

private:
	// 驱动灯光持续旋转的循环定时器
	FTimerHandle TurnLightingTimerHandle;

	// 是否已捕获三盏方向光与天光的基准姿态（序列化保存，避免地图重开后把已偏移的姿态误作基准再次叠加）
	UPROPERTY()
	bool bLightingBaseCaptured;

	// 三盏方向光的基准世界旋转（首次 Update 时捕获，此后的旋转都基于它叠加偏移）
	UPROPERTY()
	FRotator BaseRotationLow;

	UPROPERTY()
	FRotator BaseRotationMid;

	UPROPERTY()
	FRotator BaseRotationHigh;

	// 天光的基准 Cubemap 角度
	UPROPERTY()
	float SkylightBaseCubemapAngle;

	// 转台模式下的累计旋转角度
	UPROPERTY()
	float TurntableAngle;

public:
	AAssetsPreviewActor();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/**
	 *	刷新整个预览环境
	 *
	 *	按当前属性值全量重建预览状态：旋转灯光、开关 PBR Checker 后处理、
	 *	更新校准参照物、重置 Actor 变换、设置光线追踪阴影与透射、缩放背景天穹、
	 *	应用灯光氛围档位、写入主后处理与 PBR Checker 阈值。
	 *	由 OnConstruction 调用，属性在编辑器中修改后自动生效。
	 *
	 *	@note	可重写以在子类中扩展刷新逻辑，重写时应调用基类实现
	 *	@see	OnConstruction
	 */
	virtual void Update();

	/**
	 *	按参数设置三盏方向光的光线追踪阴影
	 *
	 *	@param	bEnable	true 启用（ECastRayTracedShadow::Enabled），false 禁用
	 *
	 *	@note	天光恒为禁用：其全屏遮蔽追踪是 GPU 崩溃的嫌疑路径。Update 中以
	 *		bRaytracingShadow 调用本函数；LookDev 编辑器在场景流送首帧会以 false 调用、
	 *		待首帧渲染后再恢复，规避同帧创建 MaterialSBT 又立即 dispatch RT 阴影导致的挂死
	 */
	void ApplyRayTracingShadowMode(bool bEnable);

protected:
	/**
	 *	创建并配置背景与 PBR Checker 的动态材质实例
	 *
	 *	@param	InFarValue					背景材质 Far Value 参数的基准值
	 *	@param	InBackgroundValue			背景亮度补偿指数，实际参数为 InFarValue * 2^InBackgroundValue
	 *	@param	bEffectiveCastGroundShadow	生效的贴地状态（PBR Checker 开启时被强制为非贴地）
	 */
	void CreateMaterial(float InFarValue, float InBackgroundValue, bool bEffectiveCastGroundShadow);

	/**
	 *	隐藏全部三盏方向光，供灯光氛围档位切换前先复位
	 */
	void ResetLight();

	/**
	 *	显示校准参照物。
	 */
	void ViewCalibrator();

	/**
	 *	隐藏校准参照物.
	 */
	void ResetCalibrator();

	/**
	 *	转台定时器回调：累计旋转角度并应用到灯光
	 *
	 *	由 BeginPlay 在 bTurnLight 为 true 时启动的循环定时器驱动，每帧把 TurnLighting 累加进
	 *	TurntableAngle 后绝对设置灯光姿态（基准姿态 + 偏移），不做增量叠加。
	 */
	void TurningLighting();

	/**
	 *	捕获三盏方向光与天光的基准姿态
	 *
	 *	仅在首次 Update 时调用一次（此时 Actor 变换已归零、蓝图子类的组件默认值已就位），
	 *	之后所有灯光旋转都以捕获值为基准叠加偏移，保证编辑属性时灯光不会持续累积旋转。
	 */
	void CaptureLightingBase();

	/**
	 *	按给定角度应用灯光姿态（基准姿态绕世界 Z 轴叠加偏移，天光 Cubemap 角度反向同步）
	 *
	 *	@param	InAngle	旋转偏移角度。bTurnLight 为 true 时传入累计的 TurntableAngle，
	 *					为 false 时传入 TurnLighting 作为固定偏移
	 */
	void ApplyLightingRotation(float InAngle);
};
