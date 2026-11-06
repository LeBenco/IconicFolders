# SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
$ErrorActionPreference = 'Stop'
$keys = @(Get-UninstallRegistryKey -SoftwareName 'IconicFolders' | Where-Object {
    $_.PSPath -match 'HKEY_LOCAL_MACHINE' -and $_.PSChildName -eq 'IconicFolders'
})
if ($keys.Count -eq 0) {
    Write-Warning 'No machine-wide IconicFolders installation remains.'
    return
}
if ($keys.Count -ne 1 -or !$keys[0].InstallLocation) {
    throw 'Cannot identify a unique machine-wide IconicFolders installation.'
}
$uninstaller = Join-Path $keys[0].InstallLocation 'uninstall.exe'
if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
    throw "IconicFolders uninstaller is missing: $uninstaller"
}
# NSIS must run in place so the helper waits for the actual uninstall process.
# _?= must remain last and unquoted, including when the directory contains spaces.
Uninstall-ChocolateyPackage -PackageName $env:ChocolateyPackageName -FileType 'exe' `
    -File $uninstaller -SilentArgs "/S /AllUsers _?=$($keys[0].InstallLocation)" -ValidExitCodes @(0)
# In-place NSIS cannot delete its own executable until the process has exited.
if (Test-Path -LiteralPath $uninstaller) {
    Remove-Item -LiteralPath $uninstaller -Force
}
if ((Test-Path -LiteralPath $keys[0].InstallLocation) -and
    !(Get-ChildItem -LiteralPath $keys[0].InstallLocation -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $keys[0].InstallLocation
}
