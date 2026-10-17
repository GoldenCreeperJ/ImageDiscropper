# assets/icons —— 应用图标（母版与派生）

本目录是应用图标的**派生工具与产物**。母版为仓库根 `assets/icon.svg`（VTracer 矢量，背景填实 `#b9c495`、满幅构图）；全部 png/ico/icns 与派生 svg 由 `regenerate.sh` 从母版动态生成，**派生资产不入库**（已 gitignore）。**唯一例外** `../icon-readme.png`（128px，README 展示用——GitHub 不渲染 SVG；随脚本自动刷新、入库）。

> ⚠️ **新克隆不跑脚本直接构建会报错**：`app_icon.rc`（Windows）、`resources.qrc`（窗口图标）、Android 模板（`res/mipmap-*`）在构建期引用这些派生物，缺文件即编译失败。**任何本地构建（含 CLion）前必须先跑一次** `regenerate.sh`；改 `icon.svg` 后同样要重跑。

## 本地构建

```bash
bash assets/icons/regenerate.sh
```

生成物已 gitignore，status 恒干净。改图标 = 改 svg 后本地重跑一遍（供本地构建 / 目检）+ 提交 svg；CI 会自动生成全平台产物。

脚本跨平台（Git Bash / Linux / macOS / CI ubuntu runner），依赖三种工具（可用环境变量 `INKSCAPE` / `FFMPEG` / `PYTHON` 覆盖默认路径）：

- 光栅化：Inkscape 或 rsvg-convert（rsvg 仅本地兜底——CI 用钉版 Inkscape，见「CI 动态构建」）；
- 尺寸 / 打包：ffmpeg（lanczos 缩放；多流 mux 直接出 .ico）；
- Android 自适应前景：python3 从 SVG 剥离背景路径（`fill="#b9c495"`，确定性 100%、零阈值）。

## 派生流程

`regenerate.sh` 依序产出（幂等）：svg → 光栅 1024 母版 `assets/icon.png`（脚本内校验**无 alpha**，iOS App Store 硬性要求）→ 各平台尺寸 → Windows `idc.ico`（9 尺寸 BMP/DIB 条目）→ Linux scalable（svg 复制）→ Android 自适应前后景（前景 SVG 光栅 432 + 纯色背景 `#b9c495`）→ Android 模板 `res/mipmap-*` 密度系列（Qt 打包源目录，步骤 ⑥）。`mac/idc.icns` 需在 macOS 上由 `make_icns.sh`（依赖系统 `iconutil`）另行打包。

## 目录与内容（均为派生物，不入库）

| 目录         | 平台      | 内容                                                                                                                                                                                                      |
|------------|---------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `windows/` | Windows | `idc.ico`（16/20/24/32/40/48/64/128/256，ffmpeg 多流 mux；条目为经典 BMP/DIB——兼容性最强，PNG 条目仅是 Vista+ 扩展故不求）+ 各尺寸 png + `StoreLogo-50.png`（**仅 MSIX/Store 提交资产**，进 appxmanifest `VisualElements/StoreLogo`，不进 .ico） |
| `mac/`     | macOS   | `icon_NxN.png`（16/32/64/128/256/512/1024，iconset 命名）；`idc.icns` 由 `make_icns.sh` 动态生成                                                                                                                   |
| `android/` | Android | `icon-512.png`（Play 商店列表图标）+ `ic_foreground/ic_background.png`（432，自适应前后景层，由 SVG 路径剥离生成）；启动器图标另见 `ImageDiscropperCpp/packaging/android/res/mipmap-*`（脚本步骤 ⑥ 派生）                                         |
| `ios/`     | iOS     | `icon-20/29/40/80/120/152/167/180/1024.png`（Xcode 资产目录；现代 Xcode 单尺寸 1024 可自动派生全部，其余为保险）                                                                                                                 |
| `linux/`   | Linux   | `hicolor/NxN/apps/imagediscropper.png`（16/22/24/32/48/64/128/256/512）+ `scalable/apps/imagediscropper.svg`                                                                                              |

## 工程接入点

