// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetsPreviewActor.h"

class UObject;

/**
 * LookDev 编辑器单个资产（或资产集合）的缓存条目
 *
 * 记录一个 LookDev 编辑器窗口的可恢复状态：预览环境 Actor 的全部 EditAnywhere 属性、
 * 视口 FOV 与相机姿态、多视图副本的角度槽位掩码与生成标记。
 * 由 FLookDevEditorCache 序列化到 Saved/LookDevEnhance/LookDevEditorCache.ini。
 */
struct FLookDevEditorCacheEntry
{
	// 预览资产的朝向偏移（环境 Actor 的 PreviewRotationOffset）
	FRotator PreviewRotationOffset = FRotator::ZeroRotator;

	// 灯光氛围档位
	ELightingScenarial LightingScenarios = ELightingScenarial::MidContrast;

	// 曝光补偿
	float ExposureCompensation = 1.0f;

	// 背景亮度补偿指数
	float BackgroundValueCompensation = -1.0f;

	// 是否开启光线追踪阴影
	bool bRaytracingShadow = false;

	// 是否为三盏方向光启用透射
	bool bEnableTransmission = true;

	// 是否旋转灯光
	bool bTurnLight = false;

	// 灯光旋转角度（bTurnLight 为 false 时是固定偏移）
	float TurnLighting = 0.0f;

	// 背景是否贴地
	bool bCastGroundShadow = false;

	// 是否显示校准参照物
	bool bShowCalibrator = true;

	// 是否启用 PBR Checker
	bool bEnableChecker = false;

	// 视口 FOV
	float ViewFOV = 90.0f;

	// 是否存在有效的相机姿态（存在时打开编辑器直接恢复相机，跳过默认聚焦）
	bool bHasCameraPose = false;

	// 相机位置（bHasCameraPose 为 true 时有效）
	FVector CameraLocation = FVector::ZeroVector;

	// 相机旋转（bHasCameraPose 为 true 时有效）
	FRotator CameraRotation = FRotator(-15.0, -155.0, 0.0);

	// 多视图副本角度槽位启用位掩码（bit i 对应槽位数组下标）
	uint8 MultiViewSlotMask = 0xBD;

	// 多视图副本间距（相邻副本包围盒之间的净距离，单位 cm）
	float MultiViewCopySpacing = 50.0f;

	// 上次关闭时是否存在多视图副本（恢复时按掩码自动重建）
	bool bHasMultiViewCopies = false;

	/**
	 *	逐字段精确比较两个条目是否一致（防抖轮询据此判断是否需要写盘）
	 *
	 *	@param	Other	要比较的另一个条目
	 *	@return	全部字段相等时返回 true
	 */
	bool Equals(const FLookDevEditorCacheEntry& Other) const
	{
		return PreviewRotationOffset == Other.PreviewRotationOffset
			&& LightingScenarios == Other.LightingScenarios
			&& ExposureCompensation == Other.ExposureCompensation
			&& BackgroundValueCompensation == Other.BackgroundValueCompensation
			&& bRaytracingShadow == Other.bRaytracingShadow
			&& bEnableTransmission == Other.bEnableTransmission
			&& bTurnLight == Other.bTurnLight
			&& TurnLighting == Other.TurnLighting
			&& bCastGroundShadow == Other.bCastGroundShadow
			&& bShowCalibrator == Other.bShowCalibrator
			&& bEnableChecker == Other.bEnableChecker
			&& ViewFOV == Other.ViewFOV
			&& bHasCameraPose == Other.bHasCameraPose
			&& CameraLocation == Other.CameraLocation
			&& CameraRotation == Other.CameraRotation
			&& MultiViewSlotMask == Other.MultiViewSlotMask
			&& MultiViewCopySpacing == Other.MultiViewCopySpacing
			&& bHasMultiViewCopies == Other.bHasMultiViewCopies;
	}
};

