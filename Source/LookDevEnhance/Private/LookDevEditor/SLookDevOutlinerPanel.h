// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class FLookDevAssetToolkit;
class ITableRow;
class STableViewBase;

template <typename ItemType>
class SListView;

/**
 * 预览 Actor 大纲面板（轻量 WorldOutliner）
 *
 * 列出全部预览 Actor（原预览与多视图副本），解决特效等难以在视口中点选的 Actor 的选中问题。
 * 列表为纯视图状态：Tick 轮询工具集的 PreviewActorsVersion / PreviewSelectionVersion 两个计数器
 * 决定重建列表与同步选中行（与书签面板同一套轮询模式），不注册任何委托；
 * 单击选中（支持 Ctrl / Shift 多选）经 FLookDevAssetToolkit::SelectPreviewActors 写回视口选择集，
 * 双击在选中后聚焦该 Actor。
 */
class SLookDevOutlinerPanel : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SLookDevOutlinerPanel) {}
		SLATE_ARGUMENT(TWeakPtr<FLookDevAssetToolkit>, LookDevToolkit)
	SLATE_END_ARGS()

public:
	/**
	 *	构建控件
	 *
	 *	@param	InArgs	Slate 参数，包含所属工具集
	 */
	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// 列表重建：从工具集拉取当前有效的预览 Actor（版本号变化时调用）
	void RebuildItems();

	// 视口 -> 列表方向同步：把视口选中的预览 Actor 高亮为列表选中行
	void SyncSelectionFromToolkit();

	// 列表行生成：Actor 图标 + 显示名（副本追加相对朝向角）
	TSharedRef<ITableRow> OnGenerateRow(TWeakObjectPtr<AActor> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// 列表选中变化：写回视口选择集（bApplyingSelection 内为视口 -> 列表同步，不反写防递归）
	void OnListSelectionChanged(TWeakObjectPtr<AActor> Item, ESelectInfo::Type SelectInfo);

	// 行双击：聚焦该 Actor（首次点击已完成选中，与 F 键同一套聚焦逻辑）
	void OnItemDoubleClicked(TWeakObjectPtr<AActor> Item);

	// 行显示名 / Tooltip（源资产路径）
	FText GetRowLabel(TWeakObjectPtr<AActor> Item) const;
	FText GetRowTooltip(TWeakObjectPtr<AActor> Item) const;

	// 空列表提示文本可见性
	EVisibility GetEmptyTextVisibility() const;

private:
	// 所属的 LookDev 工具集，列表数据与选择同步的出入口
	TWeakPtr<FLookDevAssetToolkit> LookDevToolkitWeak;

	// Actor 列表控件与数据源（弱引用，重建时丢弃失效项）
	TSharedPtr<SListView<TWeakObjectPtr<AActor>>> ListView;
	TArray<TWeakObjectPtr<AActor>> Items;

	// 已轮询到的版本号快照（-1 = 未同步过，首帧必触发）
	int32 CachedActorsVersion = -1;
	int32 CachedSelectionVersion = -1;

	// 视口 -> 列表方向同步进行中（SetItemSelection 会触发 OnSelectionChanged），防止反写递归
	bool bApplyingSelection = false;
};
