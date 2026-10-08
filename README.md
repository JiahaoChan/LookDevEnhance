# LookDevEnhance

![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-313131?logo=unrealengine)
![Version](https://img.shields.io/badge/version-1.0-blue)
![Type](https://img.shields.io/badge/type-Editor%20Plugin-green)
![Localization](https://img.shields.io/badge/locale-en%20%7C%20zh--Hans-orange)
![License](https://img.shields.io/badge/license-Proprietary-lightgrey)

A dedicated LookDev asset editor for Unreal Engine — inspect static meshes, materials and more in a controlled, calibrated lighting environment, right from the Content Browser.

---

## Overview

**LookDevEnhance** adds a standalone LookDev editor (a custom `UAssetEditor`) that opens your assets inside a purpose-built preview scene: environment dome, sky light, three directional light presets, post-processing, PBR checker and calibration objects. It is designed for look-development workflows — consistent lighting, calibrated exposure, quick camera angles and clean screenshots — without polluting your level or modifying the source assets. The editor is strictly read-only: save entry points are removed by design.

## Features

- **One-click LookDev** — right-click supported assets in the Content Browser and open them in the LookDev editor; multiple assets are laid out side by side.
- **Calibrated preview scene** — environment dome, sky light (specified cubemap with per-frame capture update), three-stop directional lighting, post-process volume, PBR checker and color calibrator props, configurable via Project Settings.
- **PBR calibration workflow** — built-in color checker, gray/chrome calibration balls and Lumen visualization modes in the viewport toolbar.
- **Turntable lighting** — rotate the lighting rig around the asset with absolute-angle control for stable, repeatable comparisons.
- **Multi-view copies** — generate a ring of turntable copies (8 views, 45° steps) for silhouette and angle review in a single frame.
- **Screenshot export** — toolbar dropdown with 1K / 2K / 4K presets and an optional watermark.
- **Camera bookmarks** — save and restore camera positions per asset (position / rotation / FOV with thumbnail previews) in a drawer panel.
- **Familiar viewport UX** — preview-scoped selection & gizmo, `F` to focus, view-mode and Lumen visualization switching, one-click camera/orientation reset.
- **Localized UI** — ships with English (native) and Simplified Chinese (`zh-Hans`) translations through the plugin localization pipeline.
- **Non-destructive by design** — editor tabs are color-coded green, de-registered from the native asset editor deduplication registry, and Save / Save As are fully disabled.

## Installation

1. Clone or copy this repository into your project's `Plugins/` folder:
   ```
   <YourProject>/Plugins/LookDevEnhance/
   ```
2. Regenerate project files and build, or let the editor compile the plugin on startup.
3. Enable **LookDevEnhance** in *Edit → Plugins* and restart the editor.

## Usage

- Select one or more supported assets (static meshes, materials, …) in the Content Browser, right-click and choose the LookDev entry to open the editor.
- Configure the preview scene, openable asset types and material preview mesh in **Project Settings → Plugins → LookDevEnhance**.
- See `Doc/LookDev编辑器使用指南.md` (Chinese) for the full user guide.

## Compatibility

| Engine Version | Status |
|----------------|--------|
| UE 5.8         | ✅ Tested |

Editor-only plugin; no runtime module, no impact on packaged builds.

## License & Copyright

Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

---

## 中文简介

**LookDevEnhance** 为静态网格、材质等资产提供独立的 LookDev 资产编辑器：右键菜单一键打开受控光照预览场景（天穹 / 天光 / 三档方向光 / 后处理 / PBR 校准物），内置 PBR 校准、转台灯光、多视图副本、截图导出（1K/2K/4K + 水印）、相机书签；编辑器只读（移除保存入口），支持 en / zh-Hans 本地化。纯编辑器插件，对打包构建零影响。