- **Windows**：`ImageDiscropperCpp/packaging/app_icon.rc` 引用本目录 `idc.ico`，GUI CMakeLists `if(WIN32) target_sources(idc_gui PRIVATE ../packaging/app_icon.rc)` 嵌入 exe；安装器 `packaging/installer.iss` 经 exe 图标自动生效。
- **macOS**：GUI CMakeLists 检测 `mac/idc.icns` 存在即注入 bundle Resources（`MACOSX_BUNDLE_ICON_FILE`）；icns 由 CI macos 腿调用 `make_icns.sh` 生成，本地 macOS 亦可直接 `bash make_icns.sh`。
- **Android**：`regenerate.sh` 步骤 ⑥ 刷新 `ImageDiscropperCpp/packaging/android/res/mipmap-*` 密度系列（legacy 48..192 + foreground 108..432）；`colors.xml` / `ic_launcher.xml` 为手工文件**仍入库**、不属派生；自适应图标走 `mipmap-anydpi-v26/ic_launcher.xml`（前景 `ic_foreground.png` + 背景色 `ic_launcher_background` `#b9c495`）。
- **iOS**：Xcode 资产目录 AppIcon 引用 `ios/icon-*.png`（CI macos 流程）。
- **Linux**：桌面入口 `packaging/imagediscropper.desktop`（`Icon=imagediscropper`）；打包（deb / AppImage）时安装 `linux/hicolor/*/apps/imagediscropper.png` 到 `/usr/share/icons/hicolor/<N>x<N>/apps/`（scalable 用 svg），`.desktop` 到 `/usr/share/applications/`，装后刷新缓存（`gtk-update-icon-cache`）。窗口 / 任务栏图标另经 qrc 内嵌（`packaging/resources.qrc` → `icon-256.png`，`setWindowIcon`）。

## CI 动态构建

图标在 CI / CD / verify 每次**动态构建、派生物不入库**，各构建腿图标的唯一来源即流转产物。完整流程与缓存策略以 [`.github/workflows/icons-ci.yml`](../../.github/workflows/icons-ci.yml) 为准，要点：

- `icons-ci.yml` 是**唯一生产者**，由 `ci.yml`（每次 CI）、`cd.yml`（每次发布）、`cpp-verify.yml`（手动验证）无条件调用；按 `hash(icon.svg, regenerate.sh)` 命中产物缓存则跳过重跑，miss 则现场跑 `regenerate.sh` 全流程并上传 artifact `icon-products`（`assets/**` + android res 密度系列）。
- 工具链**钉版、按需下载**（Inkscape 1.4.3 官方 AppImage + BtbN ffmpeg 静态构建）——Inkscape 抗锯齿像素级敏感，**本地须同为 Inkscape 1.4.3**，否则与缓存产物微差；runner 钉 `ubuntu-24.04`、本 job 零 apt 安装。
- 消费端各构建腿（`cpp-build.yml` 的 Download generated icons 步骤，早于 configure）下载 `icon-products` 落地；腿经 `needs: icons` 等产物，产物缺失即报错（无提交版可回退）。

## 尺寸取舍依据（常见疑问）

- **Windows 20/40/24/48/64**：16px 基准的等比缩放档（×1.25 / 1.5 / 2 / 2.5 / 3 / 4）——125%–400% 缩放率下整数匹配、免重采样发糊，值得放进 .ico；
- **Windows 50**：仅 Microsoft Store / MSIX 的 `StoreLogo` 提交资产，与运行时图标无关；
- **Linux 22/24**：22 = KDE 工具栏 / 面板约定，24 = GNOME 菜单 / 工具栏约定（hicolor 规范两类）；有 scalable svg 后二者均非必需；
- **iOS 小尺寸**：现代 Xcode 资产目录用单尺寸 1024 自动派生全部，20/29/40/120/152/167 留作无资产目录流程的保险；
- **Android 512**：Play 商店列表图标；自适应前后景层为 432（= 108dp@4x，见步骤 ⑤）。
- **自适应前景**：从 svg 剥离 `fill="#b9c495"` 背景路径后光栅化——本设计内容触边、满幅 1:1 铺 432 即可（无安全区缩放）；若未来设计留白充足需安全区（66/108 中心），在 `regenerate.sh` ⑤ 的光栅尺寸处改缩 264 并居中。

## 设计纪律（与母版相关）

- **背景色 `#b9c495` 不得出现在图形内部**——路径剥离按该填充色精确删除背景路径，图形内同色会被误删；
- 母版画满整个 1024 方块，**不预圆角**（macOS / iOS 系统自动套圆角）；
- 小尺寸（16/24）需单独目检：只保留大形外轮廓；
- 前景图形对比度足够（暗底用亮色），避免深色任务栏 / 深色壁纸上"隐形"。
