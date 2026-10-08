# LookDevEnhance 迭代文档（面向 AI 维护者）

本文件是插件后续迭代的入口文档，也是插件 Bug / 功能缺口清单的**单一事实来源**——插件自包含，不依赖所在项目的任何文件。接手改动时先读本文件。

## 一、架构速览

```
LookDevEnhance/
  LookDevEnhance.uplugin               Editor 模块（PostDefault）；LocalizationTargets 声明（LoadingPolicy=Editor）
  Config/Localization/LookDevEnhance.ini  本地化收集管线配置（路径用 %LOCPROJECTROOT%，可迁移）
  Content/Localization/LookDevEnhance/    en(原生)/zh-Hans 的 archive、po、locres
  Doc/                                 本文件 + 使用指南
  Source/LookDevEnhance/
    Public/LookDevEnhanceSettings.h    UDeveloperSettings：LookDev 场景软引用、可打开资产类型、材质预览网格
    Public/AssetsPreviewActor.h        AAssetsPreviewActor：预览环境 Actor（天穹/天光/三档方向光/后处理/PBR Checker/校准物）
    Private/LookDevContentBrowserExtensions.*  右键菜单（CommonAssetActions；全部选中资产受支持才显示）
    Private/LookDevEditor/
      LookDevAssetEditor.*             ULookDevAssetEditor（UAssetEditor）；OpenEditors 注册表按资产集合去重
      LookDevAssetToolkit.*            FLookDevAssetToolkit：布局（视口 0.8 / 详情栈 0.2）、场景流送、预览 Actor 生成、
                                       多视图副本（GenerateMultiViewCopies/ClearMultiViewCopies）
      LookDevViewportClient.*          FLookDevViewportClient：预览限定选择/gizmo、F 聚焦、FocusFromViewDirection
      SLookDevEditorViewport.*         视口 Slate：工具栏（相机/朝向归位下拉/视图模式+Lumen 可视化）
```

关键机制备忘（改动相关代码前先读这些代码段的注释）：

- **场景加载**：`ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr`（temp 包）流进 FPreviewScene；打开流程带进度条 + FinishAllCompilation。
- **GPU 崩溃规避（见 Bug-3）**：流送后 `ApplyRayTracingShadowMode(false)`，视口渲染 2 帧 + 着色器队列清空后恢复（600 帧兜底）；天光恒排除。不要随手简化这段时序。
- **天光**：裸 FPreviewScene 不自动调 `UpdateCaptureContents`，SLS_SpecifiedCubemap 天光需手动每帧调用。
- **Lumen**：预览世界开了 `AllowLumenPrimitiveTrackingInPreviewWorld`（对齐 MetaHuman 编辑器）。
- **窗口查重**：按资产集合完全一致才复用；与原生资产编辑器查重隔离（Initialize 后从 UAssetEditorSubsystem 注册表摘除）。
- **标签页颜色**：`GetDefaultTabColor()` 返回绿色，区分原生资产编辑器。
- **保存入口已移除**：菜单 Profile（LookDevNoSave）拒绝列表隐藏工具栏 Save 与 File 菜单 Save/Save As（注册在 RegisterToolbar、激活在 InitToolMenuContext，机制对齐引擎只读模式），`CanSaveAsset(As)` 恒 false 兜底。
- **资产转换**：统一入口 `CreatePreviewActorForAsset`——ActorFactory 优先（`TrySpawnPreviewActorWithFactory`，与拖入关卡同逻辑，跟踪根组件），工厂不支持回退自定义（`SpawnCustomPreviewActor`，仅材质球）；两条路径成功后统一过 `PostSpawnPreviewActor` 钩子做特殊化处理；点击子组件时 ProcessClick 按 Owner 归属解析回被跟踪组件；多视图副本也走本链路（`PreviewActorAssets` 映射找回源资产）。

## 二、Bug 清单

- **Bug-3（未收口）**：Blackwell（RTX 5060 Laptop）+ 驱动 573.24 下，新编辑器首次直接打开 LookDev 编辑器偶发 GPU 挂死（DXGI_ERROR_DEVICE_HUNG，ShadowBatch raygen）。已做同帧规避（延迟 2 帧恢复 RT 阴影 + 着色器队列清空 + 600 帧兜底）和天光排除，指向驱动层；规避方法：先随便打开一个关卡再开 LookDev 编辑器。待确认：升级驱动是否根治。
- ~~Bug-1：编辑属性时灯光持续累积旋转~~ ✅ 已修（捕获一次基准姿态 + 绝对角度设置；转台开启时定时器累加 TurntableAngle，关闭时 TurnLighting 为固定偏移）
- ~~Bug-2：预览世界天光整体缺失~~ ✅ 已修（UpdateCaptureContents）

## 三、功能缺口

- ~~Feature-2：相机书签/常用视角管理——编辑器内保存与恢复固定机位，便于标准视角下对比资产~~（已完成 2026-10-09：抽屉"书签"页，位置/旋转/FOV + 128x128 缩略图，逐资产序列化，见变更日志）
- Feature-3：材质预览网格快速切换——设置里已有 MaterialPreviewMesh，但编辑器内没有 UI 可临时切换球体/圆柱/平面/自定义网格
- ~~Feature-4：截图/缩略图导出~~（已完成 2026-10-08：工具栏 Screenshot 复合下拉，1K/2K/4K 预设 + 右下角水印，见变更日志；"相机参数固定"未做，相机书签属 Feature-2）
- Feature-5：批量 LookDev——对一个文件夹/多选资产批量打开并截图，生成对比报告

### 遗留的可选打磨项（非缺陷）

- Game View 下选中的预览组件除 gizmo 外无高亮反馈（可补描边）
- 多视图副本的角度集合与间距为硬编码（NumViews=8、45° 步进），可挪进 LookDevEnhanceSettings
- gizmo 交互依赖编辑器"新 TRS gizmo"设置开启（5.8 默认开启；切回旧 gizmo 时点选/拖拽静默失效）
- 多资产排布按 Actor 原点（Z=0），中心 pivot 的网格会半陷入地面（纯视觉）
- 回退照明的 SkyLight 在空场景中捕获为黑（不影响方向光）
- FLookDevViewportClient 持有 toolkit PreviewComponents 数组的裸指针（当前析构顺序安全，重构时注意）

