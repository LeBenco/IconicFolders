# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
# Build the same NSIS script with an isolated registry prefix. Never redirect
# the actual user's COM registration or modify an existing installation.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$id=[Guid]::NewGuid().ToString('N')
$testRoot=Join-Path $root "build\upgrade-test-$id"
$target=Join-Path $testRoot 'installation'
$prefix="Software\IconicFoldersInstallerTests\$id\"
$settings="HKCU:\${prefix}Software\IconicFolders"
$runOnce="HKCU:\${prefix}Software\Microsoft\Windows\CurrentVersion\RunOnce"
$registration="HKCU:\${prefix}Software\Classes\CLSID\{75F4D684-8623-4CE1-B3B2-464EF177FDDE}\InprocServer32"
$setup=Join-Path $testRoot 'IconicFolders-Upgrade-Test.exe'
New-Item -ItemType Directory -Path $testRoot | Out-Null
& 'C:\Program Files (x86)\NSIS\makensis.exe' /INPUTCHARSET UTF8 /WX /V2 "/DREGISTRY_PREFIX=$prefix" "/DSETUP_OUTPUT=$setup" "/DSETUP_MUTEX=Local\IconicFolders.Setup.Tests.$id" "$root\installer\IconicFolders.nsi"
if($LASTEXITCODE){throw 'Isolated installer build failed.'}
function Invoke-Setup([int]$Expected=0){
 $process=Start-Process -FilePath $setup -ArgumentList "/S /D=$target" -WindowStyle Hidden -PassThru
 if(!$process.WaitForExit(30000)){throw 'Installer timed out.'}
 if($process.ExitCode -ne $Expected){throw "Unexpected installer exit $($process.ExitCode); expected $Expected."}
}
function Invoke-Uninstall([switch]$Cleanup){
 $arguments=if($Cleanup){"/S /UPGRADE_CLEANUP _?=$target"}else{"/S _?=$target"}
 $process=Start-Process -FilePath "$target\Uninstall.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru
 if(!$process.WaitForExit(30000)){throw 'Uninstaller timed out.'}
 if($process.ExitCode){throw "Uninstaller exit $($process.ExitCode); cleanup=$Cleanup"}
}
function Assert([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Assert-Payload {
 foreach($name in 'IconicFolders.exe','IconicFoldersShell.dll'){
  Assert ((Get-FileHash -LiteralPath "$target\$name").Hash -eq (Get-FileHash -LiteralPath "$root\build\Release\$name").Hash) "Payload mismatch: $name"
 }
 Assert ((Get-Item -LiteralPath $registration).GetValue('') -eq "$target\IconicFoldersShell.dll") 'COM path changed unexpectedly.'
}
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class IconicFoldersUpgradeLock {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibrary(string path);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int CanUnload();
 public static int Check(IntPtr module){return ((CanUnload)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"DllCanUnloadNow"),typeof(CanUnload)))();}
}
'@
$loaded=[IntPtr]::Zero
$blocked=$null
$installed=$false
try {
 Invoke-Setup;$installed=$true;Assert-Payload
 Write-Output 'PASS fresh installation in an isolated registry namespace'
 # Reproduce an existing beta.1 deployment with the actual previous binaries.
 $previous=Join-Path $root 'dist\IconicFolders-1.1.0-beta.1-x64-Setup.exe'
 if(Test-Path -LiteralPath $previous){
  $legacy=Join-Path $testRoot 'legacy'
  & 'C:\Program Files\7-Zip\7z.exe' x $previous "-o$legacy" -y IconicFolders.exe IconicFoldersShell.dll | Out-Null
  if($LASTEXITCODE){throw 'Previous installer extraction failed.'}
  Copy-Item -LiteralPath "$legacy\IconicFolders.exe","$legacy\IconicFoldersShell.dll" -Destination $target -Force
 }
 New-ItemProperty -LiteralPath $settings -Name Copy -PropertyType DWord -Value 1 -Force | Out-Null
 New-ItemProperty -LiteralPath $settings -Name Sources -PropertyType MultiString -Value @('C:\Example\one.dll','C:\Example\two.exe') -Force | Out-Null
 New-ItemProperty -LiteralPath $settings -Name WindowPlacement -PropertyType Binary -Value ([byte[]](1,2,3,4)) -Force | Out-Null
 New-Item -Path $runOnce -Force | Out-Null
 New-ItemProperty -LiteralPath $runOnce -Name IconicFoldersCleanup -Value 'legacy pending uninstall' -Force | Out-Null
 $custom=Join-Path $testRoot 'custom-folder';New-Item -ItemType Directory -Path $custom | Out-Null
 [IO.File]::WriteAllText("$custom\desktop.ini","[.ShellClassInfo]`r`nIconResource=iconicfolders.ico,0`r`n",[Text.Encoding]::Unicode)
 [IO.File]::WriteAllBytes("$custom\iconicfolders.ico",[byte[]](10,20,30))
 $iniHash=(Get-FileHash -LiteralPath "$custom\desktop.ini").Hash
 $oldHash=(Get-FileHash -LiteralPath "$target\IconicFoldersShell.dll").Hash
 $loaded=[IconicFoldersUpgradeLock]::LoadLibrary("$target\IconicFoldersShell.dll")
 Assert ($loaded -ne [IntPtr]::Zero) 'Cannot load the old Shell DLL.'
 Invoke-Setup;Assert-Payload
 Assert ([IconicFoldersUpgradeLock]::Check($loaded) -eq 0) 'Loaded old DLL is no longer callable.'
 $backups=@(Get-ChildItem -LiteralPath $target -Filter 'IconicFoldersShell.dll.backup-*')
 Assert ($backups.Count -eq 1 -and (Get-FileHash -LiteralPath $backups[0].FullName).Hash -eq $oldHash) 'Loaded old DLL was not retained intact.'
 Assert ((Get-Item -LiteralPath $runOnce).GetValue('IconicFoldersUpgradeCleanup') -eq ('"'+$target+'\Uninstall.exe" /S /UPGRADE_CLEANUP')) 'Unsafe or missing upgrade cleanup command.'
 Assert ($null -eq (Get-Item -LiteralPath $runOnce).GetValue('IconicFoldersCleanup')) 'Legacy uninstall would remove the new version at sign-in.'
 $prefs=Get-ItemProperty -LiteralPath $settings
 Assert ($prefs.Copy -eq 1 -and ($prefs.Sources -join '|') -eq 'C:\Example\one.dll|C:\Example\two.exe' -and ($prefs.WindowPlacement -join ',') -eq '1,2,3,4') 'Upgrade changed preferences.'
 Assert ((Get-FileHash -LiteralPath "$custom\desktop.ini").Hash -eq $iniHash) 'Upgrade changed a customized folder.'
 Write-Output 'PASS loaded DLL upgrade, old module lifetime, preferences and folder icons preserved'
 Invoke-Setup;Assert-Payload
 Assert (@(Get-ChildItem -LiteralPath $target -Filter 'IconicFoldersShell.dll.backup-*').Count -eq 1) 'Repeated upgrade lost or accumulated live backups.'
 [void][IconicFoldersUpgradeLock]::FreeLibrary($loaded);$loaded=[IntPtr]::Zero
 Invoke-Uninstall -Cleanup;Assert-Payload
 Assert (@(Get-ChildItem -LiteralPath $target -Filter '*.backup-*').Count -eq 0) 'Released backups were not removed.'
 Assert (@(Get-ChildItem -LiteralPath $target -Filter '.upgrade-*.committed').Count -eq 0) 'Commit markers were not removed.'
 Assert ((Get-Item -LiteralPath $settings).GetValue('Copy') -eq 1) 'Upgrade cleanup ran the full uninstaller.'
 Write-Output 'PASS repeated upgrade and deferred cleanup preserve the installed application'
 # DLL rename succeeds, then a deliberate EXE lock makes the next step fail.
 $loaded=[IconicFoldersUpgradeLock]::LoadLibrary("$target\IconicFoldersShell.dll")
 $blocked=[IO.File]::Open("$target\IconicFolders.exe",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 Invoke-Setup -Expected 2;Assert-Payload
 Assert (@(Get-ChildItem -LiteralPath $target -Filter '*.backup-*').Count -eq 0) 'Rollback left a previous file displaced.'
 Assert ([IconicFoldersUpgradeLock]::Check($loaded) -eq 0) 'Rollback damaged the old DLL.'
 $blocked.Dispose();$blocked=$null
 [void][IconicFoldersUpgradeLock]::FreeLibrary($loaded);$loaded=[IntPtr]::Zero
 Write-Output 'PASS failed update restores the complete previous file set'
 # Full uninstall must also defer cleanup of a DLL from a previous upgrade.
 $loaded=[IconicFoldersUpgradeLock]::LoadLibrary("$target\IconicFoldersShell.dll")
 Invoke-Setup
 Invoke-Uninstall
 Assert (Test-Path -LiteralPath "$target\Uninstall.exe") 'Uninstaller needed for deferred deletion was removed.'
 Assert ($null -ne (Get-Item -LiteralPath $runOnce).GetValue('IconicFoldersCleanup')) 'Full deferred uninstall not scheduled.'
 [void][IconicFoldersUpgradeLock]::FreeLibrary($loaded);$loaded=[IntPtr]::Zero
 Invoke-Uninstall;$installed=$false
 Assert (!(Test-Path -LiteralPath $settings)) 'Uninstallation left preferences behind.'
 Assert ((Get-FileHash -LiteralPath "$custom\desktop.ini").Hash -eq $iniHash) 'Uninstallation changed customized folders.'
 Write-Output 'PASS full uninstall cleans retired DLLs and leaves customized folders intact'
 Write-Output 'RESULT 5 installer scenarios passed'
} finally {
 if($blocked){$blocked.Dispose()}
 if($loaded -ne [IntPtr]::Zero){[void][IconicFoldersUpgradeLock]::FreeLibrary($loaded)}
 if($installed -and (Test-Path -LiteralPath "$target\Uninstall.exe")){Invoke-Uninstall}
 $registryRoot="HKCU:\Software\IconicFoldersInstallerTests\$id"
 if(Test-Path -LiteralPath $registryRoot){Remove-Item -LiteralPath $registryRoot -Recurse -Force}
 $resolved=[IO.Path]::GetFullPath($testRoot)
 Assert ($resolved.StartsWith([IO.Path]::GetFullPath((Join-Path $root 'build'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Unsafe test cleanup target.'
 if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
