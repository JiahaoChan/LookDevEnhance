// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLookDevAssetToolkit;
class SVerticalBox;
class UTexture2D;

struct FLookDevCameraBookmark;

/**
 * LookDev 编辑器抽屉里的摄像机书签面板
 *
 * 顶部是"新建书签"按钮（弹输入框命名，重名自动追加序号），
 * 下方是书签列表：每行一个 128x128 缩略图 + 名字 + 重命名 / 删除按钮。
 * 点击缩略图跳到书签的摄像机姿态（位置 / 旋转 / FOV），并立刻按新画面刷新缩略图。
 * 书签逐资产序列化（FLookDevEditorCache），面板用版本号轮询工具集决定是否重建列表。
 */
class SLookDevCameraBookmarkPanel : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SLookDevCameraBookmarkPanel) {}
		SLATE_ARGUMENT(TWeakPtr<FLookDevAssetToolkit>, LookDevToolkit)
	SLATE_END_ARGS()

private:
	// 所属的 LookDev 工具集，书签数据的读写都走它
	TWeakPtr<FLookDevAssetToolkit> LookDevToolkitWeak;

	// 书签列表容器（重建时清空再填充）
	TSharedPtr<SVerticalBox> ListContainer;

	// 上次重建列表时的书签版本号，Tick 与工具集版本不一致时重建
	int32 CachedBookmarksVersion = -1;

	// 缩略图纹理的强引用（Slate 画刷只持有弱引用，列表展示期间由面板保活）
	TArray<TStrongObjectPtr<UTexture2D>> ThumbnailTextures;

	// 每行书签的缩略图画刷（SImage 只持有裸指针，列表展示期间由面板保活）
	TArray<TSharedPtr<FSlateBrush>> ThumbnailBrushes;

public:
	/**
	 *	构建控件
	 *
	 *	@param	InArgs	Slate 参数，包含所属工具集
	 */
	void Construct(const FArguments& InArgs);

	virtual ~SLookDevCameraBookmarkPanel() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/**
	 *	按工具集当前的书签列表重建全部行（并重载缩略图）
	 */
	void RefreshBookmarkList();

	/**
	 *	构建单个书签行：缩略图按钮 + 名字 + 重命名 / 删除按钮
	 *
	 *	@param	Index			书签下标
	 *	@param	Bookmark		书签数据
	 *	@param	ThumbnailBrush	缩略图画刷（无缩略图时传占位画刷）
	 */
	TSharedRef<SWidget> MakeBookmarkRow(int32 Index, const FLookDevCameraBookmark& Bookmark, const FSlateBrush* ThumbnailBrush);

	/**
	 *	新建书签：弹命名输入框后调用工具集 AddCameraBookmark
	 */
	FReply OnNewBookmarkClicked();

	/**
	 *	点击缩略图：跳到书签机位并刷新缩略图
	 */
	FReply OnBookmarkThumbnailClicked(int32 Index);

	/**
	 *	重命名：弹输入框（预填当前名字）后调用工具集 RenameCameraBookmark
	 */
	FReply OnRenameBookmarkClicked(int32 Index);

	/**
	 *	删除书签（含缩略图文件）
	 */
	FReply OnDeleteBookmarkClicked(int32 Index);

	/**
	 *	弹出模态命名输入框
	 *
	 *	@param	Title		对话框标题
	 *	@param	InitialName	输入框预填文本
	 *	@param	OutName		确认时输出输入的名字
	 *	@return	用户点击 OK（或回车）时返回 true，取消返回 false
	 */
	static bool ShowBookmarkNameDialog(const FText& Title, const FString& InitialName, FString& OutName);
};
