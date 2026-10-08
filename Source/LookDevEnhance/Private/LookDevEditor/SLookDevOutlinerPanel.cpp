// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/SLookDevOutlinerPanel.h"

#include "GameFramework/Actor.h"
#include "LookDevEditor/LookDevAssetToolkit.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "LookDevEnhance"

void SLookDevOutlinerPanel::Construct(const FArguments& InArgs)
{
	LookDevToolkitWeak = InArgs._LookDevToolkit;

	ChildSlot
	[
		SNew(SOverlay)

		// 空列表提示（无任何预览 Actor 时居中显示）
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility(this, &SLookDevOutlinerPanel::GetEmptyTextVisibility)
			.Text(LOCTEXT("OutlinerEmptyText", "No preview actors"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		+ SOverlay::Slot()
		[
			SAssignNew(ListView, SListView<TWeakObjectPtr<AActor>>)
			.ListItemsSource(&Items)
			.SelectionMode(ESelectionMode::Multi)
			.OnGenerateRow(this, &SLookDevOutlinerPanel::OnGenerateRow)
			.OnSelectionChanged(this, &SLookDevOutlinerPanel::OnListSelectionChanged)
			.OnMouseButtonDoubleClick(this, &SLookDevOutlinerPanel::OnItemDoubleClicked)
		]
	];
}

void SLookDevOutlinerPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid())
	{
		return;
	}

	// 版本号轮询（与书签面板同一套模式）：列表结构变化优先重建，重建内含选中同步
	if (Toolkit->GetPreviewActorsVersion() != CachedActorsVersion)
	{
		CachedActorsVersion = Toolkit->GetPreviewActorsVersion();
		CachedSelectionVersion = Toolkit->GetPreviewSelectionVersion();
		RebuildItems();
	}
	else if (Toolkit->GetPreviewSelectionVersion() != CachedSelectionVersion)
	{
		CachedSelectionVersion = Toolkit->GetPreviewSelectionVersion();
		SyncSelectionFromToolkit();
	}
}

void SLookDevOutlinerPanel::RebuildItems()
{
	Items.Reset();
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		for (const TWeakObjectPtr<AActor>& Actor : Toolkit->GetPreviewActors())
		{
			if (Actor.IsValid())
			{
				Items.Add(Actor);
			}
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	SyncSelectionFromToolkit();
}

void SLookDevOutlinerPanel::SyncSelectionFromToolkit()
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid() || !ListView.IsValid())
	{
		return;
	}

	const TArray<AActor*> SelectedActors = Toolkit->GetSelectedPreviewActors();

	// 与列表当前选中逐项比较，集合一致则跳过，避免 SetItemSelection 触发 OnSelectionChanged 回写选择集
	const TArray<TWeakObjectPtr<AActor>> CurrentSelection = ListView->GetSelectedItems();
	bool bSelectionMatches = CurrentSelection.Num() == SelectedActors.Num();
	if (bSelectionMatches)
	{
		for (const TWeakObjectPtr<AActor>& Item : CurrentSelection)
		{
			if (!SelectedActors.Contains(Item.Get()))
			{
				bSelectionMatches = false;
				break;
			}
		}
	}
	if (bSelectionMatches)
	{
		return;
	}

	TGuardValue<bool> SelectionGuard(bApplyingSelection, true);
	ListView->ClearSelection();
	for (AActor* SelectedActor : SelectedActors)
	{
		for (const TWeakObjectPtr<AActor>& Item : Items)
		{
			if (Item.Get() == SelectedActor)
			{
				ListView->SetItemSelection(Item, true);
				break;
			}
		}
	}
}

TSharedRef<ITableRow> SLookDevOutlinerPanel::OnGenerateRow(TWeakObjectPtr<AActor> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TWeakObjectPtr<AActor>>, OwnerTable)
		.ToolTipText(this, &SLookDevOutlinerPanel::GetRowTooltip, Item)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassIcon.Actor"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SLookDevOutlinerPanel::GetRowLabel, Item)
			]
		];
}

void SLookDevOutlinerPanel::OnListSelectionChanged(TWeakObjectPtr<AActor> Item, ESelectInfo::Type SelectInfo)
{
	if (bApplyingSelection)
	{
		return;
	}

	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid() || !ListView.IsValid())
	{
		return;
	}

	// 列表当前选中（含 Ctrl / Shift 多选）写回视口选择集；空数组 = 清空选择
	TArray<AActor*> Actors;
	for (const TWeakObjectPtr<AActor>& SelectedItem : ListView->GetSelectedItems())
	{
		if (AActor* Actor = SelectedItem.Get())
		{
			Actors.Add(Actor);
		}
	}
	Toolkit->SelectPreviewActors(Actors);
}

void SLookDevOutlinerPanel::OnItemDoubleClicked(TWeakObjectPtr<AActor> Item)
{
	if (const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin())
	{
		Toolkit->FocusOnSelection();
	}
}

FText SLookDevOutlinerPanel::GetRowLabel(TWeakObjectPtr<AActor> Item) const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	return Toolkit.IsValid() ? Toolkit->GetPreviewActorDisplayLabel(Item.Get()) : FText::GetEmpty();
}

FText SLookDevOutlinerPanel::GetRowTooltip(TWeakObjectPtr<AActor> Item) const
{
	const TSharedPtr<FLookDevAssetToolkit> Toolkit = LookDevToolkitWeak.Pin();
	if (!Toolkit.IsValid())
	{
		return FText::GetEmpty();
	}
	const UObject* SourceAsset = Toolkit->GetPreviewActorSourceAsset(Item.Get());
	return SourceAsset != nullptr ? FText::FromString(SourceAsset->GetPathName()) : FText::GetEmpty();
}

EVisibility SLookDevOutlinerPanel::GetEmptyTextVisibility() const
{
	return Items.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
