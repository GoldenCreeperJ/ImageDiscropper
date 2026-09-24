; ============================================================================
; 文件：installer.iss
; 作用：ImageDiscropper Windows 安装包脚本（Inno Setup 6）。
; 分块依据：单文件即整个安装逻辑（[Setup]/[Files]/[Icons]/[Run]/卸载均由框架生成）。
; 说明：
;   · 用法：发布工作流的 Windows 腿以 /DSRC=<exe 所在目录> 覆盖源目录后用 ISCC.exe 编译
;     （见 .github/workflows/cpp/build.yml）；独立使用时把 idc_gui.exe 与
;     idc.exe 放在本脚本旁即可。
;   · 版本号与根 CMakeLists 的 project() 同步——发版时改。
;   · 安装模式：向导提供「全局（所有用户）/ 仅当前用户」选择（免 UAC 时自动装到
;     {localappdata}\Programs）；安装目录可在向导中自定义。
;   · 语言：简中 / English 双语，按系统语言自动选择。
;   · 可选任务：桌面快捷方式、开机自启动（HKCU Run 键，卸载时自动移除）。
;   · 无自定义图标：安装包与快捷方式使用目标 exe 图标/系统默认图标（图标非打包强制项）。
; ============================================================================
#define AppName "ImageDiscropper"
#define AppVersion "1.0.0"
#define AppPublisher "GoldenCreeperJ"
#ifndef SRC
  #define SRC "."
#endif

[Setup]
AppId={{B2E0F7C4-8A5D-4E3F-9C1B-6D7A8F0E2C45}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
OutputBaseFilename=ImageDiscropper-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\idc_gui.exe

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
en.AdditionalTasks=Additional tasks:
en.CreateDesktopIcon=Create a desktop icon
en.AutoStartTask=Start with Windows
en.RunAfterInstall=Run {#AppName}
en.UninstallProgram=Uninstall %1
chinesesimp.AdditionalTasks=附加任务：
chinesesimp.CreateDesktopIcon=创建桌面快捷方式
chinesesimp.AutoStartTask=开机自启动
chinesesimp.RunAfterInstall=运行 {#AppName}
chinesesimp.UninstallProgram=卸载 %1

[Files]
Source: "{#SRC}\idc_gui.exe"; DestDir: "{app}"
Source: "{#SRC}\idc.exe"; DestDir: "{app}"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalTasks}"
Name: "autostart"; Description: "{cm:AutoStartTask}"; GroupDescription: "{cm:AdditionalTasks}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\idc_gui.exe"""; Tasks: autostart

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\idc_gui.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\idc_gui.exe"; Tasks: desktopicon
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\idc_gui.exe"; Description: "{cm:RunAfterInstall}"; Flags: nowait postinstall skipifsilent
