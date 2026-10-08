// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/SLookDevCameraBookmarkPanel.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "LookDevEditor/LookDevAssetToolkit.h"
#include "LookDevEditor/LookDevEditorCache.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

void SLookDevCameraBookmarkPanel::Construct(const FArguments& InArgs)
{
	LookDevToolkitWeak = InArgs._LookDevToolkit;

	ChildSlot
	[
		SNew(SVerticalBox)

		// 顶部：新建书签按钮
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("NewBookmarkButtonLabel", "New Bookmark"))
			.ToolTipText(LOCTEXT("NewBookmarkButtonTooltip", "Save the current camera position, rotation and FOV as a new bookmark"))
			.OnClicked(this, &SLookDevCameraBookmarkPanel::OnNewBookmarkClicked)
		]

		// 书签列表（可滚动）
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ListContainer, SVerticalBox)
			]
		]
	];

	RefreshBookmarkList();
}

SLookDevCameraBookmarkPanel::~SLookDevCameraBookmarkPanel()
{
	// 缩略图纹理是手动 AddToRoot 的（RefreshBookmarkList 导入处），归还 RootSet 防泄漏
	for (const TStrongObjectPtr<UTexture2D>& Texture : ThumbnailTextures)
	{
		if (Texture.IsValid())
		{
			Texture->RemoveFromRoot();
		}
	}
}

void SLookDevCameraBookmarkPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	const int32 CurrentVersion = Toolkit.IsValid() ? Toolkit->GetCameraBookmarksVersion() : -1;
	if (CurrentVersion != CachedBookmarksVersion)
	{
		RefreshBookmarkList();
	}
}

void SLookDevCameraBookmarkPanel::RefreshBookmarkList()
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid() || !ListContainer.IsValid())
	{
		return;
	}

	// 先清空旧行再释放旧画刷：SImage 只持有画刷裸指针，反序会让行短暂引用已释放画刷
	ListContainer->ClearChildren();
	// 纹理是手动 AddToRoot 的（见下方导入处），重建列表前逐个 RemoveFromRoot 归还给 GC
	for (const TStrongObjectPtr<UTexture2D>& Texture : ThumbnailTextures)
	{
		if (Texture.IsValid())
		{
			Texture->RemoveFromRoot();
		}
	}
	ThumbnailTextures.Reset();
	ThumbnailBrushes.Reset();
	CachedBookmarksVersion = Toolkit->GetCameraBookmarksVersion();

	const TArray<FLookDevCameraBookmark>& Bookmarks = Toolkit->GetCameraBookmarks();
	if (Bookmarks.IsEmpty())
	{
		ListContainer->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 12.0f)
		[
			SNew(STextBlock)
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoBookmarksHint", "No bookmarks yet. Click New Bookmark to save the current camera pose."))
		];
		return;
	}

	const FSlateBrush* PlaceholderBrush = FAppStyle::GetBrush("ClassIcon.CameraActor");
	for (int32 Index = 0; Index < Bookmarks.Num(); ++Index)
	{
		const FLookDevCameraBookmark& Bookmark = Bookmarks[Index];

		// 缩略图从 PNG 文件导入为纹理；缺失时用占位图
		const FSlateBrush* ThumbnailBrush = PlaceholderBrush;
		const FString ThumbnailPath = FLookDevEditorCache::GetCameraBookmarkThumbnailPath(Bookmark);
		if (!ThumbnailPath.IsEmpty() && FPaths::FileExists(ThumbnailPath))
		{
			if (UTexture2D* ThumbnailTexture = FImageUtils::ImportFileAsTexture2D(ThumbnailPath))
			{
				// 纹理先自行 AddToRoot：FSlateDynamicImageBrush 对不在 RootSet 的纹理会触发
				// ensure 并由画刷接管 AddToRoot（SlateDynamicImageBrush.h:120，该 hack 在加载屏场景会崩）；
				// 已入 RootSet 时画刷跳过接管，生命周期归面板管理（重建列表/析构时 RemoveFromRoot）
				ThumbnailTexture->AddToRoot();
				ThumbnailTextures.Add(TStrongObjectPtr<UTexture2D>(ThumbnailTexture));
				ThumbnailBrushes.Add(MakeShareable(new FSlateDynamicImageBrush(
					ThumbnailTexture,
					FVector2D(FLookDevAssetToolkit::CameraBookmarkThumbnailSize, FLookDevAssetToolkit::CameraBookmarkThumbnailSize),
					NAME_None)));
				ThumbnailBrush = ThumbnailBrushes.Last().Get();
			}
		}

		ListContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeBookmarkRow(Index, Bookmark, ThumbnailBrush)
		];
	}
}

