# assets/icons —— 应用图标（母版与派生）

本目录是应用图标的**派生工具与产物**：脚本写出的每个文件都在本目录内（三套母版也在），`assets/` 根只留唯一入库的矢量母版 `../icon.svg` 与示例输入 `../sample.png`。母版 `../icon.svg` 为 VTracer 矢量，背景填实 `#b9c495`，背景矩形带矢量圆角 `rx=ry=128`（即画布的 12.5%）；`regenerate.sh` 先光栅出**圆角母版** `icon_round.png`（四角透明、带 alpha），再把它压平到背景色得**直角满幅母版** `icon.png`（无 alpha），并由同一 svg 派生 **squircle 母版** `icon_round_mac.png`（见「macOS 轮廓用 squircle」）；全部 png/ico/icns 与派生 svg 由这三套母版动态生成，**派生资产不入库**（已 gitignore）。**唯一例外** `icon-readme.png`（128px，README 展示用——GitHub 不渲染 SVG；随脚本自动刷新、入库）。

> ⚠️ **新克隆不跑脚本直接构建会报错**：`app_icon.rc`（Windows）、`resources.qrc`（窗口图标）、Android 模板（`res/mipmap-*`）在构建期引用这些派生物，缺文件即编译失败。**任何本地构建（含 CLion）前必须先跑一次** `regenerate.sh`；改 `icon.svg` 后同样要重跑。

## 本地构建

```bash
bash assets/icons/regenerate.sh
```

生成物已 gitignore，status 恒干净。改图标 = 改 svg 后本地重跑一遍（供本地构建 / 目检）+ 提交 svg；CI 会自动生成全平台产物。

脚本跨平台（Git Bash / Linux / macOS / CI ubuntu runner），依赖三种工具（可用环境变量 `INKSCAPE` / `FFMPEG` / `PYTHON` 覆盖默认路径）：

- 光栅化：Inkscape 或 rsvg-convert（rsvg 仅本地兜底——CI 用钉版 Inkscape，见「CI 动态构建」）；
- 尺寸 / 打包：ffmpeg（lanczos 缩放；多流 mux 直接出 .ico）；
- macOS squircle 母版：python3 以 **XML 解析**把背景 `rect` 就地改写为角部超椭圆 `path`（命中数必为 1，否则报错）；
- Android 自适应前景：python3 以 **XML 解析**删除 `fill` 等于背景色（大小写不敏感）的 `rect`/`path`，并断言恰好删掉 1 个——命中数不为 1 即报错退出，不会静默把背景留在前景层里（确定性 100%、零阈值）。副作用：ElementTree 重写输出会丢弃注释与 DOCTYPE，母版当前无二者，日后若加入需知情。

## 派生流程

`regenerate.sh` 依序产出（幂等）：svg → 光栅 1024 圆角母版 `icon_round.png` → 压平到 `#b9c495` 得直角满幅母版 `icon.png`（脚本内校验**无 alpha**，iOS App Store 硬性要求）→ ①-b 由同一 svg 派生 **macOS 专用 squircle 母版** `icon_mac.svg` → `icon_round_mac.png`（背景 rect 改写为角部超椭圆，n=3、等效 rx=184）→ 按平台从对应母版派生各尺寸（外壳不套圆角的 Windows / Linux / Android legacy / README 小图走圆角母版，**macOS 走 squircle 母版并另按 Apple 模板留 20% 透明边**；被遮罩的 iOS / Play 512 / StoreLogo 走直角母版）→ Windows `idc.ico`（9 尺寸 BMP/DIB 条目：显式 `-c:v bmp`——ico muxer 的默认编码器随版本而变，且该选项必须放在输入之后、输出之前，否则会被当成输入解码器；打包后由 python3 解析 ICO 目录项自检——逐条读图像头判定“非 PNG 签名 + DIB 头长度合法 + 32bpp”，不 grep ffmpeg 探测文本、也不引入 ffprobe 依赖）→ Linux scalable（svg 复制，圆角原生自带）→ Android 自适应前后景（删除背景元素后的前景 SVG 光栅 432 + 纯色背景 `#b9c495`）→ Android 模板 `res/mipmap-*` 密度系列（Qt 打包源目录，步骤 ⑥）。`mac/idc.icns` 需在 macOS 上由 `make_icns.sh`（依赖系统 `iconutil`）另行打包。

