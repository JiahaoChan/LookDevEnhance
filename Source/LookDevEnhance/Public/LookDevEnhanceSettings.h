// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "LookDevEnhanceSettings.generated.h"

struct FAssetData;

/**
 * LookDev Enhance 插件的项目级配置
 *
 * 集中管理 LookDev 编辑器的可配置项：作为预览环境的 LookDev 场景、
 * 允许在 LookDev 编辑器中打开的资产类型，以及预览材质时使用的网格体。
 * 配置序列化到 DefaultEngine.ini，通过静态函数 Get() 访问单例；
 * 内容浏览器菜单扩展（FLookDevContentBrowserExtensions）通过
 * CanOpenInLookDevEditor 判断某个资产是否支持在 LookDev 编辑器中打开。
 */

/**
 * LookDev 资产编辑器的配置面板。
 * 在这里指定预览环境使用的 LookDev 场景、允许在 LookDev 编辑器中打开的资产类型，
 * 以及预览材质时使用的网格体；修改后自动保存并即时生效，无需重启编辑器。
 */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "LookDev Enhance"))
class ULookDevEnhanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 作为 LookDev 编辑器预览环境的场景（地图资产）
	UPROPERTY(Config, EditAnywhere, Category = "LookDev", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> LookDevScene;

	// 允许通过内容浏览器右键菜单在 LookDev 编辑器中打开的资产类（含其子类）
	UPROPERTY(Config, EditAnywhere, Category = "LookDev")
	TArray<TSoftClassPtr<UObject>> SupportedAssetClasses;

	// 在 LookDev 编辑器中预览材质 / 材质实例时使用的网格体
	UPROPERTY(Config, EditAnywhere, Category = "LookDev")
	TSoftObjectPtr<UStaticMesh> MaterialPreviewMesh;

	ULookDevEnhanceSettings();

	/**
	 *	判断给定的内容浏览器资产是否可以在 LookDev 编辑器中打开
	 *
	 *	蓝图资产按资产注册表的 NativeParentClass 标签取原生父类判断（配置里配的是原生类）
	 *
	 *	@param	AssetData	要判断的资产
	 *	@return	资产类（蓝图资产为原生父类）是 SupportedAssetClasses 中任意一类的子类时返回 true，否则返回 false
	 */
	bool CanOpenInLookDevEditor(const FAssetData& AssetData) const;
};
