// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/LookDevEditorCache.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

FString FLookDevEditorCache::MakeCacheKey(const TArray<UObject*>& Assets)
{
	TArray<FString> PackageNames;
	for (const UObject* Asset : Assets)
	{
		if (const UPackage* Package = Asset ? Asset->GetPackage() : nullptr)
		{
			PackageNames.AddUnique(Package->GetName());
		}
	}
	PackageNames.Sort();
	return FString::Join(PackageNames, TEXT("|"));
}

bool FLookDevEditorCache::LoadEntry(const FString& Key, FLookDevEditorCacheEntry& OutEntry)
{
	if (Key.IsEmpty())
	{
		return false;
	}

	FConfigFile ConfigFile;
	ConfigFile.Read(GetCacheFilePath());
	if (ConfigFile.FindSection(Key) == nullptr)
	{
		return false;
	}

	// 逐字段读取：缺失的字段保留条目默认值（老版本缓存文件向后兼容）
	FString StringValue;
	if (ConfigFile.GetString(*Key, TEXT("PreviewRotationOffset"), StringValue))
	{
		OutEntry.PreviewRotationOffset.InitFromString(StringValue);
	}
	if (ConfigFile.GetString(*Key, TEXT("LightingScenarios"), StringValue))
	{
		const int64 EnumValue = StaticEnum<ELightingScenarial>()->GetValueByNameString(StringValue);
		if (EnumValue != INDEX_NONE)
		{
			OutEntry.LightingScenarios = static_cast<ELightingScenarial>(EnumValue);
		}
	}
	ConfigFile.GetFloat(*Key, TEXT("ExposureCompensation"), OutEntry.ExposureCompensation);
	ConfigFile.GetFloat(*Key, TEXT("BackgroundValueCompensation"), OutEntry.BackgroundValueCompensation);
	ConfigFile.GetBool(*Key, TEXT("bRaytracingShadow"), OutEntry.bRaytracingShadow);
	ConfigFile.GetBool(*Key, TEXT("bEnableTransmission"), OutEntry.bEnableTransmission);
	ConfigFile.GetBool(*Key, TEXT("bTurnLight"), OutEntry.bTurnLight);
	ConfigFile.GetFloat(*Key, TEXT("TurnLighting"), OutEntry.TurnLighting);
	ConfigFile.GetBool(*Key, TEXT("bCastGroundShadow"), OutEntry.bCastGroundShadow);
	ConfigFile.GetBool(*Key, TEXT("bShowCalibrator"), OutEntry.bShowCalibrator);
	ConfigFile.GetBool(*Key, TEXT("bEnableChecker"), OutEntry.bEnableChecker);
	ConfigFile.GetFloat(*Key, TEXT("ViewFOV"), OutEntry.ViewFOV);
	ConfigFile.GetBool(*Key, TEXT("bHasCameraPose"), OutEntry.bHasCameraPose);
	if (ConfigFile.GetString(*Key, TEXT("CameraLocation"), StringValue))
	{
		OutEntry.CameraLocation.InitFromString(StringValue);
	}
	if (ConfigFile.GetString(*Key, TEXT("CameraRotation"), StringValue))
	{
		OutEntry.CameraRotation.InitFromString(StringValue);
	}
	int32 SlotMask = OutEntry.MultiViewSlotMask;
	if (ConfigFile.GetInt(*Key, TEXT("MultiViewSlotMask"), SlotMask))
	{
		OutEntry.MultiViewSlotMask = static_cast<uint8>(SlotMask);
	}
	ConfigFile.GetFloat(*Key, TEXT("MultiViewCopySpacing"), OutEntry.MultiViewCopySpacing);
	ConfigFile.GetBool(*Key, TEXT("bHasMultiViewCopies"), OutEntry.bHasMultiViewCopies);

	return true;
}

void FLookDevEditorCache::SaveEntry(const FString& Key, const FLookDevEditorCacheEntry& Entry)
{
	if (Key.IsEmpty())
	{
		return;
	}

	// 读回整个文件再写，保留其他资产 Key 的分节
	FConfigFile ConfigFile;
	ConfigFile.Read(GetCacheFilePath());

	ConfigFile.SetString(*Key, TEXT("PreviewRotationOffset"), *Entry.PreviewRotationOffset.ToString());
	ConfigFile.SetString(*Key, TEXT("LightingScenarios"), *StaticEnum<ELightingScenarial>()->GetNameStringByValue(static_cast<int64>(Entry.LightingScenarios)));
	ConfigFile.SetFloat(*Key, TEXT("ExposureCompensation"), Entry.ExposureCompensation);
	ConfigFile.SetFloat(*Key, TEXT("BackgroundValueCompensation"), Entry.BackgroundValueCompensation);
	ConfigFile.SetBool(*Key, TEXT("bRaytracingShadow"), Entry.bRaytracingShadow);
	ConfigFile.SetBool(*Key, TEXT("bEnableTransmission"), Entry.bEnableTransmission);
	ConfigFile.SetBool(*Key, TEXT("bTurnLight"), Entry.bTurnLight);
	ConfigFile.SetFloat(*Key, TEXT("TurnLighting"), Entry.TurnLighting);
	ConfigFile.SetBool(*Key, TEXT("bCastGroundShadow"), Entry.bCastGroundShadow);
	ConfigFile.SetBool(*Key, TEXT("bShowCalibrator"), Entry.bShowCalibrator);
	ConfigFile.SetBool(*Key, TEXT("bEnableChecker"), Entry.bEnableChecker);
	ConfigFile.SetFloat(*Key, TEXT("ViewFOV"), Entry.ViewFOV);
	ConfigFile.SetBool(*Key, TEXT("bHasCameraPose"), Entry.bHasCameraPose);
	ConfigFile.SetString(*Key, TEXT("CameraLocation"), *Entry.CameraLocation.ToString());
	ConfigFile.SetString(*Key, TEXT("CameraRotation"), *Entry.CameraRotation.ToString());
	ConfigFile.SetString(*Key, TEXT("MultiViewSlotMask"), *FString::FromInt(static_cast<int32>(Entry.MultiViewSlotMask)));
	ConfigFile.SetFloat(*Key, TEXT("MultiViewCopySpacing"), Entry.MultiViewCopySpacing);
	ConfigFile.SetBool(*Key, TEXT("bHasMultiViewCopies"), Entry.bHasMultiViewCopies);

	const FString FilePath = GetCacheFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree=*/ true);
	ConfigFile.Write(FilePath);
}

