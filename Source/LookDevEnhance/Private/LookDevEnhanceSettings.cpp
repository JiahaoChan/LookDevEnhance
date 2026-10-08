// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEnhanceSettings.h"

#include "AssetRegistry/AssetData.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"

ULookDevEnhanceSettings::ULookDevEnhanceSettings()
{
	SupportedAssetClasses.Add(UStaticMesh::StaticClass());
	SupportedAssetClasses.Add(USkeletalMesh::StaticClass());
	SupportedAssetClasses.Add(UMaterialInterface::StaticClass());

	MaterialPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
}

bool ULookDevEnhanceSettings::CanOpenInLookDevEditor(const FAssetData& AssetData) const
{
	const UClass* AssetClass = AssetData.GetClass();
	if (!AssetClass)
	{
		return false;
	}

	// 蓝图资产：AssetClass 是 UBlueprint，而配置里配的是原生父类（NativeParentClass），
	// 从资产注册表标签取原生父类路径替换后继续判断（标签由 UBlueprint::GetAssetRegistryTags 导出，
	// 无需加载资产本体）；IsChildOf 循环同时涵盖原生父类是配置类子类的情况
	if (AssetClass->IsChildOf(UBlueprint::StaticClass()))
	{
		const FString& NativeParentPath = AssetData.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
		if (NativeParentPath.IsEmpty())
		{
			return false;
		}
		AssetClass = FSoftClassPath(NativeParentPath).ResolveClass();
		if (AssetClass == nullptr)
		{
			// 原生父类未加载时同步加载（原生类常驻内存，代价低）
			AssetClass = FSoftClassPath(NativeParentPath).TryLoadClass<UObject>();
		}
		if (AssetClass == nullptr)
		{
			return false;
		}
	}

	for (const TSoftClassPtr<UObject>& SupportedClass : SupportedAssetClasses)
	{
		if (const UClass* LoadedSupportedClass = SupportedClass.LoadSynchronous())
		{
			if (AssetClass->IsChildOf(LoadedSupportedClass))
			{
				return true;
			}
		}
	}

	return false;
}
