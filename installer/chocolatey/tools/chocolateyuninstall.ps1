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
# Let NSIS create its temporary worker and resolve its own installation path.
# The bootstrap may exit first, so wait for actual removal before returning.
Uninstall-ChocolateyPackage -PackageName $env:ChocolateyPackageName -FileType 'exe' `
    -File $uninstaller -SilentArgs '/S /AllUsers' -ValidExitCodes @(0)
$deadline = [DateTime]::UtcNow.AddSeconds(120)
do {
    $busy = $false
    foreach ($name in @('Local\IconicFolders.Setup', 'Global\IconicFolders.Setup')) {
        $mutex = $null
        try {
            $mutex = [Threading.Mutex]::OpenExisting($name)
            $busy = $true
        } catch [Threading.WaitHandleCannotBeOpenedException] {
        } finally {
            if ($mutex) { $mutex.Dispose() }
        }
    }
    if (!(Test-Path -LiteralPath $keys[0].PSPath) -and !$busy -and
        !(Test-Path -LiteralPath (Join-Path $keys[0].InstallLocation 'IconicFolders.exe')) -and
        !(Test-Path -LiteralPath (Join-Path $keys[0].InstallLocation 'IconicFoldersShell.dll'))) {
        return
    }
    Start-Sleep -Milliseconds 200
} while ([DateTime]::UtcNow -lt $deadline)
throw 'The native IconicFolders uninstaller did not finish removing the machine installation.'
