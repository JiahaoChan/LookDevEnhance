// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UAssetEditor.h"

#include "LookDevAssetEditor.generated.h"

/**
 * LookDev 资产编辑器实例
 *
 * 基于 UAssetEditor 的编辑器对象，内部创建 FLookDevAssetToolkit 作为界面与预览载体，
 * 由 UAssetEditorSubsystem 跟踪管理。一个编辑器窗口在配置好的 LookDev 场景中
 * 预览一个或多个资产；同一资产允许同时出现在多个窗口中。
 * 统一通过静态函数 OpenLookDevEditorForAssets 打开，内部按资产集合去重：
 * 已有窗口展示的资产集合与请求完全一致时聚焦该窗口，否则新开窗口。
 */
UCLASS()
class ULookDevAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

private:
	// 本编辑器预览的全部资产
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ObjectsToEdit;

	// 所有存活 LookDev 编辑器的弱引用注册表，用于资产集合去重；编辑器关闭后对应条目自动失效
	static TArray<TWeakObjectPtr<ULookDevAssetEditor>> OpenEditors;

public:
	void SetObjectsToEdit(const TArray<UObject*>& InObjects);

	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;

	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

	const TArray<TObjectPtr<UObject>>& GetEditedAssets() const { return ObjectsToEdit; }

	/**
	 *	在 LookDev 编辑器窗口中打开给定资产
	 *
	 *	@param	Assets	要打开的资产列表；空指针会被过滤，全部为空时不做任何事
	 *
	 *	@note	基于资产集合去重：已有窗口展示的集合与本次请求完全一致时聚焦该窗口，
	 *		否则新开窗口（同一资产可同时出现在多个窗口中）
	 */
	static void OpenLookDevEditorForAssets(const TArray<UObject*>& Assets);

	/**
	 *	将本编辑器的资产从 UAssetEditorSubsystem 的"已打开资产"注册表中摘除
	 *
	 *	Initialize 过程中 FAssetEditorToolkit 会把编辑中的资产注册进该注册表，
	 *	导致双击资产默认打开时被查重到本编辑器；摘除后双击资产与 LookDev 编辑器互不影响。
	 *	本编辑器的窗口仍由子系统持有（OwnedAssetEditors），正常跟踪与关闭不受影响。
	 */
	void DetachFromAssetEditorLookup();

private:
	/**
	 *	将本编辑器的窗口提到前台
	 */
	void BringToFront();
};