/**
 * 摄像机书签条目
 *
 * 记录一个书签的摄像机姿态（位置 / 旋转 / FOV）与缩略图文件名。
 * 缩略图本体是独立 PNG 文件（Saved/LookDevEnhance/CameraBookmarkThumbnails/<GUID>.png），
 * 书签列表由 FLookDevEditorCache 序列化到 Saved/LookDevEnhance/LookDevCameraBookmarks.ini。
 */
struct FLookDevCameraBookmark
{
	// 书签名（同一资产 Key 下唯一）
	FString Name;

	// 摄像机位置
	FVector Location = FVector::ZeroVector;

	// 摄像机旋转
	FRotator Rotation = FRotator::ZeroRotator;

	// 视口 FOV
	float FOV = 90.0f;

	// 缩略图 PNG 文件名（GUID，不含目录）；重命名书签不影响缩略图文件
	FString ThumbnailFileName;
};

/**
 * LookDev 编辑器状态缓存的读写入口
 *
 * 以资产集合（排序后的包路径拼接）为 Key，把 FLookDevEditorCacheEntry 序列化到
 * 项目 Saved/LookDevEnhance/LookDevEditorCache.ini，每个 Key 一个 ini 分节。
 * 无状态静态工具类，内部直接操作 FConfigFile，不注册进 GConfig。
 */
class FLookDevEditorCache
{
public:
	/**
	 *	由资产列表生成缓存 Key
	 *
	 *	取每个资产包的长路径（/Game/...），排序后用 "|" 拼接；
	 *	与编辑器"按资产集合查重窗口"的逻辑一致：同一资产单独打开与组合打开各有独立缓存
	 *
	 *	@param	Assets	编辑器正在预览的资产列表
	 *	@return	缓存 Key，资产列表为空时返回空字符串
	 */
	static FString MakeCacheKey(const TArray<UObject*>& Assets);

	/**
	 *	读取指定 Key 的缓存条目
	 *
	 *	@param	Key				缓存 Key（MakeCacheKey 生成）
	 *	@param	OutEntry		输出条目；分节存在但个别字段缺失时保留默认值
	 *	@return	缓存文件存在且包含该 Key 的分节时返回 true
	 */
	static bool LoadEntry(const FString& Key, FLookDevEditorCacheEntry& OutEntry);

	/**
	 *	写入指定 Key 的缓存条目（保留文件中其他 Key 的分节，整体重写文件）
	 *
	 *	@param	Key		缓存 Key（MakeCacheKey 生成）
	 *	@param	Entry	要写入的条目
	 */
	static void SaveEntry(const FString& Key, const FLookDevEditorCacheEntry& Entry);

	/**
	 *	读取指定 Key 的摄像机书签列表
	 *
	 *	@param	Key		缓存 Key（MakeCacheKey 生成）
	 *	@return	书签列表，Key 为空或无分节时返回空数组
	 */
	static TArray<FLookDevCameraBookmark> LoadCameraBookmarks(const FString& Key);

	/**
	 *	写入指定 Key 的摄像机书签列表（整节重写，不影响缓存文件——书签是独立 ini）
	 *
	 *	@param	Key			缓存 Key（MakeCacheKey 生成）
	 *	@param	Bookmarks	要写入的书签列表
	 */
	static void SaveCameraBookmarks(const FString& Key, const TArray<FLookDevCameraBookmark>& Bookmarks);

	/**
	 *	书签缩略图目录（项目 Saved/LookDevEnhance/CameraBookmarkThumbnails）
	 */
	static FString GetCameraBookmarkThumbnailDir();

	/**
	 *	指定书签的缩略图完整路径
	 *
	 *	@param	Bookmark	书签条目
	 *	@return	缩略图完整路径，未记录文件名时返回空字符串
	 */
	static FString GetCameraBookmarkThumbnailPath(const FLookDevCameraBookmark& Bookmark);

private:
	/**
	 *	缓存 ini 的完整路径（项目 Saved/LookDevEnhance/LookDevEditorCache.ini）
	 */
	static FString GetCacheFilePath();

	/**
	 *	摄像机书签 ini 的完整路径（项目 Saved/LookDevEnhance/LookDevCameraBookmarks.ini）
	 */
	static FString GetCameraBookmarksFilePath();
};
