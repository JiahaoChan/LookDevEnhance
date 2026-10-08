// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "AssetsPreviewActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

AAssetsPreviewActor::AAssetsPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bEnableAutoLODGeneration = false;

	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	RootComponent = DefaultSceneRoot;

	Skydome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Skydome"));
	Skydome->SetupAttachment(RootComponent);

	Skylight = CreateDefaultSubobject<USkyLightComponent>(TEXT("Skylight"));
	Skylight->SetupAttachment(RootComponent);
	Skylight->SetMobility(EComponentMobility::Movable);

	DirectionalLight_Low = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Directional Light Low"));
	DirectionalLight_Low->SetupAttachment(RootComponent);
	DirectionalLight_Low->SetMobility(EComponentMobility::Movable);

	DirectionalLight_Mid = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Directional Light Mid"));
	DirectionalLight_Mid->SetupAttachment(RootComponent);
	DirectionalLight_Mid->SetMobility(EComponentMobility::Movable);

	DirectionalLight_High = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Directional Light High"));
	DirectionalLight_High->SetupAttachment(RootComponent);
	DirectionalLight_High->SetMobility(EComponentMobility::Movable);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post Process"));
	PostProcess->SetupAttachment(RootComponent);

	PostProcess_PBRChecker = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post Process PBR Checker"));
	PostProcess_PBRChecker->SetupAttachment(RootComponent);

	SpawnLocation = CreateDefaultSubobject<USceneComponent>(TEXT("Spawn Location"));
	SpawnLocation->SetupAttachment(RootComponent);

	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	Plane->SetupAttachment(SpawnLocation);

	Cylinder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cylinder"));
	Cylinder->SetupAttachment(Plane);

	ColorCalibrator = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Color Calibrator"));
	ColorCalibrator->SetupAttachment(SpawnLocation);

	CharacterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Character Mesh"));
	CharacterMesh->SetupAttachment(SpawnLocation);

	PreviewRotationOffset = FRotator::ZeroRotator;

	LightingScenarios = ELightingScenarial::MidContrast;
	ExposureCompensation = 0.0f;
	BackgroundValueCompensation = 0.0;
	bRaytracingShadow = true;
	bEnableTransmission = true;
	bUseHardWareRaytracing = true;
	Command = TEXT("r.Lumen.HardwareRayTracing ");
	bTurnLight = false;
	TurnLighting = 0.0f;

	Size = 500.0f;
	bCastGroundShadow = true;
	bShowCalibrator = false;

	bEnableChecker = false;
	MaxValueNoneMetal = 0.9f;
	MinValueNoneMetal = 0.02f;
	MinValueMetal = 0.3f;

	bLightingBaseCaptured = false;
	BaseRotationLow = FRotator::ZeroRotator;
	BaseRotationMid = FRotator::ZeroRotator;
	BaseRotationHigh = FRotator::ZeroRotator;
	SkylightBaseCubemapAngle = 0.0f;
	TurntableAngle = 0.0f;
}

void AAssetsPreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Update();
}

void AAssetsPreviewActor::BeginPlay()
{
	Super::BeginPlay();

	if (bTurnLight)
	{
		GetWorldTimerManager().SetTimer(TurnLightingTimerHandle, this, &AAssetsPreviewActor::TurningLighting, 0.016f, true);
	}
}

void AAssetsPreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TurnLightingTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(TurnLightingTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AAssetsPreviewActor::Update()
{
	// PBR Checker 开启时后处理始终生效，且背景强制为非贴地模式
	PostProcess_PBRChecker->bEnabled = bEnableChecker;
	bool bEffectiveCastGroundShadow = bCastGroundShadow;
	if (bEnableChecker)
	{
		bEffectiveCastGroundShadow = false;
	}

	if (bShowCalibrator)
	{
		ViewCalibrator();
	}
	else
	{
		ResetCalibrator();
	}

	// 与原蓝图一致：每次构造都把 Actor 自身变换归零，避免拖拽累积位移
	SetActorTransform(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::OneVector));

	// 灯光基准姿态只捕获一次（此时 Actor 变换已归零、蓝图子类的组件默认值已就位），
	// 之后以绝对角度设置：转台开启时用累计角度，关闭时 TurnLighting 只是固定偏移，
	// 避免编辑属性触发 OnConstruction 时灯光持续累积旋转
	if (!bLightingBaseCaptured)
	{
		CaptureLightingBase();
		bLightingBaseCaptured = true;
	}
	ApplyLightingRotation(bTurnLight ? TurntableAngle : TurnLighting);

	// 直接通过组件属性控制三盏方向光与天光是否使用光线追踪阴影
	ApplyRayTracingShadowMode(bRaytracingShadow);

	if (Mesh)
	{
		Skydome->SetStaticMesh(Mesh);

		// 以包围球半径为基准把背景网格缩放到目标尺寸，Z 轴压扁为一半
		if (const float SphereRadius = Skydome->CalcBounds(FTransform::Identity).SphereRadius;
			SphereRadius > KINDA_SMALL_NUMBER)
		{
			const float SkyScale = Size / (SphereRadius / 50.0f);
			Skydome->SetWorldScale3D(FVector(SkyScale, SkyScale, SkyScale * 0.5f));
		}
	}

	// 按灯光氛围档位配置天光强度与背景材质，并只启用对应档位的方向光
	switch (LightingScenarios)
	{
		case ELightingScenarial::LowContrast:
			CreateMaterial(0.18f, BackgroundValueCompensation, bEffectiveCastGroundShadow);
			Skylight->SetIntensity(2.5f);
			ResetLight();
			DirectionalLight_Low->SetVisibility(true);
			break;
		case ELightingScenarial::MidContrast:
			CreateMaterial(0.18f, BackgroundValueCompensation, bEffectiveCastGroundShadow);
			Skylight->SetIntensity(1.2f);
			ResetLight();
			DirectionalLight_Mid->SetVisibility(true);
			break;
		case ELightingScenarial::HighContrast:
			CreateMaterial(0.10f, BackgroundValueCompensation, bEffectiveCastGroundShadow);
			Skylight->SetIntensity(0.42f);
			ResetLight();
			DirectionalLight_High->SetVisibility(true);
			break;
		default:
			break;
	}

	// 主后处理：手动曝光 + 阴影对比度 + 暗角，补偿值来自 ExposureCompensation
	FPostProcessSettings Settings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Settings.bOverride_AutoExposureBias = true;
    Settings.AutoExposureBias = ExposureCompensation;
    Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_ColorContrastShadows = true;
	Settings.ColorContrastShadows = FVector4(1.0f, 1.0f, 1.0f, 0.9f);
	Settings.bOverride_FilmToe = true;
	Settings.FilmToe = 0.5f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.3f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
	PostProcess->Settings = Settings;

	DirectionalLight_Low->SetTransmission(bEnableTransmission);
	DirectionalLight_Mid->SetTransmission(bEnableTransmission);
	DirectionalLight_High->SetTransmission(bEnableTransmission);

	// Command 自带尾随空格，拼接 0/1 后即为完整的控制台命令
	//UKismetSystemLibrary::ExecuteConsoleCommand(this, Command + FString::FromInt(bUseHardWareRaytracing ? 1 : 0));
}

