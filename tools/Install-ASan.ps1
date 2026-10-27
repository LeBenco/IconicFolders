# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
$ErrorActionPreference = 'Stop'
# A private installation avoids changing the shared VS instance or requiring UAC.
$root = Split-Path $PSScriptRoot
$destination = Join-Path $root 'tools\asan'
$catalogPath = 'C:\ProgramData\Microsoft\VisualStudio\Packages\_Instances'
$catalogFile = Get-ChildItem $catalogPath -Filter catalog.json -Recurse | Select-Object -First 1
if (!$catalogFile) { throw 'Visual Studio package catalog not found.' }
$catalog = Get-Content $catalogFile.FullName -Raw | ConvertFrom-Json
$packages = $catalog.packages | Where-Object { $_.id -match '^Microsoft.VC.14.44.17.14.ASAN.(Headers|X64).base$' }
if ($packages.Count -ne 2) { throw 'Matching MSVC 14.44 ASan packages not found.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($package in $packages) {
    $payload = $package.payloads[0]
    $archive = Join-Path $destination $payload.fileName
    Invoke-WebRequest -UseBasicParsing $payload.url -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $payload.sha256) { throw 'ASan SHA256 mismatch.' }
    $extract = Join-Path $destination $package.id
    if (!(Test-Path $extract)) { [IO.Compression.ZipFile]::ExtractToDirectory($archive, $extract) }
    Remove-Item -LiteralPath $archive
    Write-Output "$($package.id) $($package.version): verified and installed locally"
}
