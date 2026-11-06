# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([string]$OutputDirectory='build\Prototype')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
Push-Location $root
try {
    $vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vs){throw 'MSVC Build Tools required.'}
    $out=Join-Path $root $OutputDirectory
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    & .\tools\build-ui-prototype.cmd $vs $out
    if($LASTEXITCODE){throw 'UI prototype build failed.'}
    Write-Output (Join-Path $out 'IconicFolders-UI-Prototype.exe')
} finally {Pop-Location}
