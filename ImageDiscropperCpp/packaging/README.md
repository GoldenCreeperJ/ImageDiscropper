# packaging/

## 职责边界

本目录存放**发布打包资产**：Windows 安装包脚本与将来可能的图标、部署清单等。
打包逻辑在 CD 工作流（`.github/workflows/_build-platform.yml` 的产物整理步骤）中执行，
本目录只提供静态资产。

## 文件清单

| 文件              | 作用                                                                                                                |
|-----------------|-------------------------------------------------------------------------------------------------------------------|
| `installer.iss` | Inno Setup 6 安装包脚本：安装 `idc_gui.exe` + `idc.exe`（全局/当前用户可选、目录可自定义、简中/英文双语向导），创建开始菜单/桌面快捷方式与卸载器，可选开机自启动（HKCU Run 键） |

## 打包约定

- **Windows**：CD 的 Windows 腿安装 Inno Setup（choco）后执行 `ISCC.exe`（以 `/DSRC=`
  指定 exe 源目录），产物 `ImageDiscropper-setup-win-x64.exe` 与裸可执行文件一并发布（见根 README「发布」）。
- **macOS**：idc_gui 以 `.app` 捆绑包构建（`ImageDiscropperGui/CMakeLists.txt` 的
  `MACOSX_BUNDLE` 属性），CD 打包为 zip 分发；无自定义图标（缺省为系统默认图标）。
- **版本同步**：`installer.iss` 的 `AppVersion` 与根 CMakeLists 的 `project()` 同步（发版时改）。
- **签名**：产物未做代码签名（Windows SmartScreen 会提示、macOS Gatekeeper 需右键打开），
  后续如需签名在此目录补充证书流程说明。
