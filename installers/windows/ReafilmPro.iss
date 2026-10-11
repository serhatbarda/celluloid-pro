; Reafilm Pro - Windows installer (Inno Setup 6.3 or newer).
;
; Installs ReafilmPro.ofx.bundle into "C:\Program Files\Common Files\OFX\Plugins", the OFX plug-in
; folder DaVinci Resolve scans. Build (paths relative to this file or absolute):
;
;   iscc /DAppVersion=1.0.0 /DBundleDir=..\..\build\ReafilmPro.ofx.bundle /DOutputDir=..\..\dist ReafilmPro.iss
;
; Optional Authenticode signing of the installer and uninstaller: define SignToolName and pass the
; matching /S option to ISCC, e.g.
;   iscc /DSignToolName=celsign "/Scelsign=signtool.exe sign /f $qcert.pfx$q /p $qPASSWORD$q /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f" ...

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BundleDir
  #define BundleDir "..\..\build\ReafilmPro.ofx.bundle"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName "Reafilm Pro"
#define AppPublisher "Serhat (serhatbarda.de)"
#define BundleName "ReafilmPro.ofx.bundle"

[Setup]
; Never change AppId: it identifies the product for upgrades and uninstall.
AppId={{6B0F3C1E-2D7A-4F49-9C1B-8E5A7D3C4F21}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/serhatbarda/celluloid-pro
AppSupportURL=https://github.com/serhatbarda/celluloid-pro
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} OpenFX plug-in installer
DefaultDirName={commoncf64}\OFX\Plugins
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
DirExistsWarning=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=ReafilmPro-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={#AppName} (OFX plug-in)
; kept from the Celluloid Pro releases so upgrades find the existing uninstaller
UninstallFilesDir={commonappdata}\Celluloid\Uninstall
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Messages]
english.WelcomeLabel2=This will install [name/ver] into the OpenFX plug-in folder used by DaVinci Resolve.%n%nPlease quit DaVinci Resolve before continuing.
german.WelcomeLabel2=Dieses Programm installiert [name/ver] in den OpenFX-Plug-in-Ordner von DaVinci Resolve.%n%nBitte beenden Sie DaVinci Resolve, bevor Sie fortfahren.

[InstallDelete]
; remove any previous version completely so no stale files survive an upgrade
Type: filesandordirs; Name: "{app}\{#BundleName}"
; the bundle of the pre-rename releases (Celluloid Pro): same plug-in, Resolve would list it twice
Type: filesandordirs; Name: "{app}\Celluloid.ofx.bundle"

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}\{#BundleName}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: dirifempty; Name: "{app}\{#BundleName}"

[Code]
function IsResolveRunning(): Boolean;
var
  ResultCode: Integer;
begin
  { tasklist exits 0 in both cases; findstr sets the exit code }
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq Resolve.exe" /NH | findstr /I "Resolve.exe" >NUL',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardSilent() then
    exit;   { unattended deployments: Inno's Restart Manager support (CloseApplications) handles locked files }
  while IsResolveRunning() do
  begin
    if MsgBox('DaVinci Resolve is running. Please quit Resolve, then click OK to continue (Cancel aborts the installation).',
              mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := 'DaVinci Resolve is still running.';
      exit;
    end;
  end;
end;