## 四、本地化增量更新

范围约定：**只翻译显式 FText（LOCTEXT）和 AAssetsPreviewActor 设置类 UPROPERTY 的 DisplayName/ToolTip meta**；组件 UPROPERTY 不加 meta 不翻译；C++ 注释保持中文。

流程：

1. 改代码（LOCTEXT 源字符串一律英文，namespace 统一 `LookDevEnhance`）后**先编译**——GatherTextFromMetaData 读的是已编译 DLL 的反射数据。
2. 跑管线（收集→清单→归档→导入已有 PO→编译 locres→导出 PO 一次完成）：
   ```bash
   UnrealEditor-Cmd.exe <项目>.uproject -run=GatherText -Config="Plugins/LookDevEnhance/Config/Localization/LookDevEnhance.ini" -unattended -nop4
   ```
3. 编辑 `Content/Localization/LookDevEnhance/zh-Hans/LookDevEnhance.po`，填新条目的 msgstr。
4. 再跑一次同一命令导入并编译 locres；重启编辑器生效。

注意：

- 收集器会对所有字段自动生成 DisplayName 条目（EnsureFieldDisplayName），无需翻译的条目（组件、私有字段）留空 msgstr 回退英文。
- UHT 会把 Doxygen 注释变成 ToolTip 元数据：属性注释是中文时，英文界面的悬停提示会是中文。需要翻译的属性必须写显式英文 ToolTip meta 覆盖。
- Category 也参与收集（`UObjectCategory` 命名空间，key 为类目字符串本身，管线 ini 的 GatherTextStep3）；引擎已为常见类目（Lighting 等）自带翻译，自定义类目在插件 PO 里翻。
- **删除翻译的坑**：PO 导入会跳过空 msgstr（PortableObjectPipeline），archive 里的旧翻译不会被清掉。要删掉某条已有翻译：清空 PO 的 msgstr + 删除 `zh-Hans/LookDevEnhance.archive` 再跑管线（archive 从 manifest 重建后只剩 PO 里非空的翻译）。
- 运行时挂载靠 .uplugin `LocalizationTargets`（`FPluginManager::GetLocalizationPathsForEnabledPlugins`），项目侧不需要任何 ini；属性名/提示的中文显示还受编辑器偏好设置「使用本地化的属性名称/工具提示」开关控制。

## 五、协作约定（每次改完必做）

1. 改代码前重读目标文件（用户会随时手动改）。
2. C++ 改动必须编译验证：`Build.bat <ProjectName>Editor Win64 Development <项目>.uproject -waitmutex`；遇 "Live Coding is active" 报告用户按 Ctrl+Alt+F11，不要重试。
3. 收尾更新本文件：Bug/功能缺口的状态变化同步到第二、三节，重大改动追加到第六节变更日志。
4. 新增 UI 文本走第四节的本地化流程，不要在代码里留中文 UI 字符串。
5. 文件头版权注释统一为：`// Copyright TA.Jiahao.Chan, SCT-X8, XJTG, Alibaba-Inc. All Rights Reserved.`
6. **新增编辑器可调状态时评估是否纳入缓存**：给编辑器加新的可持久状态（AAssetsPreviewActor 的 EditAnywhere 属性、视口/相机状态、工具栏开关等）时，同步评估是否要加进 `FLookDevEditorCacheEntry`（LookDevEditorCache.h）——若是，需四处同步：Entry 字段（含 `Equals`）、`SaveEntry`/`LoadEntry` 的序列化键、`CaptureCacheEntry` 快照、`SetEditingObject` 的恢复应用；若明确不该持久化（如纯会话态），在 Entry 注释或本节说明理由。缓存设计背景：按资产集合 Key 存 Saved/LookDevEnhance/LookDevEditorCache.ini，0.5s 防抖轮询写盘，细节见 2026-10-08 变更日志。

## 六、变更日志

