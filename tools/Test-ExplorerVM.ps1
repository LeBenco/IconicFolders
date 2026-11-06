# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
# Guest entry point for the existing AgentWin10 transport. Never run on the host.
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[switch]$Prepared,
      [Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'common\Process.ps1')
[void][IO.Directory]::CreateDirectory($RunDirectory)
$report=[ordered]@{Status='BLOCKED';Application='IconicFolders';Machine=$env:COMPUTERNAME;
    StartedUtc=[DateTime]::UtcNow.ToString('o');Stages=@();Crashes=@();Error=$null;CleanupErrors=@();Artifacts=@()}
$installed=$false
$root=Join-Path $env:LOCALAPPDATA 'Programs\IconicFolders'
$menu='Registry::HKEY_CURRENT_USER\Software\Classes\Directory\shell\IconicFolders'
$com='Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{75F4D684-8623-4CE1-B3B2-464EF177FDDE}'
$install='Registry::HKEY_CURRENT_USER\Software\IconicFolders\Install'
$start=Get-Date
$shell=$null
function Assert-True($condition,[string]$message) { if(!$condition){throw $message} }
function Start-Wait([string]$file,[string]$arguments) {
    $process=Start-Process -FilePath $file -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try { if(!$process.WaitForExit(60000)){throw ('Process timed out: '+$file)}; $process.Refresh(); Assert-True ($process.ExitCode-eq0) ('Process failed: '+$file+'; exit='+$process.ExitCode) }
    finally {if(!$process.HasExited){$process.Kill();[void]$process.WaitForExit(5000)};$process.Dispose()}
}
function Wait-InstalledUninstaller {
    # NSIS's normal bootstrap hands off to its TEMP copy. Wait for that copy,
    # including a deferred uninstall that intentionally keeps the installed EXE.
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        $busy=$false
        foreach($name in @('Local\IconicFolders.Setup','Global\IconicFolders.Setup')) {
            $mutex=$null
            try {
                # NSIS holds this named object open; it does not own its lock.
                $mutex=[Threading.Mutex]::OpenExisting($name)
                $busy=$true
            }catch [Threading.WaitHandleCannotBeOpenedException] {
                # No installer/uninstaller has the object open.
            }finally{if($mutex){$mutex.Dispose()}}
        }
        if(!(Test-Path -LiteralPath $install) -and !$busy){return}
        Start-Sleep -Milliseconds 100
    }while([DateTime]::UtcNow-lt$deadline)
    throw 'The installed uninstaller did not finish.'
}
function Get-ExplorerCrashes {
    @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=$start} -ErrorAction SilentlyContinue |
        Where-Object {$_.Message -match '\bexplorer\.exe\b'} |
        ForEach-Object {@{Time=$_.TimeCreated.ToString('o');RecordId=$_.RecordId;Message=$_.Message}})
}
function Watch-Desktop([string]$stage,[int]$seconds=25) {
    $desktop=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
    Assert-True ($desktop-ne0) ('No desktop: '+$stage)
    $seen=@{}
    for($i=0;$i-lt$seconds;$i++) {
        $current=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
        Assert-True ($current-eq$desktop) ('Desktop disappeared or restarted unexpectedly: '+$stage+'; original='+$desktop+'; current='+$current)
        $seen[[string]$current]=$true
        Start-Sleep -Seconds 1
    }
    $crashes=@(Get-ExplorerCrashes)
    Assert-True ($crashes.Count-eq0) ('Explorer crash recorded during '+$stage)
    $report.Stages+=@{Name=$stage;DesktopProcessId=$desktop;StableSeconds=$seconds;CrashCount=$crashes.Count}
    Write-Host ('PASS '+$stage+'; desktop='+$desktop+'; stable='+$seconds+'s')
}
function Restart-Desktop([string]$stage) {
    $previous=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
    if($selection.IconicScenario-eq'Control') {
        $job=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\RestartProbe.exe') -Arguments '' -OutputPath (Join-Path $RunDirectory ($stage+'.trace.log')) -ErrorPath (Join-Path $RunDirectory ($stage+'.stderr.log'))
        $probe=$job.Process
        try {if(!$probe.WaitForExit(60000)){throw 'Restart probe timed out.'};$exit=Complete-RecordedProcess $job;Assert-True ($exit-eq0) ('Restart probe failed: '+$exit)}
        finally {if(!$probe.HasExited){$probe.Kill();[void]$probe.WaitForExit(5000)};$probe.Dispose()}
    }else {Start-Wait (Join-Path $ProjectRoot 'payload\IconicFolders.exe') '--restart-explorer'}
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do {
        $current=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
        if($current-ne0 -and $current-ne$previous){break}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow-lt$deadline)
    Assert-True ($current-ne0 -and $current-ne$previous) ('Explorer was not restarted: '+$stage)
    $report.Stages+=@{Name=$stage+'-restart';Before=$previous;After=$current}
    Watch-Desktop $stage
}
function Open-FolderMenu([string]$stage,[bool]$requireExtension=$true,[int]$stableSeconds=10) {
    # Documented Shell automation, in the guest desktop's Explorer process.
    # No host keyboard/mouse input and no synthetic menu registration.
    $shell=New-Object -ComObject Shell.Application
    $parent=Join-Path $ProjectRoot 'sample'
    [void][IO.Directory]::CreateDirectory((Join-Path $parent 'Target'))
    $shell.Explore($parent)
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    $window=$null
    do {
        foreach($candidate in @($shell.Windows())) {
            try {if($candidate.Document.Folder.Self.Path-eq$parent){$window=$candidate;break}}catch{}
        }
        if($window){break}
        Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow-lt$deadline)
    Assert-True ($null-ne$window) 'The guest Explorer test folder did not open.'
    $item=$window.Document.Folder.ParseName('Target')
    $names=@($item.Verbs() | ForEach-Object {$_.Name})
    $loaded=@(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object {
        $process=$_
        foreach($module in $process.Modules) {
            if($module.FileName -like '*\IconicFoldersShell.dll*') {@{ProcessId=$process.Id;Path=$module.FileName}}
        }
    })
    $report.Stages+=@{Name=$stage;Verbs=$names;LoadedInExplorer=$loaded}
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    if($requireExtension){Assert-True ($loaded.Count-gt0) 'The menu probe did not establish that the real Explorer loaded IconicFoldersShell.dll.'}
    else {Assert-True ($loaded.Count-eq0) 'The control experiment must not load IconicFoldersShell.dll.'}
    if($stableSeconds-gt0){Watch-Desktop $stage $stableSeconds}
}
function Recover-GuestDesktop {
    # All destructive actions follow the guest identity check below. This is
    # recovery of the dedicated test account, never a host restart strategy.
    $session=[Diagnostics.Process]::GetCurrentProcess().SessionId
    $report.RecoveryBefore=@(Get-CimInstance Win32_Process -Filter "Name='explorer.exe' OR Name='IconicFolders.exe'" | Select-Object ProcessId,ParentProcessId,SessionId,CreationDate,CommandLine)
    $report.PreviousInstallationFiles=@(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Select-Object Name,Length)
    foreach($key in @($menu,$com)) {if(Test-Path -LiteralPath $key){Remove-Item -LiteralPath $key -Recurse -Force}}
    foreach($process in @(Get-Process IconicFolders -ErrorAction SilentlyContinue | Where-Object {$_.SessionId-eq$session})) {
        if($process.Path -like ($env:SystemDrive+'\FileSpacerTests\*\input\payload\IconicFolders.exe') -or $process.Path-ieq(Join-Path $root 'IconicFolders.exe')) {
            $process.Kill();[void]$process.WaitForExit(10000)
        }
    }
    $explorers=@(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object {$_.SessionId-eq$session})
    foreach($process in $explorers) {
        if($process.HasExited){continue}
        Assert-True ($process.Path-ieq(Join-Path $env:WINDIR 'explorer.exe')) 'Unexpected Explorer image in the guest.'
        try {if(!$process.HasExited){$process.Kill()}}catch {if(!$process.HasExited){throw}}
    }
    foreach($process in $explorers){Assert-True ($process.WaitForExit(10000)) 'The old guest Explorer did not exit.'}
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-eq0 -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 250}
    if([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-eq0){Start-Process (Join-Path $env:WINDIR 'explorer.exe') -WindowStyle Hidden}
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    while([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-eq0 -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 250}
    $uninstaller=Join-Path $root 'Uninstall.exe'
    if(Test-Path -LiteralPath $uninstaller){
        # Let NSIS copy itself to TEMP; _?= would keep the running uninstaller
        # mapped at its installed path and prevent it from deleting itself.
        Start-Wait $uninstaller '/S'
        $deadline=[DateTime]::UtcNow.AddSeconds(15)
        while((Test-Path -LiteralPath $uninstaller) -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 100}
        Assert-True (!(Test-Path -LiteralPath $uninstaller)) 'The previous test uninstaller did not remove itself.'
    }
    if(Test-Path -LiteralPath $root){
        $remaining=@(Get-ChildItem -LiteralPath $root -Force)
        $report.RemainingInstallationFiles=@($remaining|Select-Object Name,Length)
        if(!$remaining.Count){Remove-Item -LiteralPath $root -Force}
    }
    $script:start=Get-Date
    Watch-Desktop 'recovered-guest-desktop' 15
}
function Measure-GracefulShutdown([string]$name) {
    $old=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
    $folders=[IconicFolders.ClientTests.ExplorerProbe]::FolderWindowCount()
    $job=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\RestartProbe.exe') -Arguments '--graceful' -OutputPath (Join-Path $RunDirectory ($name+'.trace.log')) -ErrorPath (Join-Path $RunDirectory ($name+'.stderr.log'))
    try {
        if(!$job.Process.WaitForExit(75000)){throw 'Graceful shutdown diagnostic timed out.'}
        $exit=Complete-RecordedProcess $job
        $alive=$null-ne(Get-Process -Id $old -ErrorAction SilentlyContinue)
        $result=@{Name=$name;ExitCode=$exit;OriginalProcess=$old;OriginalStillAlive=$alive;FolderWindowsBefore=$folders;FolderWindowsAfter=[IconicFolders.ClientTests.ExplorerProbe]::FolderWindowCount();DesktopAfter=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()}
        $report.Stages+=@($result)
        Write-Host ($result|ConvertTo-Json -Compress)
        return $result
    }finally {if(!$job.Process.HasExited){$job.Process.Kill();[void]$job.Process.WaitForExit(5000)};$job.Process.Dispose()}
}
function Release-ComReferences($references) {
    foreach($reference in $references) {
        if($reference -and [Runtime.InteropServices.Marshal]::IsComObject($reference)) {
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($reference)
        }
    }
    [GC]::Collect();[GC]::WaitForPendingFinalizers();[GC]::Collect()
}
function Retain-ExplorerReferences {
    $parent=Join-Path $ProjectRoot 'sample'
    [void][IO.Directory]::CreateDirectory((Join-Path $parent 'Target'))
    $automation=New-Object -ComObject Shell.Application
    $automation.Explore($parent)
    $windows=$automation.Windows()
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    $view=$null
    do {
        foreach($candidate in @($windows)) {
            try {if($candidate.Document.Folder.Self.Path-eq$parent){$view=$candidate;break}}catch{}
        }
        if($view){break};Start-Sleep -Milliseconds 250
    }while([DateTime]::UtcNow-lt$deadline)
    Assert-True ($null-ne$view) 'The diagnostic Explorer folder did not open.'
    $document=$view.Document
    $folder=$document.Folder
    $item=$folder.ParseName('Target')
    $verbs=$item.Verbs()
    # Unary comma prevents PowerShell from enumerating the COM collection.
    return ,@($verbs,$item,$folder,$document,$view,$windows,$automation)
}
try {
    $selection=Get-Content -LiteralPath (Join-Path $ProjectRoot 'selection.json') -Raw | ConvertFrom-Json
    $manifest=Get-Content -LiteralPath (Join-Path $ProjectRoot 'manifest.json') -Raw | ConvertFrom-Json
    Assert-True ($Prepared -and $env:COMPUTERNAME-ne$selection.HostMachine -and $env:COMPUTERNAME-eq$manifest.GuestMachine) 'Guest-only test; refusing execution on the development host.'
    Assert-True (![Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) 'The guest test must use the limited interactive token.'
    [void][Reflection.Assembly]::LoadFrom((Join-Path $ProjectRoot 'payload\ExplorerProbe.dll'))
    $report.Status='FAIL'
    foreach($file in Get-ChildItem (Join-Path $ProjectRoot 'payload') -File) {
        $report.Artifacts+=@{File=$file.Name;Sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash;Bytes=$file.Length}
    }
    if($selection.IconicScenario-in@('Compatibility','AllUsers')) {
        Recover-GuestDesktop
        if($selection.IconicScenario-eq'Compatibility'){$installed=$true}
        $script=if($selection.IconicScenario-eq'Compatibility'){'Test-CompatibilityVM.ps1'}else{'Test-AllUsersVM.ps1'}
        if($selection.IconicScenario-eq'AllUsers'){
            & (Join-Path $PSScriptRoot $script) -ProjectRoot $ProjectRoot -RunDirectory $RunDirectory -ResumeFreshInstallation:([bool]$selection.IconicResumeAllUsers)
        }else{
            & (Join-Path $PSScriptRoot $script) -ProjectRoot $ProjectRoot -RunDirectory $RunDirectory
        }
        Watch-Desktop ('after-'+$selection.IconicScenario) 10
        $report.Status='PASS'
        return
    }
    if($selection.IconicScenario-eq'GUI') {
        Recover-GuestDesktop
        $suiteFailures=@()
        $job=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\SelectorTests.exe') -Arguments '--one' -OutputPath (Join-Path $RunDirectory 'single-selector.stdout.log') -ErrorPath (Join-Path $RunDirectory 'single-selector.stderr.log')
        try {
            if(!$job.Process.WaitForExit(30000)){throw 'Single native selector timed out.'}
            $exit=Complete-RecordedProcess $job;Assert-True ($exit-eq0) ('Single native selector failed: '+$exit)
        }finally{if(!$job.Process.HasExited){$job.Process.Kill();[void]$job.Process.WaitForExit(5000)};if(!(Test-Path $job.OutputPath)){[void](Complete-RecordedProcess $job)};$job.Process.Dispose()}
        foreach($suite in @('SelectorTests','PickerTests','IconicFoldersTests')) {
            $job=Start-RecordedProcess -Executable (Join-Path $ProjectRoot ('payload\'+$suite+'.exe')) -Arguments '' -OutputPath (Join-Path $RunDirectory ($suite+'.stdout.log')) -ErrorPath (Join-Path $RunDirectory ($suite+'.stderr.log'))
            try {
                if(!$job.Process.WaitForExit(120000)){throw ($suite+' timed out.')}
                $exit=Complete-RecordedProcess $job
                if($exit-ne0){$suiteFailures+=@($suite+' failed: '+$exit)}
                $report.Stages+=@{Name=$suite;ExitCode=$exit}
            }finally{if(!$job.Process.HasExited){$job.Process.Kill();[void]$job.Process.WaitForExit(5000)};$job.Process.Dispose()}
        }
        $job=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\NativeAppTests.exe') -Arguments ('"'+(Join-Path $RunDirectory 'native-fixtures')+'"') -OutputPath (Join-Path $RunDirectory 'native-app.stdout.log') -ErrorPath (Join-Path $RunDirectory 'native-app.stderr.log')
        try {
            if(!$job.Process.WaitForExit(240000)){throw 'Native app suite timed out.'}
            $exit=Complete-RecordedProcess $job
            Assert-True ($exit-eq0) ('Native application flows failed: '+$exit)
        }finally{if(!$job.Process.HasExited){$job.Process.Kill();[void]$job.Process.WaitForExit(5000)};$job.Process.Dispose()}
        Watch-Desktop 'after-native-application-flows' 10
        Assert-True ($suiteFailures.Count-eq0) ($suiteFailures-join'; ')
        $report.Status='PASS'
        return
    }
    if($selection.IconicScenario-eq'CrashControl') {
        Recover-GuestDesktop
        for($attempt=1;$attempt-le5;$attempt++) {
            Open-FolderMenu ('control-activation-'+$attempt) $false 0
            $desktop=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
            [IconicFolders.ClientTests.ExplorerProbe]::TerminateTestDesktop($desktop,0xC0000005L)
            $report.Stages+=@{Name='control-injected-exit';Attempt=$attempt;ProcessId=$desktop}
            $deadline=[DateTime]::UtcNow.AddSeconds(20)
            while([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-eq0 -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 100}
            Assert-True ([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-ne0) 'Winlogon did not recover the control desktop.'
        }
        Watch-Desktop 'control-recovered-desktop' 25
        $report.Status='PASS'
        return
    }
    if($selection.IconicScenario-eq'Safety') {
        Recover-GuestDesktop
        $installed=$true
        Start-Wait (Join-Path $ProjectRoot 'payload\current-Setup.exe') '/S'
        $safety='Registry::HKEY_CURRENT_USER\Software\IconicFolders\Safety'
        for($attempt=1;$attempt-le5;$attempt++) {
            # GetShellWindow can appear before the replacement shell has
            # finished starting. Do not issue COM navigation during that phase.
            Watch-Desktop ('guard-ready-'+$attempt) 3
            Open-FolderMenu ('guard-activation-'+$attempt) $true 0
            $desktop=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
            $deadline=[DateTime]::UtcNow.AddSeconds(4)
            do {
                $workers=@(Get-Process IconicFolders -ErrorAction SilentlyContinue | Where-Object {$_.Path-ieq(Join-Path $root 'IconicFolders.exe')})
                if($workers.Count){break};Start-Sleep -Milliseconds 100
            }while([DateTime]::UtcNow-lt$deadline)
            Assert-True ($workers.Count-eq1) 'Expected one short-lived safety observer in the guest.'
            $report.Stages+=@{Name='before-injected-exit';Attempt=$attempt;Processes=@(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" | Select-Object ProcessId,ParentProcessId,CreationDate,CommandLine)}
            [IconicFolders.ClientTests.ExplorerProbe]::TerminateTestDesktop($desktop,0xC0000005L)
            $deadline=[DateTime]::UtcNow.AddSeconds(10)
            do {
                $state=Get-ItemProperty -LiteralPath $safety -ErrorAction SilentlyContinue
                if($state -and $state.Failures-eq$attempt){break};Start-Sleep -Milliseconds 100
            }while([DateTime]::UtcNow-lt$deadline)
            Assert-True ($state -and $state.Failures-eq$attempt) ('The observer did not count early abnormal exit '+$attempt)
            Assert-True ($state.Disabled-eq[int]($attempt-eq5)) 'The extension was disabled at the wrong threshold.'
            $report.Stages+=@{Name='injected-early-exit';Attempt=$attempt;ProcessId=$desktop;ExitCode='0xC0000005';Failures=$state.Failures;Disabled=$state.Disabled}
            $deadline=[DateTime]::UtcNow.AddSeconds(20)
            while([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-eq0 -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 100}
            Assert-True ([IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()-ne0) 'Winlogon did not recover the test desktop.'
        }
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        do {
            $notices=@(Get-Process IconicFolders -ErrorAction SilentlyContinue | Where-Object {$_.Path-ieq(Join-Path $root 'IconicFolders.exe') -and $_.MainWindowHandle-ne0})
            if($notices.Count){break};Start-Sleep -Milliseconds 100
        }while([DateTime]::UtcNow-lt$deadline)
        Assert-True ($notices.Count-eq1) 'The safety shutdown must show exactly one error dialog.'
        $factory=[IconicFolders.ClientTests.ExplorerProbe]::ProbeFactory((Join-Path $root 'IconicFoldersShell.dll'))
        Assert-True ($factory-eq0x80040111L) 'The disabled extension still created a COM factory.'
        $report.Stages+=@{Name='guard-blocked-activation';FactoryResult=('0x{0:X8}'-f$factory);ErrorDialogs=$notices.Count}
        foreach($notice in $notices){$notice.Kill();[void]$notice.WaitForExit(5000)}
        Start-Wait (Join-Path $ProjectRoot 'payload\RestartProbe.exe') '--reset-safety'
        $state=Get-ItemProperty -LiteralPath $safety
        Assert-True ($state.Disabled-eq0 -and $state.Failures-eq0) 'Explicit reactivation did not reset safety state.'
        Assert-True ([IconicFolders.ClientTests.ExplorerProbe]::ProbeFactory((Join-Path $root 'IconicFoldersShell.dll'))-eq0) 'Explicit reactivation did not restore the COM factory.'
        $report.Stages+=@{Name='guard-explicit-reactivation';Disabled=$state.Disabled;Failures=$state.Failures}
        Watch-Desktop 'guard-recovered-desktop' 25
        $report.Status='PASS'
        return
    }
    if($selection.IconicScenario-eq'Diagnose') {
        Recover-GuestDesktop
        Assert-True ([IconicFolders.ClientTests.ExplorerProbe]::FolderWindowCount()-eq0) 'The fresh control must have no folder window.'
        $empty=Measure-GracefulShutdown '01-no-folder-no-com'
        Recover-GuestDesktop
        $parent=Join-Path $ProjectRoot 'sample'
        [void][IO.Directory]::CreateDirectory($parent)
        Start-Process (Join-Path $env:WINDIR 'explorer.exe') -ArgumentList ('"'+$parent+'"') -WindowStyle Hidden
        Start-Sleep -Seconds 5
        Assert-True ([IconicFolders.ClientTests.ExplorerProbe]::FolderWindowCount()-gt0) 'The native control folder window did not open.'
        $native=Measure-GracefulShutdown '02-folder-no-com'
        Recover-GuestDesktop
        $references=Retain-ExplorerReferences
        $retained=Measure-GracefulShutdown '03-folder-com-retained'
        $releaseStarted=Get-Date
        Release-ComReferences $references
        $references=$null
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while((Get-Process -Id $retained.OriginalProcess -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 250}
        $report.Stages+=@{Name='04-after-com-release';OriginalStillAlive=($null-ne(Get-Process -Id $retained.OriginalProcess -ErrorAction SilentlyContinue));ReleaseSeconds=((Get-Date)-$releaseStarted).TotalSeconds}
        Recover-GuestDesktop
        $references=Retain-ExplorerReferences
        Release-ComReferences $references
        $references=$null
        [GC]::Collect();[GC]::WaitForPendingFinalizers();[GC]::Collect()
        $released=Measure-GracefulShutdown '05-folder-com-released-first'
        Recover-GuestDesktop
        $report.Status='PASS'
        $report.DiagnosticOnly=$true
        return
    }
    if($selection.IconicScenario-in@('Recovery','Control')) {
        Recover-GuestDesktop
        if($selection.IconicScenario-eq'Control') {
            Assert-True (!(Test-Path -LiteralPath $menu) -and !(Test-Path -LiteralPath $com)) 'The control must have no extension registration.'
            Open-FolderMenu 'control-no-extension-menu' $false
            Restart-Desktop 'control-no-extension-restart'
        }
        $report.Status='PASS'
        return
    }
    Assert-True (!(Test-Path -LiteralPath $install) -and !(Test-Path -LiteralPath $root) -and !(Test-Path -LiteralPath $menu) -and !(Test-Path -LiteralPath $com)) 'An existing guest IconicFolders installation will not be overwritten.'
    Watch-Desktop 'baseline' 10
    $installed=$true
    Start-Wait (Join-Path $ProjectRoot 'payload\previous-Setup.exe') '/S'
    Watch-Desktop 'previous-installation' 10
    Restart-Desktop 'previous-version-restart'
    Open-FolderMenu 'previous-menu-loaded'
    Start-Wait (Join-Path $ProjectRoot 'payload\current-Setup.exe') '/S'
    Watch-Desktop 'upgrade-with-loaded-dll'
    Restart-Desktop 'after-upgrade-restart'
    Open-FolderMenu 'current-menu-loaded'
    $integration=Start-RecordedProcess -Executable (Join-Path $ProjectRoot 'payload\IconicFoldersTests.exe') -Arguments '--registered' -OutputPath (Join-Path $RunDirectory 'registered.stdout.log') -ErrorPath (Join-Path $RunDirectory 'registered.stderr.log')
    try{if(!$integration.Process.WaitForExit(30000)){throw 'Registered integration test timed out.'};$exit=Complete-RecordedProcess $integration;Assert-True ($exit-eq0) ('Registered integration test failed: '+$exit)}finally{if(!$integration.Process.HasExited){$integration.Process.Kill();[void]$integration.Process.WaitForExit(5000)};$integration.Process.Dispose()}
    $report.Stages+=@{Name='registered-COM-menu-and-two-Unicode-folder-helper-reset';Status='PASS'}
    Start-Wait (Join-Path $ProjectRoot 'payload\current-Setup.exe') '/S'
    Watch-Desktop 'repeated-upgrade-with-loaded-dll'
    Restart-Desktop 'after-repeated-upgrade-restart'
    Open-FolderMenu 'repeated-upgrade-menu-loaded'
    Restart-Desktop 'second-current-version-restart'
    Restart-Desktop 'third-current-version-restart'
    $report.Status='PASS'
}catch {
    $report.Error=$_.Exception.Message
    $report.ErrorLocation=$_.ScriptStackTrace
    $report.ExplorerAtFailure=@(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" | Select-Object ProcessId,ParentProcessId,CreationDate,CommandLine)
    Write-Host ('FAIL '+$report.Error)
}finally {
    if($selection.IconicScenario-in@('Control','CrashControl','Diagnose') -and $report.Status-ne'PASS') {
        $report.CrashesBeforeRecovery=@(Get-ExplorerCrashes)
        try{Recover-GuestDesktop}catch{$report.CleanupErrors+=@($_.Exception.Message)}
    }
    if($installed) {
        try {
            # Disable activation before cleanup, including after a crashing test.
            foreach($key in @($menu,$com)) {if(Test-Path -LiteralPath $key){Remove-Item -LiteralPath $key -Recurse -Force}}
            $uninstaller=Join-Path $root 'Uninstall.exe'
            if(Test-Path -LiteralPath $uninstaller){
                Start-Wait $uninstaller '/S'
                Wait-InstalledUninstaller
            }
            Restart-Desktop 'cleanup-restart'
            if(Test-Path -LiteralPath $uninstaller){
                Start-Wait $uninstaller '/S'
                Wait-InstalledUninstaller
                $deadline=[DateTime]::UtcNow.AddSeconds(15)
                while((Test-Path -LiteralPath $uninstaller) -and [DateTime]::UtcNow-lt$deadline){Start-Sleep -Milliseconds 100}
                Assert-True (!(Test-Path -LiteralPath $uninstaller)) 'The test uninstaller did not remove itself.'
            }
            Assert-True (!(Test-Path -LiteralPath $menu) -and !(Test-Path -LiteralPath $com)) 'Guest extension registration was not removed.'
        }catch {$report.CleanupErrors+=@($_.Exception.Message);$report.Status='FAIL'}
    }
    $report.Crashes=@(Get-ExplorerCrashes)
    if($report.Crashes.Count){$report.Status='FAIL'}
    $report.CompletedUtc=[DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $RunDirectory 'explorer-lifecycle.json') -Encoding UTF8
    @{Status=$report.Status;Total=1;Passed=[int]($report.Status-eq'PASS');Failed=[int]($report.Status-ne'PASS');Error=$report.Error} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $RunDirectory 'summary.json') -Encoding UTF8
}
if($report.Status-ne'PASS'){exit 1}
exit 0
