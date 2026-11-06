# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([string]$TransportProject='C:\Users\Benco\Documents\FileSpacer',
      [ValidateSet('Lifecycle','Recovery','Control','CrashControl','Diagnose','Safety','GUI','Compatibility','AllUsers')][string]$Scenario='Lifecycle',
      [string]$CurrentInstaller='',
      [switch]$ResumeAllUsers)
$ErrorActionPreference='Stop'
if($ResumeAllUsers-and$Scenario-ne'AllUsers'){throw 'ResumeAllUsers is only valid for the assisted AllUsers scenario.'}
$project=Split-Path $PSScriptRoot -Parent
$tokens=$null;$parseErrors=$null
[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Test-ExplorerVM.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors){throw ('Guest test script syntax error: '+($parseErrors.Message-join'; '))}
$runId=[guid]::NewGuid().ToString('N')
$run=Join-Path $TransportProject ('build\regression\vm-'+$runId)
$package=Join-Path $run 'package'
$result=Join-Path $project ('build\vm-explorer-'+$runId)
foreach($relative in @('payload','tests\client','tests\regression\common','build\debug')) {
    [void][IO.Directory]::CreateDirectory((Join-Path $package $relative))
}
# Reuse the installed protected controller and its unchanged guest worker.
# FileSpacer source and infrastructure are not modified. Its build directory
# is the controller's allowed staging area; only guest code runs from it.
Copy-Item -LiteralPath (Join-Path $TransportProject 'tests\client\HyperVSmoke.ps1') -Destination (Join-Path $package 'tests\client')
Copy-Item -LiteralPath (Join-Path $TransportProject 'tests\regression\common\Process.ps1') -Destination (Join-Path $package 'tests\regression\common')
Copy-Item -LiteralPath (Join-Path $TransportProject 'build\debug\FileSpacer.exe') -Destination (Join-Path $package 'build\debug')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Test-ExplorerVM.ps1') -Destination (Join-Path $package 'tests\regression\Run-Regression.ps1')
Copy-Item -LiteralPath (Join-Path $project 'dist\IconicFolders-1.0.1-x64-Setup.exe') -Destination (Join-Path $package 'payload\previous-Setup.exe')
if(!$CurrentInstaller){$CurrentInstaller=Join-Path $project 'dist\IconicFolders-1.1.0-x64-Setup.exe'}
Copy-Item -LiteralPath $CurrentInstaller -Destination (Join-Path $package 'payload\current-Setup.exe')
# Use the helper shipped in the tested installer, never a different local build.
& 'C:\Program Files\7-Zip\7z.exe' e -r -y ('-o'+(Join-Path $package 'payload')) $CurrentInstaller IconicFolders.exe | Out-Null
if($LASTEXITCODE -or !(Test-Path -LiteralPath (Join-Path $package 'payload\IconicFolders.exe'))){throw 'Cannot extract the helper from the installer under test.'}
if($Scenario-in@('Control','Diagnose','Safety')) {
    Copy-Item -LiteralPath (Join-Path $project 'build\vm-probe\RestartProbe.exe') -Destination (Join-Path $package 'payload')
}
if($Scenario-in@('Lifecycle','AllUsers')) {
    Copy-Item -LiteralPath (Join-Path $project 'build\Release\IconicFoldersTests.exe') -Destination (Join-Path $package 'payload')
}
if($Scenario-eq'GUI') {
    & 'C:\Program Files\7-Zip\7z.exe' e -r -y ('-o'+(Join-Path $package 'payload')) $CurrentInstaller IconicFoldersShell.dll | Out-Null
    if($LASTEXITCODE){throw 'Cannot extract the candidate DLL.'}
    foreach($file in @('IconicFolders.exe','IconicFoldersShell.dll')) {
        if((Get-FileHash (Join-Path $package ('payload\'+$file))).Hash-ne(Get-FileHash (Join-Path $project ('build\Release\'+$file))).Hash){throw ('GUI build differs from the candidate installer: '+$file)}
    }
    foreach($file in @('NativeAppTests.exe','NativeAppTests.pdb','IconicFolders.exe','IconicFolders.pdb','SelectorTests.exe','PickerTests.exe','IconicFoldersTests.exe','IconResources.dll','IconicFoldersShell.dll')) {
        Copy-Item -LiteralPath (Join-Path $project ('build\Release\'+$file)) -Destination (Join-Path $package 'payload')
    }
    Copy-Item -LiteralPath (Join-Path $project 'assets\icon.ico') -Destination (Join-Path $package 'payload\fixture.ico')
    Copy-Item -LiteralPath (Join-Path $project 'assets\icon.png') -Destination (Join-Path $package 'payload\fixture.png')
}
if($Scenario-in@('Compatibility','AllUsers')) {
    & 'C:\Program Files\7-Zip\7z.exe' e -r -y ('-o'+(Join-Path $package 'payload')) $CurrentInstaller IconicFoldersShell.dll | Out-Null
    if($LASTEXITCODE){throw 'Cannot extract the candidate DLL.'}
    $script=if($Scenario-eq'Compatibility'){'Test-CompatibilityVM.ps1'}else{'Test-AllUsersVM.ps1'}
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $package 'tests\regression')
}
if($Scenario-eq'Compatibility') {
    foreach($file in @('TranslucentTB-2026.2.msixbundle','vclibs-desktop-x64.appx','vclibs-uwp-x64.appx','winui-x64.appx')) {
        Copy-Item -LiteralPath (Join-Path $project ('build\compatibility\'+$file)) -Destination (Join-Path $package 'payload')
    }
}
# Compile on the host only; the client needs neither a compiler nor an SDK.
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:library ('/out:'+(Join-Path $package 'payload\ExplorerProbe.dll')) (Join-Path $project 'tests\client\ExplorerProbe.cs')
if($LASTEXITCODE){throw 'The host could not compile the guest probe.'}
@{BuildLocation='Host';HostMachine=$env:COMPUTERNAME;IconicScenario=$Scenario;IconicResumeAllUsers=$ResumeAllUsers.IsPresent;Cases=@('iconicfolders-explorer-lifecycle');TimeoutSeconds=900} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'selection.json') -Encoding UTF8
@(Get-ChildItem -LiteralPath $package -Recurse -File | ForEach-Object {
    @{Path=$_.FullName.Substring($package.Length+1);Sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}
}) | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'files.json') -Encoding UTF8
try {
    & (Join-Path $TransportProject 'tests\client\Invoke-HyperVRunner.ps1') -ProjectRoot $TransportProject -VMName AgentWin10 -RegressionRunId $runId
    if($LASTEXITCODE){throw ('VM controller failed: '+$LASTEXITCODE)}
}finally {
    Copy-Item -LiteralPath $run -Destination $result -Recurse
    Write-Host ('IconicFolders VM reports: '+$result)
}