TArray<FLookDevCameraBookmark> FLookDevEditorCache::LoadCameraBookmarks(const FString& Key)
{
	TArray<FLookDevCameraBookmark> Bookmarks;
	if (Key.IsEmpty())
	{
		return Bookmarks;
	}

	FConfigFile ConfigFile;
	ConfigFile.Read(GetCameraBookmarksFilePath());
	if (ConfigFile.FindSection(Key) == nullptr)
	{
		return Bookmarks;
	}

	int32 BookmarkCount = 0;
	ConfigFile.GetInt(*Key, TEXT("BookmarkCount"), BookmarkCount);
	for (int32 Index = 0; Index < BookmarkCount; ++Index)
	{
		FLookDevCameraBookmark Bookmark;

		FString StringValue;
		if (!ConfigFile.GetString(*Key, *FString::Printf(TEXT("Bookmark%d.Name"), Index), Bookmark.Name))
		{
			// 名字是书签的必要字段，缺失视为损坏条目，跳过
			continue;
		}
		if (ConfigFile.GetString(*Key, *FString::Printf(TEXT("Bookmark%d.Location"), Index), StringValue))
		{
			Bookmark.Location.InitFromString(StringValue);
		}
		if (ConfigFile.GetString(*Key, *FString::Printf(TEXT("Bookmark%d.Rotation"), Index), StringValue))
		{
			Bookmark.Rotation.InitFromString(StringValue);
		}
		ConfigFile.GetFloat(*Key, *FString::Printf(TEXT("Bookmark%d.FOV"), Index), Bookmark.FOV);
		ConfigFile.GetString(*Key, *FString::Printf(TEXT("Bookmark%d.Thumbnail"), Index), Bookmark.ThumbnailFileName);

		Bookmarks.Add(MoveTemp(Bookmark));
	}
	return Bookmarks;
}

void FLookDevEditorCache::SaveCameraBookmarks(const FString& Key, const TArray<FLookDevCameraBookmark>& Bookmarks)
{
	if (Key.IsEmpty())
	{
		return;
	}

	// 读回整个文件再写，保留其他资产 Key 的分节；整节重写前先移除旧分节，清掉多于当前数量的残留条目
	FConfigFile ConfigFile;
	ConfigFile.Read(GetCameraBookmarksFilePath());
	ConfigFile.Remove(Key);

	ConfigFile.SetInt64(*Key, TEXT("BookmarkCount"), Bookmarks.Num());
	for (int32 Index = 0; Index < Bookmarks.Num(); ++Index)
	{
		const FLookDevCameraBookmark& Bookmark = Bookmarks[Index];
		ConfigFile.SetString(*Key, *FString::Printf(TEXT("Bookmark%d.Name"), Index), *Bookmark.Name);
		ConfigFile.SetString(*Key, *FString::Printf(TEXT("Bookmark%d.Location"), Index), *Bookmark.Location.ToString());
		ConfigFile.SetString(*Key, *FString::Printf(TEXT("Bookmark%d.Rotation"), Index), *Bookmark.Rotation.ToString());
		ConfigFile.SetFloat(*Key, *FString::Printf(TEXT("Bookmark%d.FOV"), Index), Bookmark.FOV);
		ConfigFile.SetString(*Key, *FString::Printf(TEXT("Bookmark%d.Thumbnail"), Index), *Bookmark.ThumbnailFileName);
	}

	const FString FilePath = GetCameraBookmarksFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree=*/ true);
	ConfigFile.Write(FilePath);
}

FString FLookDevEditorCache::GetCameraBookmarkThumbnailDir()
{
	return FPaths::ProjectSavedDir() / TEXT("LookDevEnhance") / TEXT("CameraBookmarkThumbnails");
}

FString FLookDevEditorCache::GetCameraBookmarkThumbnailPath(const FLookDevCameraBookmark& Bookmark)
{
	return Bookmark.ThumbnailFileName.IsEmpty()
		? FString()
		: GetCameraBookmarkThumbnailDir() / Bookmark.ThumbnailFileName;
}

FString FLookDevEditorCache::GetCacheFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("LookDevEnhance") / TEXT("LookDevEditorCache.ini");
}

FString FLookDevEditorCache::GetCameraBookmarksFilePath()
{
	return FPaths::ProjectSavedDir() / TEXT("LookDevEnhance") / TEXT("LookDevCameraBookmarks.ini");
}
