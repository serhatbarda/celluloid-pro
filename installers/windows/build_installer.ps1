# Reafilm Pro - Windows installer builder (Inno Setup).
#
#   pwsh installers/windows/build_installer.ps1 -BundleDir build\ReafilmPro.ofx.bundle -Version 1.0.0 -OutDir dist `
#        [-CertPfx path\to\cert.pfx -CertPassword ***]
#
# With a certificate, the plug-in DLL (ReafilmPro.ofx), the installer and its uninstaller are
# Authenticode-signed (SHA-256, RFC 3161 timestamp). Without one, everything stays unsigned
# (SmartScreen will warn on download).
param(
    [Parameter(Mandatory = $true)][string]$BundleDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$OutDir = "dist",
    [string]$CertPfx = "",
    [string]$CertPassword = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$BundleDir = (Resolve-Path $BundleDir).Path
if ((Split-Path -Leaf $BundleDir) -ne "ReafilmPro.ofx.bundle") { throw "BundleDir must point to ReafilmPro.ofx.bundle" }
$dll = Join-Path $BundleDir "Contents\Win64\ReafilmPro.ofx"
if (-not (Test-Path $dll)) { throw "missing $dll" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

function Find-Tool([string]$name, [string[]]$candidates) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($c in $candidates) {
        $hit = Get-ChildItem -Path $c -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$iscc = Find-Tool "iscc.exe" @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe")
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found" }

$sign = $CertPfx -ne ""
$signtool = $null
if ($sign) {
    if (-not (Test-Path $CertPfx)) { throw "certificate $CertPfx not found" }
    $signtool = Find-Tool "signtool.exe" @("${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe")
    if (-not $signtool) { throw "signtool.exe not found (install the Windows SDK)" }
    Write-Host "== Signing $dll"
    & $signtool sign /f $CertPfx /p $CertPassword /fd sha256 /tr $TimestampUrl /td sha256 /d "Reafilm Pro" $dll
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on the plug-in" }
    & $signtool verify /pa $dll
    if ($LASTEXITCODE -ne 0) { throw "signature verification failed" }
}

$iss = Join-Path $here "ReafilmPro.iss"
$isccArgs = @("/Q", "/DAppVersion=$Version", "/DBundleDir=$BundleDir", "/DOutputDir=$OutDir")
if ($sign) {
    $isccArgs += "/DSignToolName=celsign"
    $isccArgs += "/Scelsign=`$q$signtool`$q sign /f `$q$CertPfx`$q /p `$q$CertPassword`$q /fd sha256 /tr $TimestampUrl /td sha256 /d `$qReafilm Pro`$q `$f"
}
$isccArgs += $iss
Write-Host "== $iscc (Inno Setup)"
& $iscc @isccArgs
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $OutDir "ReafilmPro-$Version-Windows-Setup.exe"
if (-not (Test-Path $setup)) { throw "installer not produced: $setup" }
if ($sign) {
    & $signtool verify /pa $setup
    if ($LASTEXITCODE -ne 0) { throw "installer signature verification failed" }
}
$hash = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLower()
"$hash  $(Split-Path -Leaf $setup)" | Out-File -Encoding ascii "$setup.sha256"
Write-Host "Created $setup ($hash)"
