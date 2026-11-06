# SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
$ErrorActionPreference = 'Stop'
if ((Get-OSArchitectureWidth) -ne 64) {
    throw 'IconicFolders requires Windows x64.'
}
$packageArgs = @{
    packageName = $env:ChocolateyPackageName
    fileType = 'exe'
    url64bit = '@INSTALLER_URL@'
    checksum64 = '@INSTALLER_SHA256@'
    checksumType64 = 'sha256'
    silentArgs = '/S /AllUsers'
    validExitCodes = @(0)
}
Install-ChocolateyPackage @packageArgs
