// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#include "LookDevEditor/LookDevAssetEditor.h"

#include "Editor.h"
#include "LookDevEditor/LookDevAssetToolkit.h"
#include "Subsystems/AssetEditorSubsystem.h"

void ULookDevAssetEditor::SetObjectsToEdit(const TArray<UObject*>& InObjects)
{
	ObjectsToEdit = InObjects;
}

void ULookDevAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	for (UObject* Object : ObjectsToEdit)
	{
		if (Object)
		{
			InObjectsToEdit.Add(Object);
		}
	}
}

TSharedPtr<FBaseAssetToolkit> ULookDevAssetEditor::CreateToolkit()
{
	return MakeShareable(new FLookDevAssetToolkit(this));
}

TArray<TWeakObjectPtr<ULookDevAssetEditor>> ULookDevAssetEditor::OpenEditors;

void ULookDevAssetEditor::BringToFront()
{
	if (ToolkitInstance)
	{
		ToolkitInstance->FocusWindow();
	}
}

void ULookDevAssetEditor::OpenLookDevEditorForAssets(const TArray<UObject*>& Assets)
{
	TSet<UObject*> RequestedAssets;
	for (UObject* Asset : Assets)
	{
		if (Asset)
		{
			RequestedAssets.Add(Asset);
		}
	}

	if (RequestedAssets.Num() == 0)
	{
		return;
	}

	// 基于资产集合去重：仅当已有窗口展示的集合与本次请求完全一致时才聚焦该窗口，否则新开窗口
	// 先清理已关闭编辑器留下的失效弱引用
	OpenEditors.RemoveAll([](const TWeakObjectPtr<ULookDevAssetEditor>& Editor) { return !Editor.IsValid(); });

	for (const TWeakObjectPtr<ULookDevAssetEditor>& OpenEditor : OpenEditors)
	{
		TSet<UObject*> EditorAssets;
		for (const TObjectPtr<UObject>& Object : OpenEditor->GetEditedAssets())
		{
			if (Object)
			{
				EditorAssets.Add(Object);
			}
		}

		// 数量相等且互相包含，两个集合才完全一致
		if (EditorAssets.Num() == RequestedAssets.Num() && EditorAssets.Includes(RequestedAssets))
		{
			OpenEditor->BringToFront();
			return;
		}
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	ULookDevAssetEditor* AssetEditor = NewObject<ULookDevAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
	AssetEditor->SetObjectsToEdit(RequestedAssets.Array());
	AssetEditor->Initialize();
	AssetEditor->DetachFromAssetEditorLookup();
	OpenEditors.Add(AssetEditor);
}

void ULookDevAssetEditor::DetachFromAssetEditorLookup()
{
	if (ToolkitInstance == nullptr)
	{
		return;
	}

	// RemoveSingle 对已不存在的键值是安全的，窗口真正关闭时子系统的二次移除不受影响
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	for (const TObjectPtr<UObject>& Object : ObjectsToEdit)
	{
		if (Object)
		{
			AssetEditorSubsystem->NotifyAssetClosed(Object, ToolkitInstance);
		}
	}
}
