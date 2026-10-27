# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([ValidateSet('Release','Asan')][string]$Configuration='Release',[switch]$Test,[switch]$Installer,[switch]$RegenerateIcons)
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs) { throw 'MSVC Build Tools 2022 required.' }
$out=Join-Path $PSScriptRoot "build\$Configuration"
New-Item -ItemType Directory -Path $out -Force | Out-Null
$iconInputs=Get-Item 'assets\icon.svg','tools\Make-Assets.py'
$iconOutputs=@('assets\icon.ico','assets\icon.png','assets\logo44.png','assets\logo150.png')
$missingIcons=@($iconOutputs | Where-Object { !(Test-Path -LiteralPath $_) })
$newestInput=($iconInputs | Measure-Object LastWriteTimeUtc -Maximum).Maximum
$oldestOutput=($iconOutputs | Where-Object { Test-Path -LiteralPath $_ } | Get-Item | Measure-Object LastWriteTimeUtc -Minimum).Minimum
if($RegenerateIcons -or $missingIcons.Count -or $newestInput -gt $oldestOutput) {
    $inkscape=$env:INKSCAPE
    if(!$inkscape) { $inkscape=Join-Path $env:ProgramFiles 'Inkscape\bin\inkscape.com' }
    $iconPython=$env:ICON_PYTHON
    if(!$iconPython) { $iconPython=Join-Path (Split-Path $inkscape) 'python.exe' }
    if(!(Test-Path -LiteralPath $inkscape) -or !(Test-Path -LiteralPath $iconPython)) {
        throw 'Icon generation requires Inkscape and its bundled Python. Set INKSCAPE and ICON_PYTHON for another installation.'
    }
    & $iconPython .\tools\Make-Assets.py --inkscape $inkscape --work-dir "$out\assets"
    if($LASTEXITCODE) { throw 'Icon generation failed.' }
} else { Write-Output 'Icon assets are up to date.' }
$asanLib=''
if($Configuration -eq 'Asan') {
    $asan=Get-ChildItem "$vs\VC\Tools\MSVC" -Recurse -Filter clang_rt.asan_dynamic-x86_64.lib | Select-Object -First 1
    if(!$asan) { $asan=Get-ChildItem "$PSScriptRoot\tools\asan" -Recurse -Filter clang_rt.asan_dynamic-x86_64.lib -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if(!$asan) { throw 'Install AddressSanitizer: tools\Install-ASan.ps1' }
    $asanLib=$asan.DirectoryName
    $runtime=Get-ChildItem "$PSScriptRoot\tools\asan","$vs\VC\Tools\MSVC" -Recurse -Filter clang_rt.asan_dynamic-x86_64.dll -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'Hostx64\\x64|bin\\x64' } | Select-Object -First 1
    if(!$runtime) { throw 'ASan x64 runtime missing.' }
    Copy-Item $runtime.FullName $out -Force
}
& .\tools\build-native.cmd $vs $out $Configuration $asanLib
if($LASTEXITCODE) { throw 'Build failed.' }
if($Test) {
    & "$out\IconicFoldersTests.exe"
    if($LASTEXITCODE) {throw 'Tests failed.'}
    if($Configuration -eq 'Asan') {
        $probeLog=Join-Path $out 'asan-positive-control.log'
        $process=Start-Process "$out\ASanProbe.exe" -WindowStyle Hidden -RedirectStandardError $probeLog -Wait -PassThru
        if($process.ExitCode -eq 0 -or !(Select-String -Path $probeLog -Pattern 'AddressSanitizer: heap-buffer-overflow' -Quiet)) {throw 'ASan positive control did not detect the intentional overflow.'}
        Write-Output 'PASS AddressSanitizer positive control: intentional heap-buffer-overflow detected'
    }
}
if($Installer) {
    if($Configuration -ne 'Release') {throw 'Installer requires Release.'}
    New-Item -ItemType Directory dist -Force | Out-Null
    & 'C:\Program Files (x86)\NSIS\makensis.exe' /V2 installer\IconicFolders.nsi
    if($LASTEXITCODE) {throw 'NSIS build failed.'}
}
