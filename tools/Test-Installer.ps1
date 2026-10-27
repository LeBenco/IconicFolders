# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([switch]$LockedDll)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$target=Join-Path $root 'build\install-test'
$clsid='Software\Classes\CLSID\{75F4D684-8623-4CE1-B3B2-464EF177FDDE}'
$menu='Software\Classes\Directory\shell\IconicFolders'
if((Test-Path 'HKCU:\Software\IconicFolders') -or (Test-Path "HKCU:\$clsid") -or (Test-Path "HKCU:\$menu")) {throw 'An existing installation/settings was found. Installer test will not modify it.'}
if(Test-Path $target) {throw 'Installer test destination must be empty.'}
$setup=Join-Path $root 'dist\IconicFolders-1.0.1-x64-Setup.exe'
$process=Start-Process -FilePath $setup -ArgumentList "/S /D=$target" -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(30000)) {throw 'Installer timed out.'}
if($process.ExitCode) {throw "Installer exit $($process.ExitCode)"}
$heldDll=[IntPtr]::Zero
try {
 foreach($file in 'IconicFolders.exe','IconicFoldersShell.dll','Uninstall.exe','LICENSE.txt','WIL-LICENSE.txt','WIL-THIRD-PARTY-NOTICES.txt','MICROSOFT-FLUENT-EMOJI-LICENSE.txt') {if(!(Test-Path (Join-Path $target $file))) {throw "Missing installed file: $file"}}
 foreach($file in 'IconicFolders.exe','IconicFoldersShell.dll') {
  if((Get-FileHash (Join-Path $target $file)).Hash -ne (Get-FileHash (Join-Path "$root\build\Release" $file)).Hash) {throw "Installed payload differs from the validated build: $file"}
 }
 $registered=(Get-Item "HKCU:\$clsid\InprocServer32").GetValue('')
 if($registered -ne (Join-Path $target 'IconicFoldersShell.dll')) {throw 'Wrong COM path.'}
 if((Get-Item "HKCU:\$menu").GetValue('ExplorerCommandHandler') -ne '{75F4D684-8623-4CE1-B3B2-464EF177FDDE}') {throw 'Wrong menu registration.'}
 & "$root\build\Release\IconicFoldersTests.exe" --registered
 if($LASTEXITCODE) {throw 'Installed integration test failed.'}
 Write-Output 'PASS silent NSIS installation, menu and COM registration'
 if($LockedDll) {
  Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class IconicFoldersInstallerLock { [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibrary(string name); [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module); }'
  $heldDll=[IconicFoldersInstallerLock]::LoadLibrary((Join-Path $target 'IconicFoldersShell.dll'))
  if($heldDll -eq [IntPtr]::Zero) {throw 'Could not hold the installed DLL for the locked-uninstall test.'}
 }
} finally {
 $process=Start-Process -FilePath "$target\Uninstall.exe" -ArgumentList "/S _?=$target" -WindowStyle Hidden -PassThru
 if(!$process.WaitForExit(30000)) {throw 'Uninstaller timed out.'}
 if($process.ExitCode) {throw "Uninstaller exit $($process.ExitCode)"}
 if($heldDll -ne [IntPtr]::Zero) {
  try {
   $pending=(Get-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce').GetValue('IconicFoldersCleanup')
   if($pending -ne ('"'+$target+'\Uninstall.exe" /S') -or !(Test-Path "$target\IconicFoldersShell.dll")) {throw 'Locked DLL cleanup was not correctly scheduled.'}
   Write-Output 'PASS loaded DLL is retained and per-user sign-in cleanup is scheduled'
  } finally { [void][IconicFoldersInstallerLock]::FreeLibrary($heldDll) }
  # Execute the exact RunOnce command, without actually signing out the user.
  $process=Start-Process -FilePath "$target\Uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -PassThru
  if(!$process.WaitForExit(30000) -or $process.ExitCode) {throw 'Deferred uninstallation failed after releasing the DLL.'}
  for($retry=0;$retry -lt 100 -and (Test-Path $target);$retry++) {Start-Sleep -Milliseconds 50}
  if(Test-Path $target) {throw 'The scheduled cleanup command did not remove the program directory.'}
  if((Get-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce').GetValue('IconicFoldersCleanup')) {throw 'Deferred cleanup registry value left behind.'}
  Write-Output 'PASS deferred cleanup removes DLL and its RunOnce value after release'
 }
}
if((Test-Path 'HKCU:\Software\IconicFolders') -or (Test-Path "HKCU:\$clsid") -or (Test-Path "HKCU:\$menu")) {throw 'Registration/settings left behind.'}
# NSIS cannot delete its own running executable with _?= (synchronous test mode).
$resolved=[IO.Path]::GetFullPath($target)
if(!$resolved.StartsWith([IO.Path]::GetFullPath((Join-Path $root 'build'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unsafe test cleanup target.'}
$leftovers=Get-ChildItem -LiteralPath $resolved -File -ErrorAction SilentlyContinue
if($leftovers | Where-Object Name -ne 'Uninstall.exe') {throw 'Installed payload was not fully removed.'}
if(Test-Path $resolved) {Remove-Item -LiteralPath $resolved -Recurse -Force}
Write-Output 'PASS silent NSIS uninstallation, registry and payload cleanup'
