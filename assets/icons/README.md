# assets/icons —— 应用图标（母版与派生）

**母版**：仓库根 `assets/icon.svg`（VTracer 矢量，背景填实 `#b9c495`、满幅构图）。
**派生资产不入库**（已 gitignore）：全部 png/ico/icns 与派生 svg 由母版动态生成——
**唯一例外** `../icon-readme.png`（128px，README 展示用——GitHub 不渲染 SVG；随脚本自动刷新、入库）：

> ⚠️ **新克隆不跑脚本直接构建会报错**：`app_icon.rc`（Windows）、`resources.qrc`
> （窗口图标）、Android 模板（`res/mipmap-*`）在构建期引用这些派生物，缺文件即编译
> 失败。**任何本地构建（含 CLion）前必须先跑一次**下方脚本；改 `icon.svg` 后同样要重跑。

- **CI/CD/verify**：icons job 每次完整构建，经 artifact `icon-products` 流转给构建腿
  （构建腿图标的唯一来源）；
- **本地**：首次构建前跑一次

  ```bash
  bash assets/icons/regenerate.sh
  ```

  生成物已 gitignore，status 恒干净。改图标 = 改 svg 后本地重跑一遍（供本地构建/目检）
  + 提交 svg；CI 自动生成全平台产物。

脚本跨平台（Git Bash / Linux / macOS / CI ubuntu runner），依赖三种工具（可用环境变量
`INKSCAPE` / `FFMPEG` / `PYTHON` 覆盖默认路径）：

- 光栅化：Inkscape 或 rsvg-convert（rsvg 仅本地兜底——CI 用钉版 Inkscape，见
  「CI 动态构建」）；
- 尺寸/打包：ffmpeg（lanczos 缩放；多流 mux 直接出 .ico）；
- Android 自适应前景：python3 从 SVG **剥离背景路径**（`fill="#b9c495"`，确定性 100%，
  零阈值、无 chromakey 误伤）。

流程：svg → 光栅 1024 母版 `assets/icon.png`（脚本内校验**无 alpha**——iOS App Store
硬性要求）→ 各平台尺寸 → Windows .ico → Linux scalable（svg 复制）→ Android 自适应
前后景（前景 SVG 光栅 432 + 纯色背景 `#b9c495`）→ Android 模板密度系列
（`ImageDiscropperCpp/packaging/android/res/mipmap-*`，步骤 ⑥）。已弃用 PowerShell（旧
`make_adaptive.ps1` 色键方案已删除）。

## 目录与内容（均为派生物，不入库）

| 目录         | 平台      | 内容                                                                                                                                                                                                                          |
|------------|---------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `windows/` | Windows | `idc.ico`（16/20/24/32/40/48/64/128/256，ffmpeg 多流 mux；条目为经典 BMP/DIB——兼容性最强，PNG 条目仅是 Vista+ 扩展故不求）+ 各尺寸 png + `StoreLogo-50.png`（**仅 MSIX/Store 提交资产**，进 appxmanifest `VisualElements/StoreLogo`，不进 .ico）                     |
| `mac/`     | macOS   | `icon_NxN.png`（16/32/64/128/256/512/1024，iconset 命名）；`idc.icns` 由 `make_icns.sh` 动态生成（见下）                                                                                                                                   |
| `android/` | Android | `mipmap-48/72/96/144/192.png`（旧版启动器）+ `icon-432.png`（占位）/`icon-512.png`（Play 商店）+ `ic_foreground/ic_background.png`（432，自适应前后景层，由 SVG 路径剥离生成）；另脚本步骤 ⑥ 派生 `ImageDiscropperCpp/packaging/android/res/mipmap-*` 密度系列（Qt 打包源目录） |
| `ios/`     | iOS     | `icon-20/29/40/80/120/152/167/180/1024.png`（Xcode 资产目录；现代 Xcode 单尺寸 1024 可自动派生全部，其余为保险）                                                                                                                                     |
| `linux/`   | Linux   | `hicolor/NxN/apps/imagediscropper.png`（16/22/24/32/48/64/128/256/512）+ `scalable/apps/imagediscropper.svg`                                                                                                                  |

## 工程接入点

- **Windows**：`ImageDiscropperCpp/packaging/app_icon.rc` 引用本目录 `idc.ico`，
  GUI CMakeLists `if(WIN32) target_sources(idc_gui PRIVATE ../packaging/app_icon.rc)` 嵌入 exe；
  安装器 `packaging/installer.iss` 经 exe 图标自动生效。
- **macOS**：GUI CMakeLists 检测 `mac/idc.icns` 存在即注入 bundle Resources
  （`MACOSX_BUNDLE_ICON_FILE`）；icns 由 CI macos 腿 `make_icns.sh` 动态生成
  （见 `.github/workflows/cpp-build.yml`），本地 macOS 亦可直接 `bash make_icns.sh`。
- **Android**：仓库维护模板目录 `ImageDiscropperCpp/packaging/android/`——`res/mipmap-*` 的
  密度系列 png 由 `regenerate.sh` 步骤 ⑥ 派生刷新（legacy 48..192 + foreground
  108..432；`colors.xml`/`ic_launcher.xml` 为手工文件**仍入库**，不属派生）；自适应
  图标走 `mipmap-anydpi-v26/ic_launcher.xml`（前景 `ic_foreground.png` + 背景色
  `ic_launcher_background` `#b9c495`）。