## 目录与内容

| 目录         | 平台      | 内容                                                                                                                                                                                                                                   |
|------------|---------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `./`       | 母版      | `icon.png`（直角满幅、无 alpha）、`icon_round.png`（圆角带 alpha）、`icon_mac.svg` + `icon_round_mac.png`（macOS squircle 母版与其派生 svg）均**不入库**；`icon-readme.png`（128px README 展示图）**入库**——本目录唯一的派生入库件                                                 |
| `windows/` | Windows | `idc.ico`（16/20/24/32/40/48/64/128/256，ffmpeg 多流 mux；条目为经典 BMP/DIB——兼容性最强，PNG 条目仅是 Vista+ 扩展故不求）+ 各尺寸 png（**圆角母版**）+ `StoreLogo-50.png`（**仅 MSIX/Store 提交资产**，进 appxmanifest `VisualElements/StoreLogo`，不进 .ico；磁贴由外壳套圆角，故取**直角母版**） |
| `mac/`     | macOS   | `icon_NxN.png`（16/32/64/128/256/512/1024，iconset 命名，**squircle 母版 + 20% 透明边**——Dock / Finder 无系统遮罩，画的就是 icns 像素本身，轮廓靠母版自带）；`idc.icns` 由 `make_icns.sh` 动态生成                                                                          |
| `android/` | Android | `icon-512.png`（Play 商店列表图标，**直角母版**——商店自套遮罩）+ `ic_foreground/ic_background.png`（432，自适应前后景层，由 SVG 路径剥离生成）；启动器图标另见 `ImageDiscropperCpp/packaging/android/res/mipmap-*`（脚本步骤 ⑥ 派生，legacy 层取**圆角母版**）                                   |
| `ios/`     | iOS     | `icon-20/29/40/80/120/152/167/180/1024.png`（Xcode 资产目录，**直角母版**且无 alpha；系统套 superellipse 遮罩；现代 Xcode 单尺寸 1024 可自动派生全部，其余为保险）                                                                                                         |
| `linux/`   | Linux   | `hicolor/NxN/apps/imagediscropper.png`（16/22/24/32/48/64/128/256/512，**圆角母版**）+ `scalable/apps/imagediscropper.svg`（圆角随 svg 原生生效）                                                                                                    |

## 工程接入点

- **Windows**：`ImageDiscropperCpp/packaging/app_icon.rc` 引用本目录 `idc.ico`，GUI CMakeLists `if(WIN32) target_sources(idc_gui PRIVATE ../packaging/app_icon.rc)` 嵌入 exe；安装器 `packaging/installer.iss` 经 exe 图标自动生效。
- **macOS**：GUI CMakeLists 检测 `mac/idc.icns` 存在即注入 bundle Resources（`MACOSX_BUNDLE_ICON_FILE`）；icns 由 CI macos 腿调用 `make_icns.sh` 生成，本地 macOS 亦可直接 `bash make_icns.sh`。squircle 轮廓与透明边已在 `mac/icon_NxN.png` 里烘进去，`iconutil` 不再加工形状。
- **Android**：`regenerate.sh` 步骤 ⑥ 刷新 `ImageDiscropperCpp/packaging/android/res/mipmap-*` 密度系列（legacy 48/72/96/144/192 + foreground 108/162/216/324/432，逐档写死而非等比递推）；`colors.xml` / `ic_launcher.xml` 为手工文件**仍入库**、不属派生；自适应图标走 `mipmap-anydpi-v26/ic_launcher.xml`（前景 `ic_foreground.png` + 背景色 `ic_launcher_background` `#b9c495`）。`AndroidManifest.xml` 只引用 `@mipmap/ic_launcher`，**无 `android:roundIcon`**，圆形启动器由系统自套遮罩，故不派生 `ic_launcher_round`（若日后引入 `roundIcon`，需同步补上该档产物）。
- **iOS**：Xcode 资产目录 AppIcon 引用 `ios/icon-*.png`（CI macos 流程）。
- **Linux**：桌面入口 `packaging/imagediscropper.desktop`（`Icon=imagediscropper`）；打包（deb / AppImage）时安装 `linux/hicolor/*/apps/imagediscropper.png` 到 `/usr/share/icons/hicolor/<N>x<N>/apps/`（scalable 用 svg），`.desktop` 到 `/usr/share/applications/`，装后刷新缓存（`gtk-update-icon-cache`）。窗口 / 任务栏图标另经 qrc 内嵌（`packaging/resources.qrc` → `icon-256.png`，`setWindowIcon`）。

