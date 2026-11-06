# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
# Limited guest token: exercise the real UAC-delegated installer and uninstaller.
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$RunDirectory,[switch]$ResumeFreshInstallation)
$ErrorActionPreference='Stop'
if($env:COMPUTERNAME-ne'AGENTWIN10'){throw 'VM-only functional test.'}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'The initiating test must be unelevated.'}
$root=Join-Path $env:ProgramFiles 'IconicFolders'
$registry='Registry::HKEY_LOCAL_MACHINE\Software\IconicFolders\Install'
$userRegistry='Registry::HKEY_CURRENT_USER\Software\IconicFolders\Install'
$clsid='Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{75F4D684-8623-4CE1-B3B2-464EF177FDDE}\InprocServer32'
$menu='Registry::HKEY_LOCAL_MACHINE\Software\Classes\Directory\shell\IconicFolders'
$report=[ordered]@{Status='FAIL';Mode='AllUsers';InitiatingToken='Limited';SessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId;SessionName=$env:SESSIONNAME;Stages=@();Error=$null}
$held=[IntPtr]::Zero
function Assert-Machine($condition,[string]$message){if(!$condition){throw $message}}
function Invoke-UacOperation([string]$phase,[string]$file,[string]$arguments){
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'uac-phase.txt'),$phase)
    $process=New-Object Diagnostics.Process
    $process.StartInfo.FileName=$file;$process.StartInfo.Arguments=$arguments
    $process.StartInfo.UseShellExecute=$false;$process.StartInfo.CreateNoWindow=$true
    $process.StartInfo.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    try {
        Assert-Machine ($process.Start()) ('Cannot start UAC operation: '+$phase)
        if(!$process.WaitForExit(240000)){throw ('UAC operation timed out: '+$phase)}
        Assert-Machine ($process.ExitCode-eq0) ($phase+' failed or UAC was cancelled: '+$process.ExitCode)
    }finally{$process.Dispose()}
}
function Wait-MachineRemoval {
    $deadline=[DateTime]::UtcNow.AddSeconds(240)
    do {
        $busy=$false
        foreach($name in @('Local\IconicFolders.Setup','Global\IconicFolders.Setup')) {
            $mutex=$null
            try{$mutex=[Threading.Mutex]::OpenExisting($name);$busy=$true}catch [Threading.WaitHandleCannotBeOpenedException] {}finally{if($mutex){$mutex.Dispose()}}
        }
        if(!(Test-Path $registry) -and !(Test-Path (Join-Path $root 'IconicFoldersShell.dll')) -and !$busy){break}
        Start-Sleep -Milliseconds 200
    }while([DateTime]::UtcNow-lt$deadline)
    Assert-Machine (!(Test-Path $registry) -and !(Test-Path $menu) -and !(Test-Path $clsid)) 'Machine uninstallation did not remove registration.'
    Assert-Machine (!(Test-Path (Join-Path $root 'IconicFoldersShell.dll'))) 'Canonical DLL path remains after machine uninstallation.'
}
try {
    Assert-Machine (!(Test-Path $userRegistry)) 'An existing per-user installation must not be overwritten.'
    if($ResumeFreshInstallation){
        Assert-Machine ((Test-Path $registry) -and (Test-Path -LiteralPath $root)) 'The previous test installation is missing.'
        foreach($file in @('IconicFolders.exe','IconicFoldersShell.dll')){
            Assert-Machine ((Get-FileHash (Join-Path $root $file)).Hash-eq(Get-FileHash (Join-Path $ProjectRoot ('payload\'+$file))).Hash) ('The resumed test installation differs from the candidate: '+$file)
        }
    }else{
        Assert-Machine (!(Test-Path $registry) -and !(Test-Path -LiteralPath $root)) 'An existing installation must not be overwritten by this test.'
    }
    [void][Reflection.Assembly]::LoadFrom((Join-Path $ProjectRoot 'payload\ExplorerProbe.dll'))
    # The protected controller starts with VMConnect closed. This assisted
    # phase lets the user open the BASIC console before any secure-desktop UAC.
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'uac-phase.txt'),'Connect the basic VM console now; UAC tests begin after 90 seconds.')
    for($second=0;$second-lt90;$second++){Start-Sleep -Seconds 1}
    $setup=Join-Path $ProjectRoot 'payload\current-Setup.exe'
    if($ResumeFreshInstallation){
        # Remove only the exact test payload verified above, then validate a
        # complete fresh-install/upgrade/uninstall cycle of the final package.
        Invoke-UacOperation 'Remove the previous interrupted test installation: accept the IconicFolders UAC prompt in the VM.' (Join-Path $root 'Uninstall.exe') '/S'
        Wait-MachineRemoval
        $report.Stages+=@{Name='previous-interrupted-test-uninstalled';Status='PASS'}
    }
    Invoke-UacOperation 'Fresh installation: accept the IconicFolders UAC prompt in the VM.' $setup '/S /AllUsers'
    Assert-Machine ((Get-ItemProperty $registry).Path-eq$root) 'Machine installation path is incorrect.'
    Assert-Machine ((Get-Item $clsid).GetValue('')-eq(Join-Path $root 'IconicFoldersShell.dll')) 'Machine COM registration is incorrect.'
    Assert-Machine ((Test-Path $menu) -and !(Test-Path $userRegistry)) 'Registration scope is incorrect.'
    Assert-Machine ((Get-Content (Join-Path $root 'install-info.ini') -Raw)-match'Mode=AllUsers') 'Uninstaller scope metadata is missing.'
    $report.Stages+=@{Name='all-users-fresh-install';Status='PASS'}
    $integration=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\IconicFoldersTests.exe') -Arguments '--registered' -OutputPath (Join-Path $RunDirectory 'registered.stdout.log') -ErrorPath (Join-Path $RunDirectory 'registered.stderr.log')
    try {
        if(!$integration.Process.WaitForExit(30000)){throw 'Machine COM integration test timed out.'}
        $exit=Complete-RecordedProcess $integration
        $report.Stages+=@{Name='all-users-integration-exit';ExitCode=$exit}
        Assert-Machine ($exit-eq0) ('Machine COM activation or helper reset failed: '+$exit)
    }finally {
        if(!$integration.Process.HasExited){$integration.Process.Kill();[void]$integration.Process.WaitForExit(5000)}
        $integration.Process.Dispose()
    }
    $report.Stages+=@{Name='all-users-registered-COM-and-helper-reset';Status='PASS'}
    $held=[IconicFolders.ClientTests.ExplorerProbe]::HoldTestLibrary((Join-Path $root 'IconicFoldersShell.dll'))
    Invoke-UacOperation 'Upgrade with loaded DLL: accept the IconicFolders UAC prompt in the VM.' $setup '/S /AllUsers'
    Assert-Machine (@(Get-ChildItem -LiteralPath $root -Filter 'IconicFoldersShell.dll.backup-*').Count-gt0) 'Loaded machine DLL was not retired.'
    Assert-Machine ((Get-FileHash (Join-Path $root 'IconicFoldersShell.dll')).Hash-eq(Get-FileHash (Join-Path $ProjectRoot 'payload\IconicFoldersShell.dll')).Hash) 'Published machine payload differs from the candidate.'
    $report.Stages+=@{Name='all-users-loaded-dll-upgrade';Status='PASS'}
    Invoke-UacOperation 'Uninstallation: accept the IconicFolders UAC prompt in the VM.' (Join-Path $root 'Uninstall.exe') '/S'
    Wait-MachineRemoval
    $report.Stages+=@{Name='all-users-loaded-dll-uninstall';Status='PASS'}
    $report.Status='PASS'
}catch {$report.Error=$_.Exception.Message}
finally {
    if($held-ne[IntPtr]::Zero){[IconicFolders.ClientTests.ExplorerProbe]::ReleaseTestLibrary($held)}
    foreach($name in @('IconicFolders-install.log','IconicFolders-install-machine.log')){
        $log=Join-Path $env:TEMP $name
        if(Test-Path -LiteralPath $log){Copy-Item -LiteralPath $log -Destination (Join-Path $RunDirectory $name) -Force}
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $RunDirectory 'all-users.json') -Encoding UTF8
}
if($report.Status-ne'PASS'){throw $report.Error}
