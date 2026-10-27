# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([string]$CertificateThumbprint='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$sdk=Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory | Where-Object {Test-Path (Join-Path $_.FullName 'x64\makeappx.exe')} | Sort-Object Name -Descending | Select-Object -First 1
if(!$sdk) {throw 'Windows SDK packaging tools not found.'}
$staging=Join-Path $root 'build\identity'
New-Item -ItemType Directory -Path "$staging\assets","$root\dist" -Force | Out-Null
Copy-Item "$root\installer\AppxManifest.xml" $staging -Force
Copy-Item "$root\assets\logo*.png" "$staging\assets" -Force
$package=Join-Path $root 'dist\IconicFolders.Identity.msix'
& "$($sdk.FullName)\x64\makeappx.exe" pack /o /nv /d $staging /p $package
if($LASTEXITCODE) {throw 'Identity package failed.'}
if($CertificateThumbprint) {
 & "$($sdk.FullName)\x64\signtool.exe" sign /fd SHA256 /sha1 $CertificateThumbprint $package
 if($LASTEXITCODE) {throw 'Signing failed. Certificate subject must match the manifest Publisher.'}
}