## CI 动态构建

图标在 CI / CD / verify 每次**动态构建、派生物不入库**，各构建腿图标的唯一来源即流转产物。完整流程与缓存策略以 [`.github/workflows/icons-ci.yml`](../../.github/workflows/icons-ci.yml) 为准，要点：

- `icons-ci.yml` 是**唯一生产者**，由 `ci.yml`（每次 CI）、`cd.yml`（每次发布）、`cpp-verify.yml`（手动验证）无条件调用；按 `hash(icon.svg, regenerate.sh)` 命中产物缓存则跳过重跑，miss 则现场跑 `regenerate.sh` 全流程并上传 artifact `icon-products`（本目录的母版 png/svg + 各平台子目录 + android res 密度系列）。
- 工具链**钉版、按需下载**（Inkscape 1.4.3 官方 AppImage + BtbN ffmpeg 静态构建）——Inkscape 抗锯齿像素级敏感，**本地须同为 Inkscape 1.4.3**，否则与缓存产物微差；runner 钉 `ubuntu-24.04`、本 job 零 apt 安装。
- 消费端各构建腿（`cpp-build.yml` 的 Download generated icons 步骤，早于 configure）下载 `icon-products` 落地；腿经 `needs: icons` 等产物，产物缺失即报错（无提交版可回退）。

## 尺寸取舍依据（常见疑问）