| 日期 | 摘要 |
|------|------|
| 2026-10-01 | LookDev 资产编辑器初版（右键菜单 + DeveloperSettings + UAssetEditor/FBaseAssetToolkit + 地图流送预览场景）；同资产查重、Game View、多选同窗口横排、gizmo 仅限预览 Actor |
| 2026-10-01 | 窗口去重改为按资产集合；Bug-1 修复；F 键聚焦（平滑过渡）；与原生编辑器查重隔离 |
| 2026-10-02 | 细节面板展示 AAssetsPreviewActor 可编辑属性；Bug-2 修复（天光）；Bug-3 治理（关卡式打开流程 + RT 阴影延迟恢复）；Selected Actor 详情面板；标签页绿色图标；Feature-7 多视图副本（后修订为 Y 轴两侧对称 + 槽位 45° 递增）；两栏布局；朝向归位下拉 |
| 2026-10-07 | LOCTEXT_NAMESPACE 统一为 "LookDevEnhance"；UI 本地化（en 原生 + zh-Hans），插件自包含（.uplugin LocalizationTargets + 插件内 gather 配置）；组件 UPROPERTY 不翻译；新增 Doc/ 双文档 |
| 2026-10-07 | 本地化修订：Category 纳入收集（ini 新增 GatherTextStep3，UObjectCategory 命名空间），补 PBR 校验/资产引用/组件/灯光/背景翻译；清除组件属性的残留翻译（PO 导入跳过空 msgstr，需删 archive 重建——已记入第四节注意事项） |
| 2026-10-07 | 本地化：ELightingScenarial 枚举 Entry 翻译（低/中/高对比度）——DisplayName 收集步骤去掉 Enum 排除，UHT 隐藏 MAX 条目不翻 |
| 2026-10-07 | 本地化：ULookDevEnhanceSettings 类 DisplayName（LookDev 增强）与 3 个属性（LookDev 场景/材质预览网格/支持的资产类型）补中文翻译 |
| 2026-10-07 | 新增预览旋转偏移：AAssetsPreviewActor 增加 EditAnywhere 属性 PreviewRotationOffset（新类目 Preview，校正正面不朝 +X 的资产）；Toolkit 生成预览/副本时按绝对值落位，并通过 FCoreUObjectDelegates::OnObjectPropertyChanged 监听该属性、以增量方式同步到全部预览 Actor（保留 Gizmo 手动调整）；补类目/属性名/提示的中文翻译 |
| 2026-10-07 | 修复旋转偏移实时同步失效：详情面板编辑 FRotator 的 Yaw/Pitch/Roll 子字段时，FPropertyChangedEvent.Property 是叶子属性（Yaw），MemberProperty 才是对象成员属性（PreviewRotationOffset），过滤改为优先 MemberProperty |
| 2026-10-08 | 资产 -> 预览 Actor 转换改为 ActorFactory 优先：`CreatePreviewActorForAsset` 成为统一入口（审核/迭代从这里进），先经 `FActorFactoryAssetProxy::GetFactoryForAssetObject` + `UActorFactory::CreateActor` 生成（与资产拖入关卡同逻辑，工厂 Actor 跟踪根组件），工厂不支持时回退 `SpawnCustomPreviewActor`（裸 Actor + 手工网格组件，覆盖材质球）；`ProcessClick` 命中解析改为优先精确匹配、否则按 Owner 归属匹配（工厂 Actor 点击到子组件时解析回被跟踪组件） |
| 2026-10-08 | 链路补特殊化处理钩子：两条生成路径成功后统一经过 `PostSpawnPreviewActor(UObject* Asset, AActor*, USceneComponent*)`（首参为源资产便于按类型分支；当前无处理项，碰撞/Mobility/渲染标志等调整集中加在这里）；`SpawnCustomPreviewActor` 删除静态/骨骼网格分支（ActorFactory 已涵盖），只保留材质路径 |
| 2026-10-08 | 多视图副本改走统一生成链路：新增 `PreviewActorAssets` 映射（预览 Actor -> 源资产，原预览与副本都登记，ClearMultiViewCopies 时移除）；`GenerateMultiViewCopies` 不再 DuplicateObject 克隆组件，改为找回源资产后逐个调 `CreatePreviewActorForAsset`——副本与原预览共享 ActorFactory/兜底/PostSpawn 全部逻辑，副本的副本也能生成 |
| 2026-10-08 | 修复副本动画进度不一致：动作序列/蒙太奇经 UActorFactoryAnimationAsset 以单节点模式播放，副本从 0 开始；GenerateMultiViewCopies 生成前捕获本体 UAnimSingleNodeInstance 的进度/速率/播放状态，副本生成后 SetPlayRate/SetPosition(bFireNotifies=false)/SetPlaying 对齐（不同步本体，避免本体视觉跳变） |
| 2026-10-08 | 视图朝向菜单新增两个预设：Default（归位到初始机位，旋转常量收敛为 FLookDevViewportClient::DefaultViewRotation，SpawnPreviewForAssets 共用）；Character Face（聚焦 head 骨骼，参考 Persona FSkeletonSelectionEditMode::GetCameraTarget——骨骼位置 + 30cm 半径、包围盒只沿 X 轴展开；FLookDevViewportClient::CanFocusHeadBone 门控，仅预览单个含 head 骨骼的骨骼网格组件时可用）；补 zh-Hans 翻译（默认视角/角色正脸） |
| 2026-10-08 | Multi-View Copies 改为复合下拉框：主按钮生成副本，下拉内是 8 个角度槽位的多选复选框（ToggleButton + bShouldCloseWindowAfterMenuSelection=false 保持菜单打开连续勾选），未勾选的角度生成时跳过；槽位状态存工具集成员 MultiViewSlotMask 位掩码（会话级，不持久化），槽位角度收敛为 GetMultiViewCopySlotYaw/IsMultiViewCopySlotEnabled/SetMultiViewCopySlotEnabled 接口；补 zh-Hans 翻译（视图角度） |
| 2026-10-08 | 多视图副本修订：默认 ±135° 不勾选（掩码 0xBD）；位置改为紧凑补位——GenerateMultiViewCopies 先收集启用槽位 EnabledSlotIndices，副本位置按 PackedSlotIndex（跳过原型 0 号位）依次靠拢不留空档，旋转仍绑定角度槽位本身 |
| 2026-10-08 | 新增编辑器状态序列化缓存（LookDevEditorCache.h/.cpp）：按资产集合 Key（排序包路径 "|" 拼接）写入 Saved/LookDevEnhance/LookDevEditorCache.ini（单 ini + 每 Key 一分节，FConfigFile 直读直写，不注册 GConfig）；缓存环境 Actor 全部 EditAnywhere 属性、视口 FOV、相机位置/旋转、副本角度掩码与生成标记。恢复顺序（SetEditingObject）：应用环境（赋值 + Update()）→ 生成预览（有相机姿态则跳过默认聚焦直接恢复视角与 FOV）→ 按掩码自动重建副本。写盘为 0.5s FTSTicker 防抖轮询（CaptureCacheEntry 快照 Equals 比较有变化才写）+ 析构 FlushEditorCache 兜底 |
| 2026-10-08 | 缓存修订：应用户要求去掉 bRaytracingShadow=true 首开的特殊处理（强制禁用 + 重新武装延迟恢复整段删除，Update() 按缓存值直接开关 RT 阴影）；仅保留缓存为 false 时取消 LoadLookDevScene 延迟恢复的分支（防覆盖缓存值）；SetDeferredRTShadows 无调用方一并移除 |
| 2026-10-08 | 协作约定新增第 6 条：新增编辑器可调状态时评估是否纳入 FLookDevEditorCacheEntry（四处同步清单 + 不持久化须注明理由） |
| 2026-10-08 | 视口屏幕分辨率固定 override=100：FLookDevViewportClient 构造时 SetPreviewScreenPercentage(100)+SetPreviewingScreenPercentage(true)（UE5.8 per-viewport 机制，EditorViewportClient.cpp:4940-4966 每帧经 FLegacyScreenPercentageDriver 消费；100% 时引擎强制关 ScreenPercentage show flag 即全分辨率渲染），Tick 每帧钉回防用户经视口 Options 菜单改动；应用户要求不纳入缓存序列化 |
| 2026-10-08 | 环境详情 revert 基准改为场景实例快照：打开时（缓存应用之前）DuplicateObject 预览环境 Actor 为 EnvironmentSnapshot（TStrongObjectPtr 防 GC），经 PropertyEditorPolicy::RegisterArchetypePolicy 全局策略（UE::LookDevEnhance::FSnapshotArchetypePolicy，按对象指针精确过滤）把该 Actor 的 Archetype 重定向到快照——revert 箭头出现/消失与回写值都以场景实例配置为基准而非蓝图类默认值（LevelInstance 属性覆盖同款机制）；须在 DetailsView->SetObject 之前注册（节点 diff 缓存按注册时 archetype 建立），缓存应用后 ForceRefresh；析构先注销策略再卸场景；策略成员用 TSharedPtr 持有（IArchetypePolicy 析构非虚，TUniquePtr 会触发 C5205 UB 警告） |
| 2026-10-08 | 详情面板改为浮空抽屉（仿 ContentDrawer）：新增 SLookDevDetailsDrawer 控件，在 SLookDevEditorViewport::Construct 里把基类视口内容包进 SOverlay、抽屉以 HAlign_Right 浮层覆盖视口右侧（宽 450、顶部留 44px 避开视口工具栏），不参与停靠布局，视口保持完整宽高比；布局改全宽视口（Standalone_LookDevEditor_Layout_v2），并注销基类 DetailsTabID、移除 SelectedActorTabID 停靠 Tab（防同一 DetailsView 同时挂在抽屉与停靠页两处）；抽屉内"环境详情/选中 Actor"两页经 SWidgetSwitcher 切换，显隐与当前页由工具集 ELookDevDetailsPanel ActiveDetailsPanel 驱动（默认展开环境页，延续旧版默认显示详情的行为），视口工具栏新增两个 ToggleButton（经 UUnrealEdViewportToolbarContext::AssetEditorToolkit 找回工具集）与抽屉头部页面切换/X 收起按钮共用 ToggleDetailsPanel；抽屉显隐/页面为纯会话 UI 状态，不纳入 FLookDevEditorCacheEntry（约定第 6 条）；注意 SOverlay.h 在 SlateCore（Widgets/SOverlay.h）、SWidgetSwitcher.h 在 Slate（Widgets/Layout/SWidgetSwitcher.h）；补 zh-Hans 翻译（环境详情/选中 Actor/关闭详情面板等 9 条） |
| 2026-10-08 | 抽屉修订：①展开/收起加宽度动画——FCurveSequence（0.25s CubicOut）驱动面板 SBox WidthOverride 0↔450，Tick 检测 ActiveDetailsPanel 的 收起↔展开 切换触发 Play/反向 Play（展开态下切换页面不动画；构建时默认展开则 JumpToEnd 跳过开场动画；CurveSequence.h 在 SlateCore 的 Animation/ 下）；②视口工具栏的两个 ToggleButton 移除（用户认为不美观），改为视口最右缘窄入口列——抽屉收起后显示一条纵向图标按钮列（Details/Actor 图标 + tooltip，贴右缘顶部、工具栏下方），点击展开到对应页面；面板可见性=展开中或动画未归零，入口条可见性=完全收起后；补入口条 tooltip 的 zh-Hans 翻译（2 条） |
| 2026-10-08 | 修复抽屉动画疯狂循环：FCurveSequence::Play 的第二参数是 bPlayLooped 而非 bReverse，关闭时 Play(AsShared(), true) 实际开启了无限循环播放；改为 PlayRelative(AsShared(), bIsOpen)（未播放时按端点 Play/PlayReverse，播放中方向相反则 Reverse 掉头，中途反向无跳变）；注意 FCurveSequence 初始 bInReverse=true、AddCurve 后处于起点（lerp 0） |
| 2026-10-08 | 抽屉默认改为收起：ActiveDetailsPanel 初值 Environment → None，打开编辑器只显示视口右缘入口条，由用户按需展开 |
| 2026-10-08 | 新增高清截图工具（工具栏 Screenshot 复合下拉）：主按钮截图，下拉内为分辨率预设单选（1K/2K/4K，16:9 传统分辨率，默认 2K）+ 水印开关；实现对齐关卡编辑器 HighResScreenshot 对话框的内部调用——GetHighResScreenshotConfig().SetResolution（整视口按倍数渲染）+ CaptureRegion（视口内居中 16:9 裁剪区 × 倍数）+ SetFilename（Saved/Screenshots/<平台>/LookDevScreenshot_<时间戳>，FilenameOverride 带路径时不追加序号）+ SceneViewport->TakeHighResScreenShot()（DummyViewport 离屏渲染，引擎自带完成通知）；FScreenshotRequest::OnScreenshotRequestProcessed 一次性订阅（AddSP，回调即退订，析构兜底）在引擎写盘后叠加水印——插件 Resources/LookDevWatermark.png（缺失静默跳过），缩放到截图短边 4% 高、边距 2%，BGRA8 alpha 混合后 SaveImageByExtension 覆盖写回；回调里还原 FilenameOverride/CaptureRegion 防污染关卡编辑器对话框；截图设置存全局 ini（Saved/LookDevEnhance/GlobalSettings.ini，改动即写），不进 per-asset 缓存（约定第 6 条：非单资产状态而是全局偏好）；Build.cs 新增 ImageCore/ImageWrapper/Projects 依赖（IPluginManager.h 在 Projects 的 Interfaces/ 子目录）；补 zh-Hans 翻译（截图/分辨率/水印等 5 条） |
| 2026-10-08 | 截图设置持久化移除：应用户要求删除 LoadScreenshotSettings/SaveScreenshotSettings（分辨率预设与水印开关改为纯会话态，每次打开编辑器回到默认 2K + 水印开）；GlobalSettings.ini 不再使用；水印图临时切到 Resources/Icon128.png 做效果实验（正式图待定）；同轮把 .cpp 函数实现顺序重排到与用户调整后的 .h 声明顺序一致（纯移动无逻辑改动） |
| 2026-10-09 | 修复书签缩略图 ensure 崩溃（SlateDynamicImageBrush.h:120）：FImageUtils::ImportFileAsTexture2D 导入的纹理不在 RootSet，FSlateDynamicImageBrush 构造时对非 RootSet 纹理触发 ensure（引擎注释：加载屏场景会崩）；对齐引擎 PIEPreviewDevice.cpp 用法——导入后 AddToRoot()，面板重建列表前逐个 RemoveFromRoot()，新增析构兜底防编辑器关闭时 RootSet 泄漏 |
| 2026-10-09 | 修复视口点选预览 Actor 无效（选中 Actor 详情面板恒空 + 无变换 gizmo）：①详情不刷新——USelection::Select 走元素集 Added 同步只广播 SelectObjectEvent，不广播面板监听的 SelectionChangedEvent（Selection.cpp FLegacySync 分支），ProcessClick 末尾补 NoteSelectionChanged() 手动广播；②无 gizmo——FEditorModeTools 的 WidgetMode 初值 WM_None（FWidget::Render 的 switch 不画任何东西），关卡编辑器在视口构造时显式 SetWidgetMode(WM_Translate)（LevelEditorViewport.cpp:2380），FLookDevViewportClient 构造函数对齐补上；③gizmo 枢轴——ModeTools->PivotLocation 只被关卡编辑器 UpdatePivotLocationForSelection 维护，资产编辑器无人代劳会停在原点，OnPreviewSelectionChanged 里按选中组件位置均值 SetPivotLocation；④点击 gizmo 轴向（HWidgetAxis）时不再误清空选择 |
| 2026-10-09 | 支持 gizmo 拖拽移动预览 Actor（用户决策：允许拖拽，变换不序列化）：新增 FLookDevViewportClient::InputWidgetDelta 重写——基类把 delta 转给 EdMode 旧版 helper，而 FLegacyEdModeWidgetHelper::InputDelta 只实现属性 widget（MakeEditWidget UPROPERTY）的拖拽、不移动选中组件（关卡编辑器的移动是新 ITF gizmo 干的活，资产编辑器 ModeTools 不走那条路）；自行把 Drag/Rot/Scale 应用到选中预览组件所属 Actor（按 Owner 去重、只动被跟踪的预览组件，场景 Actor 锁定），旋转/缩放围绕 PivotLocation（与关卡编辑器多选行为一致），平移同步 PivotLocation/SnappedLocation、旋转调 ApplyDeltaToRotateWidget 刷新视觉；拖拽为会话内调整不入 FLookDevEditorCacheEntry（约定第 6 条：重开编辑器回标准排布位是刻意语义，避免手动变换覆盖自动排布）；与预览旋转偏移兼容（偏移本来就是增量叠加，保留手动旋转） |
| 2026-10-09 | CanOpenInLookDevEditor 支持蓝图资产：资产类是 UBlueprint 时按资产注册表 NativeParentClass 标签（FBlueprintTags::NativeParentClassPath，UBlueprint::GetAssetRegistryTags 导出，无需加载资产本体）取原生父类替换后判断（FSoftClassPath::ResolveClass，未加载则 TryLoadClass；配置里配的是原生父类），IsChildOf 循环同时涵盖原生父类是配置类子类的情况；注意 FSoftClassPath 在 UObject/SoftObjectPath.h（没有 SoftClassPath.h），且没有 LoadSynchronous，用 TryLoadClass<T>() |
| 2026-10-09 | 多视图副本间距可配置：Multi-Copy 下拉新增间距输入框（SSpinBox，FToolMenuEntry::InitWidget 自定义控件条目；单位 cm，语义=相邻副本包围盒净距离，公式 HalfSpacing = 包围盒半径 + 间距/2，默认 50 与旧版 25 边距视觉等价；负值钳 0，只影响之后生成的副本）；逐资产序列化——FLookDevEditorCacheEntry 新增 MultiViewCopySpacing 字段（四处同步：结构默认值/Equals/ini 读写/工具集成员，约定第 6 条持久化项）；旧缓存无该键时 GetFloat 保留默认值，向后兼容 |
| 2026-10-09 | ViewDirection 下拉新增 6 个 15° 微俯视预设（X±15/Y±15/Z±15）：水平轴向同朝向 + Pitch=-15，顶/底视角收敛为 75° 俯/仰（15° 余量）；标签为纯符号不翻译（与 X+ 等一致），tooltip 复用既有格式化条目 |
| 2026-10-09 | 多视图副本生成按钮唯一化：已存在副本时 GenerateMultiViewCopies 早退改调新增的 RelayoutMultiViewCopies——按生成时登记的排布信息（新成员 MultiViewCopyLayouts：副本 Actor -> 原型组件 + 紧凑槽位序号）与当前间距重排位置（原型以当前位置为基准，旋转与手动拖拽保留），不再重复生成；ClearMultiViewCopies 同步清空映射；Generate 按钮 tooltip 更新（补 zh-Hans 翻译） |
| 2026-10-09 | 重构：预览跟踪从组件改为以 Actor 为单位——FLookDevAssetToolkit::PreviewComponents 与 FLookDevViewportClient::PreviewComponents 统一改为 PreviewActors（TArray<TWeakObjectPtr<AActor>>）；选择机制仍走组件选择集（选中 Actor 的根组件，gizmo helper 的 ShouldDrawWidget 要求组件选择），ProcessClick 命中解析简化为纯 Owner 归属匹配；包围盒测量统一用 AActor::GetComponentsBoundingBox(true)（注意 5.8 没有 CalculateComponentsBoundingBox，那个名字是本地空间版本 CalculateComponentsBoundingBoxInLocalSpace）；GenerateMultiViewCopies 源收集/副本登记/排布信息（FLookDevMultiViewCopyLayout::SourceComponent → SourceActor）、ClearMultiViewCopies、ApplyPreviewRotationOffsetDelta 全部 Actor 化，Owner 去重逻辑随之删除；SpawnPreviewForAssets 布局测量改用局部 LayoutComponents 数组 |
| 2026-10-09 | LookDevViewportClient.cpp 函数实现顺序重排为与 .h 声明一致（Tick 上移至 InputWidgetDelta 之后、ComputeFocusBounds 先于 GetSingleSkeletalPreviewComponent），纯移动无逻辑改动 |
| 2026-10-09 | 副本唯一化 v2：应用户要求改为"已存在副本时 GenerateMultiViewCopies 先 ClearMultiViewCopies 全部清除再重新生成"（角度勾选 / 间距 / 选中范围修改立即生效，副本数量变化立即响应）；v1 的 RelayoutMultiViewCopies / FLookDevMultiViewCopyLayout / MultiViewCopyLayouts 整套移除；Generate 按钮 tooltip msgid 变更并补 zh-Hans 翻译 |
| 2026-10-09 | 选中 Actor 面板对齐关卡编辑器 SActorDetails：①详情视图过滤拆分——CreateDetailsView 重构为 CreateDetailsViewInternal(bHideEnvironmentUnrelatedCategories)，环境详情保持隐藏 Actor/Rendering/LevelInstance/Transform（含 TransformCommon 自定义行委托），选中 Actor 详情不再隐藏，全局注册的 FActorDetails 定制器注入的 Transform 组合控件（锁/重置/复制粘贴，TransformCommon 类别 AddCustomBuilder）自动出现；②新增组件树——SSubobjectInstanceEditor（Build.cs 新增 SubobjectEditor/SubobjectDataInterface 依赖），ObjectContext=当前选中预览 Actor（SelectedPreviewActor）、AllowEditing=false，放在抽屉"选中 Actor"页面上方（SSplitter 纵向 0.3/0.7，对齐 SActorDetails.cpp:503-512 的组合；组件树不在 DetailsView 节点树内，必须自行组合）；③双向选择同步——视口选择变化时 PrePotentialTargetChange+UpdateTree 重建树并把树内选中同步到视口选中组件（SyncComponentTreeSelectionFromViewport，对齐 SActorDetails:737-743/1068-1082），树内点选组件时详情切换为该组件并同步视口选择集+gizmo 枢轴（OnComponentTreeSelectionUpdated，根 Actor 节点选中回退到整个 Actor），bSelectionSyncGuard 防双向递归；注意：SSubobjectInstanceEditor 把 UpdateTree 重声明成 protected，成员/getter 类型放宽到基类 SSubobjectEditor；PrePotentialTargetChange 是实验性 API 需 PRAGMA_DISABLE_DEPRECATION_WARNINGS 抑制 C4996 |
| 2026-10-09 | 视口选择描边：选中预览 Actor 现在画橙色描边——①SetGameView(true) 会关掉 Selection 与 SelectionOutline（EditorViewportClient.cpp SetGameView 分支），FLookDevViewportClient 构造函数恢复 EngineShowFlags.SetSelection(true) + SetSelectionOutline(GetDefault<ULevelEditorViewportSettings>()->bUseSelectionOutline)（FStaticMeshEditorViewportClient 同款，描边 pass 门控在 PostProcessing.cpp 的 EPass::SelectionOutline，5.8 不依赖 PostProcessing flag）；②预览组件逐个绑定 SelectionOverrideDelegate（UPrimitiveComponent::FSelectionOverride，判定查 ModeTools 本地组件选择集——关卡编辑器绑的 GUnrealEd 全局选择集覆盖不到预览世界，静态网格编辑器 / Persona / SCS 编辑器都是绑自己的 delegate）+ PushSelectionToProxy 推送代理选中标记（bIndividuallySelected 路径，与 bSelectable 无关，无需 MarkRenderStateDirty）；Tick 里 EnsureSelectionOutlineOverrides 给新组件补绑，选择变化时 RefreshSelectionOutlineProxies 全量重推，工具集析构前 ClearSelectionOutlineOverrides 解绑防野委托；注意 5.8 没有 GetComponents<T>() 返回值重载，用出参版本；LevelEditorViewportSettings.h 的 include 路径是 Settings/LevelEditorViewportSettings.h（UnrealEd/Classes 下） |
| 2026-10-09 | 修复多开编辑器工具栏串台：工具栏菜单 AssetEditor.LookDevEditor.ToolBar 按 ToolkitFName 命名、被所有编辑器实例共享，RegisterToolbar 里 CreateSP(this) 的委托把最后注册的实例冻结进共享菜单，导致一个窗口的 Multi-Copy/Clear Copies/Screenshot 按钮操作到另一个窗口；改为对齐 FStaticMeshEditor 的动态条目方案——整个 LookDev 区块注册为 Section.AddDynamicEntry(FNewToolMenuSectionDelegate::CreateStatic(FillToolbarSection))，每个窗口生成自己的工具栏时回调各执行一次，经 GetToolkitFromToolMenu（InSection.FindContext<UAssetEditorToolkitMenuContext>()->Toolkit，GenerateToolbar 在 AssetEditorToolkit.cpp:1527-1529 塞入各实例 Toolkit，下拉子菜单继承同一上下文）找回当前窗口实例后再 CreateSP 绑定；弃坑记录：TCommands 命令路由方案对 ToolBarComboButton 无效（ToolMenus.cpp:1968 的逐实例命令解析只覆盖 ToolBarButton/MenuEntry），FToolUIActionChoice(Command, CommandList) 双参构造会在注册时立即解析冻结实例，均不可用于复合按钮 |
| 2026-10-09 | 预览 Actor 落点改为包围盒底部中心对齐：SpawnPreviewForAssets 第二遍排布原先把包围盒中心对齐槽位且只补偿 Y 轴，骨骼模型固定包围盒中心常与枢轴不重合导致枢轴偏离世界原点；中途曾误改为枢轴对齐，应用户要求定为"包围盒底部中心点（水平居中 + Min.Z 底部落地）对齐槽位中心"（Offset = 槽位中心 - BoundsBottomCenter），单资产即落在世界原点；槽位宽度仍按包围盒计算防多资产穿插 |
| 2026-10-09 | 多选详情对齐关卡编辑器：OnPreviewSelectionChanged 从"只取第一个选中 Actor"改为收集全部选中组件的 Owner Actor（去重）后 SetObjects 多对象显示共有属性；组件树按 SActorDetails 语义只在恰好单选一个 Actor 时显示（多选隐藏）；树内点选（OnComponentTreeSelectionUpdated）路径不受影响 |
| 2026-10-09 | 新增预览 Actor 大纲面板（轻量 WorldOutliner）：Drawer 面板栈新增"场景"页（SLookDevOutlinerPanel，SListView 列出全部预览 Actor 与多视图副本，副本行名由 GetPreviewActorDisplayLabel 追加相对原型的朝向角，Tooltip 显示源资产路径），解决特效等难以视口点选的 Actor 的选中问题；单击选中（Ctrl/Shift 多选）经 SelectPreviewActors 写回组件选择集（选根组件 + NoteSelectionChanged 手动广播，详情/组件树/gizmo 枢轴/选择描边由 OnPreviewSelectionChanged 统一刷新），双击在选中后 FocusOnSelection 平滑聚焦（FocusFromViewDirection 传当前视口旋转，复用 F 键的 ComputeFocusBounds 选中优先逻辑）；同步用版本号轮询模式——新计数器 PreviewActorsVersion（SpawnPreviewForAssets/GenerateMultiViewCopies/ClearMultiViewCopies 递增）与 PreviewSelectionVersion（OnPreviewSelectionChanged/OnComponentTreeSelectionUpdated 递增），面板 Tick 轮询、不注册委托，视口->列表同步前做集合相等比较 + bApplyingSelection 守卫防递归；ELookDevDetailsPanel 新增 Outliner，Drawer 头部按钮/右侧入口条（LevelEditor.Tabs.Outliner 图标）/WidgetSwitcher 各加一项；注意 SListView::OnMouseButtonDoubleClick 委托签名是 void(ItemType) 不是 FReply；纯视图状态不入 FLookDevEditorCacheEntry（约定第 6 条注明理由：列表内容完全由会话内预览状态推导，无用户配置）；新增 4 条 LOCTEXT 并补 zh-Hans 翻译 |
| 2026-10-09 | Drawer 页签顺序调整 + 改名：大纲页挪到"选中 Actor"之后、"摄像机书签"之前（ELookDevDetailsPanel 枚举顺序同步改为 None/Environment/SelectedActor/Outliner/CameraBookmarks，WidgetSwitcher 下标映射 0/1/2/3、头部按钮与右侧入口条按钮同序调整）；页签名"Scene"改为"Outliner-大纲"（msgid 含中文，zh-Hans 留空 msgstr，旧"Scene"条目由 GatherText 自动清除）；ActiveDetailsPanel 为会话态不序列化，枚举值重排无兼容问题 |
| 2026-10-09 | 新增选中高亮描边开关："选中 Actor"页顶部复选框（组件树上方）——工具集新成员 bSelectionOutlineEnabled（默认开）+ IsSelectionOutlineEnabled/SetSelectionOutlineEnabled，转发到 FLookDevViewportClient::SetSelectionOutlineEnabled（同时开关 Selection 与 SelectionOutline 两个 show flags，描边后处理的 EPass::SelectionOutline 门控要求两者都开；开启时 SelectionOutline 对齐关卡编辑器读 ULevelEditorViewportSettings::bUseSelectionOutline 偏好）并 Invalidate 刷新；CreateEditorViewportClient 创建视口客户端后立即以复选框状态覆盖构造初始值（构造函数按用户偏好初始化），保证勾选态与实际渲染一致；会话态不序列化（约定第 6 条注明理由：用户明确要求不持久化，每次打开默认开启）；补 2 条 zh-Hans 翻译 |
| 2026-10-09 | 选中高亮开关改为 G 键（应用户要求撤销上一轮的复选框方案）：工具集 bSelectionOutlineEnabled / IsSelectionOutlineEnabled / SetSelectionOutlineEnabled 与 CreateEditorViewportClient 里的初始化调用整套移除，"选中 Actor"页恢复为纯组件树+详情；G 键重新赋予语义 = 切换预览辅助显示（选中高亮描边 + 视口左下角坐标轴指示器），本编辑器恒为 GameView、G 不再是 GameView 总开关；FLookDevViewportClient 新成员 bPreviewHelpersEnabled（默认开）+ SetPreviewHelpersEnabled（Selection/SelectionOutline 双 show flags 与 bDrawAxesGame 同开同关并 Invalidate 刷新）；InputKey 必须拦截在基类之前——否则基类 FEditorViewportClient::InputKey 会把 G 当 SetGameView 开关；废弃的 SelectionOutlineToggle 两条 LOCTEXT 由 GatherText 自动清除，无新增文本 |
| 2026-10-09 | 大纲页签名规范化 + G 键补 gizmo 开关：页签 msgid 改为纯英文 "Outliner"、zh-Hans 翻译为"大纲"（原 "Outliner-大纲" 混合条目由 GatherText 自动清除）；G 键预览辅助显示补上变换 gizmo——SetPreviewHelpersEnabled 新增 ShowWidget(bEnabled)（gizmo 可见性检查 ViewportClient->GetShowWidget()，EditorTransformGizmoSource.cpp:75），与选中描边、坐标轴指示器三者同开同关 |
| 2026-10-09 | 修复多资产打开崩溃（Assertion failed: EditingObjects.Num() == 1，AssetEditorToolkit.cpp:583）：独立窗口主 Tab 的标签与 Tooltip 属性绑定的是 FAssetEditorToolkit::GetToolkitName / GetToolkitToolTipText（AssetEditorToolkit.cpp:139-141），二者内部经 GetEditingObject() 取唯一编辑对象、多资产时必崩（该缺陷自多资产打开功能上线即存在，单资产不触发）；FLookDevAssetToolkit 重写这两个 override——GetToolkitName 直接复用 GetBaseToolkitName 的多资产实现（"{0} (+{1} more)"），GetToolkitToolTipText 单资产用基类静态 GetToolTipTextForObject（含资产路径）、多资产逐行拼接资产名（基于本类 EditedAssets，基类 EditingObjects 在 5.8 是 private 不可直接访问）；已排查引擎内 GetEditingObject 全部调用点，仅此两处会在我们的流程中触发 |
| 2026-10-09 | "角色正脸"聚焦条件放宽：多资产同开时视图朝向菜单的 Character Face 原先恒灰（GetSingleSkeletalPreviewComponent 要求恰好单个预览 Actor）；重构为 GetHeadFocusSkeletalComponent——优先当前选中的预览 Actor（选择集 Owner 归属匹配 PreviewActors），未选中时回退"恰好单个预览 Actor"，目标须含 head 骨骼；FocusOnHeadBone/CanFocusHeadBone 统一改走该入口，菜单启用态随选择动态刷新 |
| 2026-10-09 | 多资产副本全局行带排布：GenerateMultiViewCopies 原先每个源 Actor 各自以自身为中心沿 Y 轴排副本，多资产同开时副本行互相重叠；改为源集合覆盖全部预览 Actor 时启用全局排布——每个原型占一个 Y 轴行带（行带半宽 = (packed 最大序号 (N+1)/2 * 2 + 1) * HalfSpacing，相邻行带净距恰为 MultiViewCopySpacing），所有行带以原点为中心依次排开，原型 Actor 迁就偏移到行带中心（SetActorLocation）；只选中部分 Actor 生成时保持各源原位（旧行为），由用户自行控制；单资产行为与旧版等价（行带关于原点对称、原型在 0） |
| 2026-10-09 | 清除副本恢复原位 + 间距允许负值：①ClearMultiViewCopies 在确实清过副本后调用新增的 RelayoutOriginalPreviewActors——把原预览 Actor（排除副本）按初始排布规则重新排成一行（局部包围盒 CalculateComponentsBoundingBoxInLocalSpace 测量口径与 SpawnPreviewForAssets 一致、相邻净距 50cm、底部中心点对齐槽位；只恢复位置，保留会话中的手动旋转）；无副本时早退不碰，避免误清手动拖拽位；②MultiViewCopySpacing 允许负值（副本重叠压紧）——setter 去掉钳 0、SpinBox 去掉 MinValue(0)，布局两侧 HalfSpacing 钳最小 1cm 防止槽位坍缩/顺序翻转；tooltip msgid 变更并补 zh-Hans 翻译 |
| 2026-10-09 | 排布间距语义统一为"预览排布间距"（三个方向按用户确认全部实施）：①原型行与副本行带共用同一 MultiViewCopySpacing——RelayoutOriginalPreviewActors 的 50cm 硬编码改为读该值（HalfSpacing = 包围盒半径 + 间距/2，钳 1cm）；②间距修改立即生效——SetMultiViewCopySpacing 挂钩新增的 RelayoutMultiViewCopies：有副本时纯位置重排（只挪 Y，动画/手动旋转/XZ 调整保留），无副本时退化重排原型行；③加回轻量排布记录 FLookDevMultiViewCopyLayout（副本 -> 原型 + PackedSlotIndex，生成时写入、ClearMultiViewCopies 清空，不参与生成决策——注意与 v1 被删的 MultiViewCopyLayouts 目的不同：那次是"重排代替重新生成"被否决，这次仅为间距重排服务）；重排的行带公式与生成时完全一致（逐源取 packed 最大序号、世界包围盒测量、全覆盖才全局排布）；缓存恢复路径补 else 分支：无副本恢复时按缓存间距重排原型；边界保持不变：角度勾选（数量变化）仍下次点生成按钮时先删再生成；tooltip msgid 变更并补 zh-Hans 翻译 |
| 2026-10-09 | 修复奇数副本数时多资产行带拼接处空位（如去掉 -180° 后 7 个副本）：packed 槽位在 N 为奇数时左右不对称（-3..+4），而行带半宽原按对称最大序号 (MaxMagnitude*2+1)*H 计算，行带一侧留出 2H 空带、两个资产的行带拼接处出现空位；改为按槽位实际左右伸展量计算——左伸展 = (-Min*2+1)*H、行宽 = ((Max-Min)*2+2)*H，原型偏移到行带左侧伸展量处（不再假设行带中心）；GenerateMultiViewCopies 与 RelayoutMultiViewCopies 两侧同公式（生成侧槽位范围由 N 直接推出 Min/MaxPackedIndex，重排侧从排布记录取实际 Min/Max）；packed 序号计算提取为 ComputePackedIndex lambda 两处复用 |
| 2026-10-09 | 使用指南功能对齐刷新（纯文档，无代码改动）：补蓝图资产按原生父类判断、绿色标签页图标、缓存范围列举（副本间距/生成状态）、动画资产副本对齐本体播放进度；大纲页签名更新为"大纲/Outliner"本地化对；多资产副本描述去掉"正中间"（奇数副本修复后按实际伸展量偏移）；截图水印注明正式图未定（临时用插件图标） |
| 2026-10-09 | 移除保存入口（本编辑器不允许保存，改动只作用于预览世界）：对齐引擎只读模式的菜单 Profile 机制——RegisterToolbar 里注册 LookDevNoSave Profile（共享菜单 AssetEditorToolbar.CommonActions 的 MenuPermissions 拒绝 SaveAsset、本编辑器 File 子菜单（GetToolMenuName()+".File"）拒绝 SaveAsset/SaveAssetAs；拒绝项按 Profile 名隔离，其他资产编辑器不激活本 Profile 不受影响；AddRuntimeMenuProfile 对未注册菜单名可用），InitToolMenuContext 重写无条件激活该 Profile（对齐 FAssetEditorToolkit 只读模式的激活方式）；并重写 CanSaveAsset/CanSaveAssetAs 恒 false 兜底（含 Ctrl+S 快捷键）；弃坑：FAssetEditorCommonCommands 头文件在 UnrealEd 私有目录不可引用，命令名按 UI_COMMAND 注册名硬编码（"SaveAsset"/"SaveAssetAs"）；注意 5.8 的 FToolMenuProfile 在 Runtime/Slate 的 ToolMenuBase.h，只有 MenuPermissions，Profile 按菜单名逐菜单注册 |
