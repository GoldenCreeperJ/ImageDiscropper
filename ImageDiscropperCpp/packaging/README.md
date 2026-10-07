# packaging/

## 职责边界

本目录存放**发布打包与品牌资产**：各平台的图标接线（Windows 资源脚本 / Qt 资源 /
Android 包源目录）、Windows 安装包脚本与 Linux 桌面入口。
打包逻辑在发布工作流（`.github/workflows/cpp-build.yml` 的产物整理步骤）中执行，
本目录只提供静态资产。

## 文件清单

| 文件 / 目录                           | 作用                                                                                                                                                                                                                       |
|-----------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `installer.iss`                   | Inno Setup 6 安装包脚本：安装 `idc_gui.exe` + `idc.exe`（全局/当前用户可选、目录可自定义、简中/英文双语向导），创建开始菜单/桌面快捷方式与卸载器，可选开机自启动（HKCU Run 键）                                                                                                        |
| `languages/ChineseSimplified.isl` | 仓库自带的 6.7.1 兼容中文语言文件（官方中文仅存在于 issrc main 分支、未随任何发布版分发，故自带以摆脱构建机 Inno 安装内容依赖；由官方翻译按键表过滤生成）                                                                                                                                |
| `app_icon.rc`                     | Windows 资源脚本：把多尺寸 `idc.ico`（16/20/24/32/40/48/64/128/256，BMP/DIB 条目）嵌入 `idc_gui.exe`，供任务栏/资源管理器/安装器显示；由 GUI CMakeLists `target_sources(idc_gui PRIVATE ../packaging/app_icon.rc)` 编译；相对路径以本文件所在目录为基准（`../../assets/...`） |
| `resources.qrc`                   | Qt 资源：内嵌 256px 应用图标（`/icons/app_icon.png`），`main.cpp` 经 `setWindowIcon` 挂到 QApplication——窗口/任务栏图标，Linux 裸二进制分发的主要载体；图标派生物不入库，文件由 `assets/icons/regenerate.sh` 生成                                                         |
| `android/`                        | Qt Android 包源目录（`QT_ANDROID_PACKAGE_SOURCE_DIR`）：`AndroidManifest.xml` + `res/` 启动器图标（`mipmap-*` 密度 png 为派生物不入库，由 `regenerate.sh` 步骤 ⑥ 生成；`colors.xml` / `mipmap-anydpi-v26/ic_launcher.xml` 为手工自适应图标配置，入库）              |
| `imagediscropper.desktop`         | Linux 桌面入口（`Icon=imagediscropper`，hicolor 主题图标另见 `assets/icons/linux/`）                                                                                                                                                  |

## 打包约定

- **产物命名**：`<产品名>-<平台>[-arm64]-<发布标签>`，GUI 用产品全名（`ImageDiscropper-...`）、
  CLI 保持短名（`idc-...`），安装包为 `ImageDiscropper-setup-win-x64-<tag>.exe`。
- **Windows**：CD 的 Windows 腿安装 Inno Setup（choco）后执行 `ISCC.exe`（以 `/DSRC=`
  指定 exe 源目录），安装包与裸可执行文件一并发布（见根 README「发布」）。
- **macOS**：idc_gui 以 `.app` 捆绑包构建（`ImageDiscropperGui/CMakeLists.txt` 的
  `MACOSX_BUNDLE` 属性），CD 以 `hdiutil` 打成 dmg 分发（含 Applications 符号链接，
  拖拽安装惯例）；应用图标经 `assets/icons/make_icns.sh` 动态生成 icns 并注入
  bundle Resources（详见 `assets/icons/README.md`）。
- **版本同步**：`installer.iss` 的 `AppVersion` 与根 CMakeLists 的 `project()` 同步（发版时改）。
- **签名**：Windows/macOS 产物未做代码签名（Windows SmartScreen 会提示、macOS
  Gatekeeper 需右键打开；mac 产物带 ad-hoc 签名满足 arm64 内核要求）；Android 由 CD
  用仓库 secrets 的长期 keystore 经 `apksigner` 签名（约定：别名 `key1`、store/key
  密码相同，secrets 为 `IDC_ANDROID_KEYSTORE_B64` + `IDC_ANDROID_KEYSTORE_PASS`；
  密钥文件须本地备份——丢失即无法覆盖升级）；
  iOS 签名需 Apple Developer 账号资产，暂缓。