- **Windows 20/40/24/48/64**：16px 基准的等比缩放档（×1.25 / 1.5 / 2 / 2.5 / 3 / 4）——125%–400% 缩放率下整数匹配、免重采样发糊，值得放进 .ico；
- **Windows 50**：仅 Microsoft Store / MSIX 的 `StoreLogo` 提交资产，与运行时图标无关；
- **Linux 22/24**：22 = KDE 工具栏 / 面板约定，24 = GNOME 菜单 / 工具栏约定（hicolor 规范两类）；有 scalable svg 后二者均非必需；
- **iOS 小尺寸**：现代 Xcode 资产目录用单尺寸 1024 自动派生全部，20/29/40/120/152/167 留作无资产目录流程的保险；此处为**非完整 Asset Catalog 集**（缺 58/60/76/87 等 @2x/@3x 档）。`icon-1024.png` 与母版同尺寸，lanczos 1024→1024 是恒等重编码，故直接 `cp` 母版；
- **Android 512**：Play 商店列表图标；自适应前后景层为 432（= 108dp@4x，见步骤 ⑤）。
- **自适应前景**：从 svg 删除 `fill` 等于 `#b9c495` 的背景元素后光栅化——本设计内容触边、满幅 1:1 铺 432 即可（无安全区缩放）；若未来设计留白充足需安全区（66/108 中心），在 `regenerate.sh` ⑤ 的光栅尺寸处改缩 264 并居中。
- **圆角半径 128/1024（12.5%）**：256px 桌面档能明确读作圆角卡片，16px 档仍留有约 2px 干净倒角（lanczos 采样后不糊成一团）；半径再往上（Apple 遮罩用的 ≈22%）会把四角绿色象限缩成花瓣、削弱“网格”大形，而该比例在 iOS 上本就看不见（系统遮罩更大）。
- **macOS 轮廓用 squircle**：Apple 图标的角是连续曲率，而 `rect` 的 `rx/ry` 是纯圆弧（直边与圆角接缝处曲率突变为 0）。故 mac 档单独用角部超椭圆母版：`P(θ) = C + R·(sgn(cosθ)|cosθ|^m, sgn(sinθ)|sinθ|^m)`，`m=2/n`，n=2 时精确退化为圆形 rx（实测与 `rx=184` 的光栅仅 950/1048576 像素有差，全在抗锯齿过渡带上）。采样按**逐角**组织（每角 128 区间、含两端切点，共 516 点）而非整圈等分 θ，使 8 个切点精确命中、直边为真直线；折线相对解析边界的最大偏离 0.0051px。取 **n=3** 并按“45° 内缩量与圆形 rx=184 相同”反解得 R=261.2（过渡延展 1.42× 圆弧、两切点间直段 501.5px 占整条边 49%）：n=2.5 偏保守（1.21×）、n≥4 角部开始塌肩；整幅超椭圆（不加直边）则“抱枕化”，已弃用。其他平台仍用 rx=128 圆角母版（Windows / Linux 外壳自有风格约定，iOS 遮罩更大）。
- **macOS 留边**：Apple 图标模板把内容放在画布中央约 80%，四周留透明边，因为系统不加工形状——不留边则 Dock 里比邻接应用大一圈。内容边长四舍五入到偶数（`(size*4/5+1)/2*2`），保证 `(size-content)/2` 为整数、四边对称；实测各档比例 0.75–0.81（1024→820、512→410、256→204、128→102、64→52、32→26、16→12，边距 102/51/26/13/6/3/2，squircle 边中点触及内容框故包围盒仍为完整正方形）。
- **Android 密度不是等比**：mdpi→hdpi→xhdpi→xxhdpi→xxxhdpi 的乘数依次为 1.5 / 1.3 / 1.5 / 1.3，所以步骤 ⑥ 逐档写死 legacy `48/72/96/144/192` 与 foreground `108/162/216/324/432`（= 108/162/216/324/432dp 的 @1x..@4x 序列）；用连续 `*3/2` 递推会得到 48/72/108/162/243 与 108/162/243/364/546，从 xhdpi 起逐档偏大（xxxhdpi 多 26%）。

## 设计纪律（与母版相关）

- **背景色 `#b9c495` 不得出现在图形内部**——前景层按 `fill` 等于该色删除背景元素，图形内同色会被误删（脚本断言命中数为 1，多删即报错）；
- **圆角只在母版 svg 上做一次**（背景矩形的 `rx=ry=128`），派生层不再补任何光栅掩膜：抗锯齿边由 Inkscape 矢量光栅给出，逐像素掩膜只会得到无锯齿的硬角；
- **三套母版各取所需**：外壳不套圆角的（Windows 运行时图标 / Linux hicolor / Android legacy / README 小图）取圆角母版，**macOS（另加 20% 透明边）取 squircle 母版**，会被遮罩的（iOS / Play 512 / StoreLogo）取直角母版——遮罩半径大于 12.5%，圆角被完整覆盖，套上去只会双重圆角；
- 背景矩形必须**填实**（无留白、无透明底）：圆角切掉的四角靠压平步骤填回 `#b9c495`，填不实则直角母版露透，脚本 alpha 校验即报错；
- 小尺寸（16/24）需单独目检：只保留大形外轮廓；
- 前景图形对比度足够（暗底用亮色），避免深色任务栏 / 深色壁纸上"隐形"。