TSharedRef<SWidget> SLookDevCameraBookmarkPanel::MakeBookmarkRow(int32 Index, const FLookDevCameraBookmark& Bookmark, const FSlateBrush* ThumbnailBrush)
{
	constexpr float ThumbnailSize = FLookDevAssetToolkit::CameraBookmarkThumbnailSize;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			// 缩略图：点击跳到书签机位并刷新缩略图
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(ThumbnailSize)
				.HeightOverride(ThumbnailSize)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
					.ContentPadding(0.0f)
					.ToolTipText(LOCTEXT("BookmarkThumbnailTooltip", "Jump to this bookmark; the thumbnail is refreshed from the new view"))
					.OnClicked(this, &SLookDevCameraBookmarkPanel::OnBookmarkThumbnailClicked, Index)
					[
						SNew(SImage)
						.Image(ThumbnailBrush)
					]
				]
			]

			// 书签名
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Bookmark.Name))
				.ToolTipText(FText::FromString(Bookmark.Name))
			]

			// 重命名按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("RenameBookmarkTooltip", "Rename this bookmark"))
				.OnClicked(this, &SLookDevCameraBookmarkPanel::OnRenameBookmarkClicked, Index)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Edit"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			// 删除按钮
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("DeleteBookmarkTooltip", "Delete this bookmark"))
				.OnClicked(this, &SLookDevCameraBookmarkPanel::OnDeleteBookmarkClicked, Index)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

FReply SLookDevCameraBookmarkPanel::OnNewBookmarkClicked()
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		FString BookmarkName;
		if (ShowBookmarkNameDialog(LOCTEXT("NewBookmarkDialogTitle", "New Camera Bookmark"), FString(), BookmarkName))
		{
			Toolkit->AddCameraBookmark(BookmarkName);
		}
	}
	return FReply::Handled();
}

FReply SLookDevCameraBookmarkPanel::OnBookmarkThumbnailClicked(int32 Index)
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		Toolkit->ApplyCameraBookmark(Index);
	}
	return FReply::Handled();
}

FReply SLookDevCameraBookmarkPanel::OnRenameBookmarkClicked(int32 Index)
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		const TArray<FLookDevCameraBookmark>& Bookmarks = Toolkit->GetCameraBookmarks();
		if (!Bookmarks.IsValidIndex(Index))
		{
			return FReply::Handled();
		}

		FString NewName;
		if (ShowBookmarkNameDialog(LOCTEXT("RenameBookmarkDialogTitle", "Rename Bookmark"), Bookmarks[Index].Name, NewName))
		{
			Toolkit->RenameCameraBookmark(Index, NewName);
		}
	}
	return FReply::Handled();
}

FReply SLookDevCameraBookmarkPanel::OnDeleteBookmarkClicked(int32 Index)
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		Toolkit->RemoveCameraBookmark(Index);
	}
	return FReply::Handled();
}

bool SLookDevCameraBookmarkPanel::ShowBookmarkNameDialog(const FText& Title, const FString& InitialName, FString& OutName)
{
	bool bConfirmed = false;

	// 先建窗口再建文本框：回车提交回调需要窗口弱指针，而 OnTextCommitted 只能在构造时传入
	//（SEditableTextBox::OnTextCommitted 是 protected 成员，没有事后 setter）
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(Title)
		.SizingRule(ESizingRule::Autosized)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMaximize(false)
		.SupportsMinimize(false);
	const TWeakPtr<SWindow> WeakWindow = DialogWindow;

	// 文本框独立创建：OK 按钮的 IsEnabled lambda 按值捕获它的弱指针，
	// 若捕获局部 TSharedPtr 的引用会在函数返回后悬空（lambda 生命周期超过本函数）
	TSharedRef<SEditableTextBox> NameTextBox = SNew(SEditableTextBox)
		.Text(FText::FromString(InitialName))
		.SelectAllTextWhenFocused(true)
		.MinDesiredWidth(280.0f)
		// 回车等同于点击 OK（直接用提交文本校验，无需读回控件）
		.OnTextCommitted(FOnTextCommitted::CreateLambda(
			[&bConfirmed, WeakWindow](const FText& InText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter && !InText.ToString().TrimStartAndEnd().IsEmpty())
				{
					bConfirmed = true;
					if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
					{
						PinnedWindow->RequestDestroyWindow();
					}
				}
			}));
	const TWeakPtr<SEditableTextBox> WeakTextBox = NameTextBox;

	DialogWindow->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				NameTextBox
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
					.Text(LOCTEXT("BookmarkDialogOK", "OK"))
					// 输入非空才可确认
					.IsEnabled_Lambda([WeakTextBox]()
					{
						const TSharedPtr<SEditableTextBox> PinnedTextBox = WeakTextBox.Pin();
						return PinnedTextBox.IsValid() && !PinnedTextBox->GetText().ToString().TrimStartAndEnd().IsEmpty();
					})
					.OnClicked_Lambda([&bConfirmed, WeakWindow]()
					{
						bConfirmed = true;
						if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
						{
							PinnedWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("BookmarkDialogCancel", "Cancel"))
					.OnClicked_Lambda([WeakWindow]()
					{
						if (const TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
						{
							PinnedWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		]);

	DialogWindow->SetWidgetToFocusOnActivate(NameTextBox);
	GEditor->EditorAddModalWindow(DialogWindow);

	if (bConfirmed)
	{
		OutName = NameTextBox->GetText().ToString().TrimStartAndEnd();
	}
	return bConfirmed;
}

#undef LOCTEXT_NAMESPACE
