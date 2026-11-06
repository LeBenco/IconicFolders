# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference='Stop'
if($env:COMPUTERNAME-ne'AGENTWIN10'){throw 'VM-only compatibility test.'}
$report=[ordered]@{Status='FAIL';Windows=[Environment]::OSVersion.Version.ToString();Stages=@();Error=$null;TranslucentTB=$null}
$existing=@(Get-AppxPackage '28017CharlesMilette.TranslucentTB')
if(@(Get-Process TranslucentTB -ErrorAction SilentlyContinue).Count){throw 'An existing TranslucentTB instance will not be replaced by the test.'}
$added=$false;$ttb=$null;$config=$null;$previous=$null;$hadConfig=$false;$helper=$null
try {
    if(!$existing.Count){
        $dependencies=@('vclibs-desktop-x64.appx','vclibs-uwp-x64.appx','winui-x64.appx') | ForEach-Object {Join-Path $ProjectRoot ('payload\'+$_)}
        Add-AppxPackage -Path (Join-Path $ProjectRoot 'payload\TranslucentTB-2026.2.msixbundle') -DependencyPath $dependencies
        $added=$true
    }
    $package=Get-AppxPackage '28017CharlesMilette.TranslucentTB';if(!$package){throw 'TranslucentTB registration missing.'}
    $report.TranslucentTB=$package.Version.ToString()
    $storage=Join-Path $env:LOCALAPPDATA ('Packages\'+$package.PackageFamilyName)
    $config=Join-Path $storage 'RoamingState\settings.json';$hadConfig=Test-Path -LiteralPath $config
    if($hadConfig){$previous=[IO.File]::ReadAllBytes($config)}
    [void][IO.Directory]::CreateDirectory((Split-Path $config))
    [IO.File]::WriteAllText($config,'{"verbosity":"trace","disable_saving":true}',[Text.UTF8Encoding]::new($false))
    [void][Reflection.Assembly]::LoadFrom((Join-Path $ProjectRoot 'payload\ExplorerProbe.dll'))
    $root=Join-Path $env:LOCALAPPDATA 'Programs\IconicFolders'
    $setup=Join-Path $ProjectRoot 'payload\current-Setup.exe'
    $install=Start-Process $setup -ArgumentList '/S /CurrentUser' -PassThru -WindowStyle Hidden
    if(!$install.WaitForExit(60000)){throw 'Compatibility fixture installation timed out.'};$install.Refresh();if($install.ExitCode-ne0){throw 'Compatibility fixture installation failed.'}
    # Context-menu registration becomes effective after Explorer restarts.
    # Establish that baseline before starting the application under test.
    Restart-Desktop 'compatibility-fixture-ready'
    $appPid=[IconicFolders.ClientTests.ExplorerProbe]::ActivateTestPackage(($package.PackageFamilyName+'!TranslucentTB'))
    $ttb=Get-Process -Id $appPid
    Start-Sleep -Seconds 10
    if($ttb.HasExited){throw 'TranslucentTB stopped during startup.'}
    for($attempt=1;$attempt-le2;$attempt++){
        # Load the real context-menu implementation before replacing its DLL.
        Open-FolderMenu ('compatibility-loaded-'+$attempt) $true 0
        $update=Start-Process $setup -ArgumentList '/S /CurrentUser' -PassThru -WindowStyle Hidden
        if(!$update.WaitForExit(60000)){throw 'Compatibility upgrade timed out.'};$update.Refresh();if($update.ExitCode-ne0){throw 'Compatibility upgrade failed.'}
        $before=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId();$clock=[Diagnostics.Stopwatch]::StartNew()
        # This is an interactive progress-window test, so show the helper UI.
        $helper=Start-Process (Join-Path $root 'IconicFolders.exe') -ArgumentList '--restart-explorer' -PassThru -WindowStyle Normal
        $responsive=$true;$observed=$false
        while(!$helper.WaitForExit(200)){
            $helper.Refresh();if($helper.MainWindowHandle-ne0){$observed=$true;$responsive=$responsive-and[IconicFolders.ClientTests.ExplorerProbe]::Responds($helper.MainWindowHandle)}
            if($clock.Elapsed.TotalSeconds-gt65){throw 'Explorer restart helper timed out.'}
        }
        if($helper.ExitCode-ne0){throw 'Explorer restart failed during compatibility test.'}
        $clock.Stop()
        if(!$observed-and$clock.Elapsed.TotalSeconds-gt2){throw 'The restart helper did not display its progress window.'}
        $after=[IconicFolders.ClientTests.ExplorerProbe]::DesktopProcessId()
        if(!$after-or$after-eq$before-or!$responsive){throw 'Explorer restart was ineffective or its progress window froze.'}
        # TTB intentionally exits after rapid repeated Explorer restarts. Keep
        # ordinary upgrade repetitions outside its documented 30-second guard.
        Start-Sleep -Seconds 35
        $ttb.Refresh();if($ttb.HasExited){throw 'TranslucentTB exited after the Explorer restart.'}
        if($ttb.MainWindowHandle-ne0){throw ('TranslucentTB displayed an unexpected window: '+$ttb.MainWindowTitle)}
        $report.Stages+=@{Name='loaded-upgrade-restart-with-translucenttb';Attempt=$attempt;Before=$before;After=$after;RestartSeconds=$clock.Elapsed.TotalSeconds;ProgressObserved=$observed;ProgressResponsive=$responsive;TranslucentTBProcessId=$ttb.Id}
    }
    $report.Status='PASS'
}catch {$report.Error=$_.Exception.Message}
finally {
    if($helper-and!$helper.HasExited){$helper.Kill();[void]$helper.WaitForExit(10000)}
    if($config){
        $logs=Join-Path (Split-Path (Split-Path $config)) 'TempState';if(Test-Path $logs){Copy-Item -LiteralPath $logs -Destination (Join-Path $RunDirectory 'TranslucentTB-logs') -Recurse}
        if($hadConfig){[IO.File]::WriteAllBytes($config,$previous)}elseif(Test-Path $config){Remove-Item -LiteralPath $config}
    }
    if($ttb-and!$ttb.HasExited){$ttb.Kill();[void]$ttb.WaitForExit(10000)}
    if($added){$package=Get-AppxPackage '28017CharlesMilette.TranslucentTB';if($package){Remove-AppxPackage $package.PackageFullName}}
    $report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $RunDirectory 'compatibility.json') -Encoding UTF8
}
if($report.Status-ne'PASS'){throw $report.Error}