void AAssetsPreviewActor::CreateMaterial(float InFarValue, float InBackgroundValue, bool bEffectiveCastGroundShadow)
{
	// 背景动态材质：贴地与非贴地走两套 Mask Radius / PDO 参数
	if (UMaterialInstanceDynamic* MID = Skydome->CreateDynamicMaterialInstance(0, BackDropMaterial))
	{
		MID->SetScalarParameterValue(TEXT("MaskOffset"), bEffectiveCastGroundShadow ? 1000.0f : 0.0f);
		MID->SetScalarParameterValue(TEXT("MaskRadius"), bEffectiveCastGroundShadow ? 5000.0f : 0.05f);
		MID->SetScalarParameterValue(TEXT("PDO"), bEffectiveCastGroundShadow ? 0.0f : 3000.0f);
		MID->SetScalarParameterValue(TEXT("FarValue"), InFarValue * FMath::Pow(2.0, InBackgroundValue));
	}

	if (UMaterialInstanceDynamic* MID = UKismetMaterialLibrary::CreateDynamicMaterialInstance(this, PBRCheckerMaterial))
	{
		// 把校验材质以满权重混入 PBR Checker 后处理体积
		FPostProcessSettings& CheckerSettings = PostProcess_PBRChecker->Settings;
		CheckerSettings.WeightedBlendables.Array.Reset();
		CheckerSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, MID));

		MID->SetScalarParameterValue(TEXT("Max_NoneMetallic"), MaxValueNoneMetal);
		MID->SetScalarParameterValue(TEXT("Min_Metallic"), MinValueMetal);
		MID->SetScalarParameterValue(TEXT("Min_NoneMetallic"), MinValueNoneMetal);
	}
}

void AAssetsPreviewActor::ResetLight()
{
	DirectionalLight_Low->SetVisibility(false);
	DirectionalLight_Mid->SetVisibility(false);
	DirectionalLight_High->SetVisibility(false);
}

void AAssetsPreviewActor::ViewCalibrator()
{
	SpawnLocation->SetVisibility(true, true);
}

void AAssetsPreviewActor::ResetCalibrator()
{
	SpawnLocation->SetVisibility(false, true);
}

void AAssetsPreviewActor::TurningLighting()
{
	TurntableAngle += TurnLighting;
	ApplyLightingRotation(TurntableAngle);
}

void AAssetsPreviewActor::CaptureLightingBase()
{
	BaseRotationLow = DirectionalLight_Low->GetComponentRotation();
	BaseRotationMid = DirectionalLight_Mid->GetComponentRotation();
	BaseRotationHigh = DirectionalLight_High->GetComponentRotation();
	SkylightBaseCubemapAngle = Skylight->SourceCubemapAngle;
}

void AAssetsPreviewActor::ApplyLightingRotation(float InAngle)
{
	// 方向光在基准姿态上绕世界 Z 轴（Yaw）叠加偏移，天光的 Cubemap 角度反向同步
	const FQuat OffsetQuat = FRotator(0.0f, InAngle, 0.0f).Quaternion();
	DirectionalLight_Low->SetWorldRotation((OffsetQuat * BaseRotationLow.Quaternion()).Rotator());
	DirectionalLight_Mid->SetWorldRotation((OffsetQuat * BaseRotationMid.Quaternion()).Rotator());
	DirectionalLight_High->SetWorldRotation((OffsetQuat * BaseRotationHigh.Quaternion()).Rotator());
	Skylight->SetSourceCubemapAngle(SkylightBaseCubemapAngle - InAngle);
}

void AAssetsPreviewActor::ApplyRayTracingShadowMode(bool bEnable)
{
	const ECastRayTracedShadow::Type RayTracedShadowMode = bEnable ? ECastRayTracedShadow::Enabled : ECastRayTracedShadow::Disabled;
	DirectionalLight_Low->SetCastRaytracedShadows(RayTracedShadowMode);
	DirectionalLight_Mid->SetCastRaytracedShadows(RayTracedShadowMode);
	DirectionalLight_High->SetCastRaytracedShadows(RayTracedShadowMode);

	// 天光不走 RT 阴影：其全屏遮蔽追踪是 GPU 崩溃的嫌疑路径（天光 scene proxy 修复后才首次激活），单独隔离验证
	Skylight->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
}
