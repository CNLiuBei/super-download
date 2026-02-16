#define MyAppName "Super Download"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Super Download"
#define MyAppExeName "super_download.exe"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=SuperDownload_Setup_{#MyAppVersion}
SetupIconFile=src\gui\logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=欢迎安装 {#MyAppName}
WelcomeLabel2=安装程序将引导您完成 {#MyAppName} {#MyAppVersion} 的安装。%n%n建议关闭所有其他应用程序后继续。
SelectDirLabel3=安装程序将把 {#MyAppName} 安装到以下文件夹。
SelectDirBrowseLabel=点击「下一步」继续，或点击「浏览」选择其他文件夹。
DiskSpaceWarning=至少需要 %1 KB 的可用空间才能安装，当前只有 %2 KB 可用。是否继续？
ButtonNext=下一步(&N) >
ButtonInstall=安装(&I)
ButtonFinish=完成(&F)
ButtonCancel=取消
ButtonBack=< 上一步(&B)
ButtonBrowse=浏览(&R)...
ButtonWizardBrowse=浏览(&R)...
SelectDirDesc=选择安装位置
ReadyLabel1=安装程序已准备好安装 {#MyAppName}。
ReadyLabel2a=点击「安装」继续，或点击「上一步」修改设置。
ReadyLabel2b=点击「安装」继续。
InstallingLabel=正在安装 {#MyAppName}，请稍候...
FinishedHeadingLabel=安装完成
FinishedLabel={#MyAppName} 已成功安装到您的计算机。
FinishedLabelNoIcons={#MyAppName} 已成功安装。
ClickFinish=点击「完成」退出安装程序。
StatusExtractFiles=正在解压文件...
StatusCreateIcons=正在创建快捷方式...
StatusCreateRegEntries=正在写入注册表...
StatusRunProgram=正在完成安装...
AdditionalIcons=附加快捷方式:
CreateDesktopIcon=创建桌面快捷方式(&D)
UninstallProgram=卸载 {#MyAppName}
ConfirmUninstall=确定要完全卸载 {#MyAppName} 及其所有组件吗？
UninstallStatusLabel=正在卸载 {#MyAppName}，请稍候...
UninstalledAll={#MyAppName} 已成功从您的计算机中卸载。
ExitSetupTitle=退出安装
ExitSetupMessage=安装尚未完成。如果现在退出，程序将不会被安装。%n%n确定要退出安装吗？

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式:"
Name: "autostart"; Description: "开机自动启动"; GroupDescription: "其他选项:"

[Files]
Source: "package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "SuperDownload"; ValueData: """{app}\{#MyAppExeName}"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart
; Register superdownload:// protocol handler
Root: HKCR; Subkey: "superdownload"; ValueType: string; ValueData: "URL:Super Download Protocol"; Flags: uninsdeletekey
Root: HKCR; Subkey: "superdownload"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCR; Subkey: "superdownload\DefaultIcon"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"",0"
Root: HKCR; Subkey: "superdownload\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('taskkill', '/f /im super_download.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;

function InitializeUninstall(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('taskkill', '/f /im super_download.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := True;
end;
