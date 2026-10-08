// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

// LookDevEnhance 插件模块的日志分类
DECLARE_LOG_CATEGORY_EXTERN(LogLookDevEnhance, Log, All);

/**
 * LookDevEnhance 插件的模块入口
 *
 * 本模块为 Editor 类型，加载阶段为 PostDefault。
 * 启动时向内容浏览器安装资产右键菜单扩展（“在 LookDev 编辑器中打开”），
 * 关闭时移除该扩展；具体的菜单逻辑由 FLookDevContentBrowserExtensions 实现。
 */
class FLookDevEnhanceModule : public IModuleInterface
{

public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;
};