- **iOS**：Xcode 资产目录 AppIcon 引用 `ios/icon-*.png`（CI macos 流程）。
- **Linux**：桌面入口 `packaging/imagediscropper.desktop`（`Icon=imagediscropper`）；
  打包（deb/AppImage）时安装 `linux/hicolor/*/apps/imagediscropper.png` 到
  `/usr/share/icons/hicolor/<N>x<N>/apps/`（scalable 用 svg），`.desktop` 到
  `/usr/share/applications/`，装后刷新缓存（`gtk-update-icon-cache`）。
  窗口/任务栏图标另经 qrc 内嵌（`packaging/resources.qrc` → `icon-256.png`，`setWindowIcon`）。
- **本地构建前提**：rc/qrc/Android 模板引用的图标文件均为派生物——新克隆后先跑一次
  `regenerate.sh`（Inkscape 版本要求见下），CI 腿则由 icon-products 产物供给。

## CI 动态构建

图标**每次 CI/CD/verify 动态构建（按输入缓存），产物像构建产物一样在管线里流转**：

- `.github/workflows/icons-ci.yml`（**唯一生产者**）：ci.yml 每次 CI 运行、cd.yml 每次
  发布、cpp-verify.yml 手动验证均调用。流程：快照提交版 → **产物缓存**（key =
  `hash(svg, 脚本)`）命中则恢复上次构建、跳过重跑；miss 则 checkout **原地**跑
  `regenerate.sh` 全流程 → **闸门** → 通过则上传 artifact `icon-products`
  （assets/** + android res 密度系列）。
- **闸门（每轮执行）**：
  - 反向完好性：提交版文件（母版 svg、脚本、手工 xml）须仍在工作区且未被脚本改写
    ——防脚本误伤手工资产；
  - 生成物不变量：派生物没有提交参照，像素回归靠不变量兜底（前景中心有内容/四角
    透明、背景纯色精确 `#b9c495`、ico 九条目）。缓存产物 ≡ 新鲜构建（脚本已验证
    确定性）。
- **消费端**：各构建腿（cpp-build.yml 的 Download generated icons 步骤，早于 configure）
  下载 `icon-products` 落地——派生物不入库，这是构建腿图标的**唯一来源**；腿经
  `needs: icons` 等产物，闸门失败则 icons 红、下游腿整体跳过（CI 红/发布中止）。
  产物缺失即报错（没有提交版可回退）。
- **缓存三层**：
  - 产物缓存（~1MB）：key = `hash(svg, 脚本)`，命中免构建（job ~3 分钟 → ~1 分钟）；
    **不用 restore-keys**（前缀回退取旧 svg 产物）。key 不含工具链版本——升级
    Inkscape 后若闸门误红，清一次产物缓存即自愈。缓存路径只覆盖派生子目录
    （整目录缓存会连带覆盖提交版脚本/手工 xml）。
  - 工具链缓存：Inkscape 官方 AppImage **1.4.3** 与 ffmpeg **8.0.1** 静态构建
    （johnvansickle）**一同钉版缓存**（`/tmp/icontools` 单目录单 key，首次下载 ~170MB；
    每轮恢复 + 每轮版本断言）。Inkscape 抗锯齿像素级敏感（apt 1.2.2 与本地 1.4.3 必然
    微差→误报；rsvg-convert 同理不可用于比对）；ffmpeg 钉 8.0.1 与本地同版，lanczos
    缩放输出逐像素确定，版本差漂移类彻底关闭。**本地必须同为 Inkscape 1.4.3 + ffmpeg
    8.0.1**；升级本地时同步改工具链缓存 key 与下载 URL。
  - 流转层：产物经 artifact `icon-products` 给下游腿（cache 承载免重建，artifact 承载
    生产者→消费者，二者语义不同不混用）。
  - runner 钉 `ubuntu-24.04`（镜像宇宙随版本固定，防静默漂移）；本 job **零 apt 安装**。

## 尺寸取舍依据（常见疑问）

- **Windows 20/40/24/48/64**：16px 基准的等比缩放档（16×1.25=20、×1.5=24、×2=32、×2.5=40、×3=48、×4=64）——125%–400% 缩放率下整数匹配、免重采样发糊，值得放进 .ico；
- **Windows 50**：仅 Microsoft Store / MSIX 的 `StoreLogo` 提交资产，与运行时图标无关；
- **Linux 22/24**：22 = KDE 工具栏/面板约定；24 = GNOME 菜单/工具栏约定（hicolor 规范两类）；有 scalable svg 后二者均非必需；
- **iOS 小尺寸**：现代 Xcode 资产目录用单尺寸 1024 自动派生全部，20/29/40/120/152/167 留作无资产目录流程的保险；
- **Android 432/512**：432 = 自适应图标前后景层（108dp@4x）；512 = Play 商店列表图标。
- **自适应前景**：从 svg 剥离 `fill="#b9c495"` 背景路径后光栅化——本设计内容触边、满幅
  1:1 铺 432 即可（无安全区缩放）；若未来设计留白充足需安全区（66/108 中心），在
  `regenerate.sh` ⑤ 的光栅尺寸处改缩 264 并居中（同时同步 icons-ci 闸门的中心 alpha
  断言）。

## 设计纪律（与母版相关）

- **背景色 `#b9c495` 不得出现在图形内部**——路径剥离按该填充色精确删除背景路径，
  图形内同色会被误删；
- 母版画满整个 1024 方块，**不预圆角**（macOS/iOS 系统自动套圆角）；
- 小尺寸（16/24）需单独目检：只保留大形外轮廓；
- 前景图形对比度足够（暗底用亮色），避免深色任务栏/深色壁纸上"隐形"。
