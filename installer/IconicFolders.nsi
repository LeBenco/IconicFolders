; SPDX-License-Identifier: GPL-3.0-only
; Copyright (C) 2026 Benco
; Presentation and scope/elevation protocol adapted from FileSpacer.
Unicode True
!include MUI2.nsh
!include LogicLib.nsh
!include WinVer.nsh
!include FileFunc.nsh
!include x64.nsh
!include Sections.nsh
!ifndef REGISTRY_PREFIX
 !define REGISTRY_PREFIX ""
!endif
!ifndef SETUP_OUTPUT
 !define SETUP_OUTPUT "..\dist\IconicFolders-1.1.0-x64-Setup.exe"
!endif
!ifndef SETUP_MUTEX
 !define SETUP_MUTEX "Global\IconicFolders.Setup"
!endif
!define SETTINGS "${REGISTRY_PREFIX}Software\IconicFolders"
!define CLASSES "${REGISTRY_PREFIX}Software\Classes"
!define RUNONCE "${REGISTRY_PREFIX}Software\Microsoft\Windows\CurrentVersion\RunOnce"
!define UNINSTALL "${REGISTRY_PREFIX}Software\Microsoft\Windows\CurrentVersion\Uninstall\IconicFolders"
!define CLSID "{75F4D684-8623-4CE1-B3B2-464EF177FDDE}"
!define PRODUCT_VERSION "1.1.0"
!define EXIT_SCOPE 4
!define EXIT_FILES 5
!define EXIT_REGISTRY 6
!define EXIT_CONTEXT 8
!define EXIT_OPTIONS 9
Name "IconicFolders"
OutFile "${SETUP_OUTPUT}"
InstallDir ""
RequestExecutionLevel user
SetCompressor /SOLID lzma
AllowSkipFiles off
ManifestDPIAware True
ManifestSupportedOS Win10
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "1.1.0.4"
VIAddVersionKey /LANG=1033 "ProductName" "IconicFolders"
VIAddVersionKey /LANG=1033 "FileDescription" "IconicFolders installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) 2026 Benco"
BrandingText "IconicFolders ${PRODUCT_VERSION} (x64)"
!define MUI_ICON "..\assets\icon.ico"
!define MUI_UNICON "..\assets\icon.ico"
!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT Rollback
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_TEXTCOLOR "20252E"
!define MUI_BGCOLOR "FFFFFF"
!define MUI_CUSTOMFUNCTION_GUIINIT SetWizardArtwork
!define MUI_CUSTOMFUNCTION_UNGUIINIT un.SetWizardArtwork
Page Custom ScopePageShow ScopePageLeave
!define MUI_PAGE_CUSTOMFUNCTION_SHOW SetPagePalette
!define MUI_LICENSEPAGE_BGCOLOR "FFFFFF"
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW SetPagePalette
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_SHOW SetPagePalette
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_CUSTOMFUNCTION_SHOW SetPagePalette
!insertmacro MUI_PAGE_INSTFILES
Page Custom FinishPageShow FinishPageLeave
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.SetPagePalette
!insertmacro MUI_UNPAGE_CONFIRM
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.SetPagePalette
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Polish"
!insertmacro MUI_LANGUAGE "Dutch"
!insertmacro MUI_LANGUAGE "Ukrainian"
ChangeUI IDD_INST "..\build\installer-assets\wizard-ui.dll"
!include Languages.nsh

Var TransactionId
Var StageDirectory
Var TransactionActive
Var Publishing
Var RollbackFailed
Var CleanupId
Var CleanupPending
Var SetupMutex
Var FailureCode
Var Uninstalling
Var choicesScope
Var upgrading
Var installMode
Var parameters
Var requestedDirectory
Var machineWorker
Var delegated
Var scopeCurrent
Var scopeAll
Var notesText
Var elevatedParameters
Var elevatedResult
Var brandingBitmap
Var brandingTitleFont
Var brandingCaptionFont
Var titleFont
Var logPath
Var updateResult
Var installStep
Var restartChoice

!macro WORKER_LOG TEXT
    Push $R9
    Push $R8
    StrCpy $R8 0
    ${If} ${Errors}
        StrCpy $R8 1
    ${EndIf}
    ClearErrors
    DetailPrint "${TEXT}"
    ${If} $logPath != ""
        FileOpen $R9 "$logPath" a
        ${IfNot} ${Errors}
            FileSeek $R9 0 END
            FileWriteUTF16LE $R9 "${TEXT}$\r$\n"
            FileClose $R9
        ${Else}
            DetailPrint "Cannot open installation log: $logPath"
        ${EndIf}
    ${EndIf}
    ClearErrors
    ${If} $R8 == 1
        SetErrors
    ${EndIf}
    Pop $R8
    Pop $R9
!macroend

!macro INSTALL_STEP TEXT
    StrCpy $installStep "${TEXT}"
    !insertmacro WORKER_LOG "Starting: $installStep"
!macroend

!macro WORKER_EXIT CODE
    ${If} $machineWorker == 1
        SetErrorLevel ${CODE}
    ${Else}
        SetErrorLevel 2
    ${EndIf}
!macroend

!include Elevation.nsh
!macro AcquireSetupMutex
 System::Call 'kernel32::CreateMutexW(p 0, i 0, w "${SETUP_MUTEX}") p.r0 ?e'
 Pop $1
 StrCpy $SetupMutex $0
 StrCmp $0 0 setup_busy
 StrCmp $1 183 setup_busy setup_ready
 setup_busy:
 MessageBox MB_ICONSTOP "$(SetupBusy)" /SD IDOK
 SetErrorLevel 1
 Quit
 setup_ready:
!macroend

; Only these installer-owned files participate in replacement and recovery.
!macro Payload Action
 !insertmacro ${Action} "IconicFoldersShell.dll"
 !insertmacro ${Action} "IconicFolders.exe"
 !insertmacro ${Action} "README.md"
 !insertmacro ${Action} "LICENSE.txt"
 !insertmacro ${Action} "WIL-LICENSE.txt"
 !insertmacro ${Action} "WIL-THIRD-PARTY-NOTICES.txt"
 !insertmacro ${Action} "MICROSOFT-FLUENT-EMOJI-LICENSE.txt"
 !insertmacro ${Action} "Uninstall.exe"
 !insertmacro ${Action} "install-info.ini"
!macroend
!macro BackupFile Name
 IfFileExists "$INSTDIR\${Name}" 0 backup_done_${Name}
 ClearErrors
 Rename "$INSTDIR\${Name}" "$INSTDIR\${Name}.backup-$TransactionId"
 IfErrors install_failed
 backup_done_${Name}:
!macroend
!macro PublishFile Name
 ClearErrors
 Rename "$StageDirectory\${Name}" "$INSTDIR\${Name}"
 IfErrors install_failed
!macroend
!macro RestoreFile Name
 StrCmp $Publishing 1 0 restore_old_${Name}
 IfFileExists "$StageDirectory\${Name}" restore_old_${Name}
 ClearErrors
 Rename "$INSTDIR\${Name}" "$StageDirectory\${Name}"
 IfErrors 0 restore_old_${Name}
 StrCpy $RollbackFailed 1
 restore_old_${Name}:
 IfFileExists "$INSTDIR\${Name}.backup-$TransactionId" 0 restore_done_${Name}
 ClearErrors
 Rename "$INSTDIR\${Name}.backup-$TransactionId" "$INSTDIR\${Name}"
 IfErrors 0 restore_done_${Name}
 StrCpy $RollbackFailed 1
 restore_done_${Name}:
!macroend
!macro DeleteCommittedFile Name
 Delete "$INSTDIR\${Name}.backup-$CleanupId"
 IfFileExists "$INSTDIR\${Name}.backup-$CleanupId" 0 +2
 StrCpy $9 1
 ${If} $installMode == "AllUsers"
  Delete /REBOOTOK "$INSTDIR\${Name}.backup-$CleanupId"
 ${EndIf}
!macroend
!macro CleanupFunction Prefix
 Function ${Prefix}CleanupCommitted
  StrCpy $CleanupPending 0
  FindFirst $R0 $R1 "$INSTDIR\.upgrade-*.committed"
  cleanup_loop:
  StrCmp $R1 "" cleanup_end
  ; FindFirst returns a basename, never an arbitrary path. An exact fixed-size
  ; transaction marker authorizes deletion of this committed backup set only.
  StrCpy $CleanupId $R1 38 9
  StrCmp $R1 ".upgrade-$CleanupId.committed" 0 cleanup_next
  StrCpy $9 0
  !insertmacro Payload DeleteCommittedFile
  StrCmp $9 0 0 cleanup_pending
  Delete "$INSTDIR\$R1"
  Goto cleanup_next
  cleanup_pending:
  StrCpy $CleanupPending 1
  ${If} $installMode == "AllUsers"
  ${AndIf} $Uninstalling == 1
   Delete /REBOOTOK "$INSTDIR\$R1"
  ${EndIf}
  cleanup_next:
  FindNext $R0 $R1
  Goto cleanup_loop
  cleanup_end:
  FindClose $R0
  StrCmp $CleanupPending 0 0 cleanup_schedule
  DeleteRegValue SHCTX "${RUNONCE}" "IconicFoldersUpgradeCleanup"
  Return
  cleanup_schedule:
  ${If} $installMode == "AllUsers"
   Return ; The elevated worker already scheduled its unique backup names.
  ${EndIf}
  WriteRegStr SHCTX "${RUNONCE}" "IconicFoldersUpgradeCleanup" '$\"$INSTDIR\Uninstall.exe$\" /S /UPGRADE_CLEANUP'
 FunctionEnd
!macroend
!insertmacro CleanupFunction ""
!insertmacro CleanupFunction "un."
Function Rollback
 StrCmp $TransactionActive 1 0 rollback_done
 StrCpy $RollbackFailed 0
 Delete "$INSTDIR\.upgrade-$TransactionId.committed"
 !insertmacro Payload RestoreFile
 StrCpy $TransactionActive 0
 rollback_done:
 SetOutPath "$TEMP"
 StrCmp $StageDirectory "" +2
 RMDir /r "$StageDirectory"
FunctionEnd
Function .onInstFailed
 Call Rollback
FunctionEnd
Function RestartExplorer
 ClearErrors
 Exec '$\"$INSTDIR\IconicFolders.exe$\" --restart-explorer'
 IfErrors 0 restart_cleanup
 MessageBox MB_ICONINFORMATION "$(RestartLaunchFailed)" /SD IDOK
 restart_cleanup:
 Call CleanupCommitted
FunctionEnd


Function .onInit
!ifdef LANGUAGE_TEST_REPORT
    ; Compile-time test only: verify the strings stored in the real NSIS image.
    StrCpy $LANGUAGE ${LANG_FRENCH}
    FileOpen $0 "${LANGUAGE_TEST_REPORT}" w
    FileWriteUTF16LE /BOM $0 "$(SCOPE_TITLE)$\r$\n$(FINISH_TEXT)$\r$\n$(RestartExplorerText)$\r$\n"
    FileClose $0
    SetErrorLevel 0
    Quit
!endif
    ${IfNot} ${IsNativeAMD64}
    ${OrIfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP $(ERROR_OS) /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
    SetRegView 64
    Call InitializeInstall
    InitPluginsDir
    ${If} $LANGUAGE == ${LANG_FRENCH}
        File /oname=$PLUGINSDIR\changelog.txt "..\build\installer-assets\changelog-fr.txt"
    ${ElseIf} $LANGUAGE == ${LANG_POLISH}
        File /oname=$PLUGINSDIR\changelog.txt "..\build\installer-assets\changelog-pl.txt"
    ${ElseIf} $LANGUAGE == ${LANG_DUTCH}
        File /oname=$PLUGINSDIR\changelog.txt "..\build\installer-assets\changelog-nl.txt"
    ${ElseIf} $LANGUAGE == ${LANG_UKRAINIAN}
        File /oname=$PLUGINSDIR\changelog.txt "..\build\installer-assets\changelog-uk.txt"
    ${Else}
        File /oname=$PLUGINSDIR\changelog.txt "..\build\installer-assets\changelog-en.txt"
    ${EndIf}
    ${If} $machineWorker != 1
        !insertmacro AcquireSetupMutex
    ${EndIf}
FunctionEnd

Section "IconicFolders" SecBase
 SectionIn RO
    !insertmacro WORKER_LOG "Setup context: version=${PRODUCT_VERSION}; scope=$installMode; worker=$machineWorker; language=$LANGUAGE; directory=$INSTDIR; temp=$TEMP; plugins=$PLUGINSDIR; result=$updateResult"
    Call LogInstallComponents
    ClearErrors
    ${GetFileVersion} "$INSTDIR\IconicFolders.exe" $0
    ${If} ${Errors}
        StrCpy $0 "unavailable (missing file, unreadable file or absent version resource)"
    ${EndIf}
    !insertmacro WORKER_LOG "Existing executable version: $0"
    ClearErrors
    ; Prevent parallel installations from masking each other's COM registration.
    ; The parent checks the initiating user's HKCU, not an alternate admin's.
    ${If} $machineWorker != 1
        ${If} $installMode == "AllUsers"
            ReadRegStr $0 HKCU "${SETTINGS}\Install" "Path"
        ${Else}
            ReadRegStr $0 HKLM "${SETTINGS}\Install" "Path"
        ${EndIf}
        ${If} $0 != ""
            !insertmacro WORKER_LOG "ERROR: conflicting installation in the other scope: $0"
            MessageBox MB_OK|MB_ICONSTOP $(ERROR_SCOPE) /SD IDOK
            !insertmacro WORKER_EXIT ${EXIT_SCOPE}
            Abort
        ${EndIf}
    ${EndIf}
    ReadRegStr $1 SHCTX "${SETTINGS}\Install" "Path"
    ${If} $1 != ""
    ${AndIf} $1 != $INSTDIR
        !insertmacro WORKER_LOG "An installation already exists at $1, not at $INSTDIR."
        MessageBox MB_OK|MB_ICONSTOP $(ERROR_SCOPE) /SD IDOK
        !insertmacro WORKER_EXIT ${EXIT_SCOPE}
        Abort
    ${EndIf}
    ${If} $installMode == "AllUsers"
    ${AndIf} $machineWorker != 1
        StrCpy $delegated 1
        Call InstallForAllUsers
        ${If} $elevatedResult != 0
            ${If} $elevatedResult == 1223
                StrCpy $elevatedResult 1 ; Match NSIS and the updater's cancellation code.
            ${Else}
                Call ExplainWorkerFailure
            ${EndIf}
            SetErrorLevel $elevatedResult
            Abort
        ${EndIf}
        Goto install_done
    ${EndIf}

 SetRegView 64
 System::Call 'ole32::CoCreateGuid(g .r0) i.r1'
 StrCmp $1 0 0 install_failed
 StrCpy $TransactionId $0
 StrCpy $StageDirectory "$INSTDIR\.install-$TransactionId"
 !insertmacro INSTALL_STEP "Stage program files: $StageDirectory"
 SetOutPath "$StageDirectory"
 SetOverwrite on
 File "..\build\Release\IconicFolders.exe"
 File "..\build\Release\IconicFoldersShell.dll"
 File /oname=README.md "..\README.md"
 File /oname=LICENSE.txt "..\LICENSE"
 File /oname=WIL-LICENSE.txt "..\external\wil\LICENSE"
 File /oname=WIL-THIRD-PARTY-NOTICES.txt "..\external\wil\ThirdPartyNotices.txt"
 File /oname=MICROSOFT-FLUENT-EMOJI-LICENSE.txt "..\assets\third-party\LICENSE-Microsoft-Fluent-Emoji.txt"
 ClearErrors
 WriteUninstaller "$StageDirectory\Uninstall.exe"
 IfErrors install_failed
 WriteINIStr "$StageDirectory\install-info.ini" "Installation" "Mode" $installMode
 WriteINIStr "$StageDirectory\install-info.ini" "Installation" "Language" $LANGUAGE
 IfErrors install_failed
 SetOutPath "$INSTDIR"
 StrCpy $TransactionActive 1
 StrCpy $Publishing 0
 !insertmacro INSTALL_STEP "Replace program files: $INSTDIR"
 !insertmacro Payload BackupFile
 StrCpy $Publishing 1
 !insertmacro Payload PublishFile

 ClearErrors
 !insertmacro INSTALL_STEP "Register installation: $installMode"
 WriteRegStr SHCTX "${SETTINGS}\Install" "Path" "$INSTDIR"
 WriteRegStr SHCTX "${SETTINGS}\Install" "Mode" $installMode
 WriteRegStr SHCTX "${UNINSTALL}" "DisplayName" "IconicFolders"
 WriteRegStr SHCTX "${UNINSTALL}" "DisplayVersion" "${PRODUCT_VERSION}"
 WriteRegStr SHCTX "${UNINSTALL}" "Publisher" "Benco"
 WriteRegStr SHCTX "${UNINSTALL}" "DisplayIcon" '"$INSTDIR\IconicFolders.exe",0'
 WriteRegStr SHCTX "${UNINSTALL}" "InstallLocation" "$INSTDIR"
 WriteRegStr SHCTX "${UNINSTALL}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\" /$installMode'
 WriteRegStr SHCTX "${UNINSTALL}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /$installMode /S'
 WriteRegDWORD SHCTX "${UNINSTALL}" "NoModify" 1
 WriteRegDWORD SHCTX "${UNINSTALL}" "NoRepair" 1
 IfErrors registry_failed registration_ok
 registry_failed:
 StrCpy $FailureCode ${EXIT_REGISTRY}
 Call FailInstall
 registration_ok:
 Goto install_done
 install_failed:
 StrCpy $FailureCode ${EXIT_FILES}
 Call FailInstall
 install_done:
SectionEnd



Section "$(SEC_CONTEXT)" SecContext
 ${If} $delegated == 1
  Goto context_done
 ${EndIf}
 ClearErrors
 WriteRegStr SHCTX "${CLASSES}\CLSID\${CLSID}\InprocServer32" "" "$INSTDIR\IconicFoldersShell.dll"
 WriteRegStr SHCTX "${CLASSES}\CLSID\${CLSID}\InprocServer32" "ThreadingModel" "Apartment"
 WriteRegStr SHCTX "${CLASSES}\Directory\shell\IconicFolders" "MUIVerb" "@$INSTDIR\IconicFoldersShell.dll,-100"
 WriteRegStr SHCTX "${CLASSES}\Directory\shell\IconicFolders" "ExplorerCommandHandler" "${CLSID}"
 WriteRegStr SHCTX "${CLASSES}\Directory\shell\IconicFolders" "MultiSelectModel" "Player"
 ${If} ${Errors}
  StrCpy $FailureCode ${EXIT_CONTEXT}
  Call FailInstall
 ${EndIf}
 context_done:
SectionEnd

Section -ApplyOptions
 ${If} $delegated == 1
  Goto options_parent
 ${EndIf}
 ${IfNot} ${SectionIsSelected} ${SecContext}
  ReadRegStr $0 SHCTX "${CLASSES}\Directory\shell\IconicFolders" "ExplorerCommandHandler"
  ${If} $0 != ""
   ClearErrors
   DeleteRegKey SHCTX "${CLASSES}\Directory\shell\IconicFolders"
   ${If} ${Errors}
    StrCpy $FailureCode ${EXIT_CONTEXT}
    Call FailInstall
   ${EndIf}
  ${EndIf}
  ReadRegStr $0 SHCTX "${CLASSES}\CLSID\${CLSID}\InprocServer32" ""
  ${If} $0 != ""
   ClearErrors
   DeleteRegKey SHCTX "${CLASSES}\CLSID\${CLSID}"
   ${If} ${Errors}
    StrCpy $FailureCode ${EXIT_CONTEXT}
    Call FailInstall
   ${EndIf}
  ${EndIf}
 ${EndIf}
 ClearErrors
 SectionGetFlags ${SecContext} $0
 IntOp $0 $0 & ${SF_SELECTED}
 WriteINIStr "$INSTDIR\install-info.ini" "Components" "ContextMenu" $0
 ${If} ${Errors}
  StrCpy $FailureCode ${EXIT_OPTIONS}
  Call FailInstall
 ${EndIf}
 FileOpen $0 "$INSTDIR\.upgrade-$TransactionId.committed" w
 ${If} ${Errors}
  StrCpy $FailureCode ${EXIT_FILES}
  Call FailInstall
 ${EndIf}
 FileClose $0
 StrCpy $TransactionActive 0
 RMDir "$StageDirectory"
 DeleteRegValue SHCTX "${RUNONCE}" "IconicFoldersCleanup"
 Call CleanupCommitted
 System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
 options_parent:
 Call WriteUpdateResult
SectionEnd

Function LogInstallComponents
 Push $0
 SectionGetFlags ${SecContext} $0
 IntOp $0 $0 & ${SF_SELECTED}
 !insertmacro WORKER_LOG "Components: ContextMenu=$0"
 Pop $0
FunctionEnd
Function FailInstall
 Call Rollback
 ${If} $RollbackFailed == 1
  MessageBox MB_ICONSTOP "$(RecoveryNeeded)" /SD IDOK
 ${Else}
  MessageBox MB_ICONSTOP "$(UpdateFailed)" /SD IDOK
 ${EndIf}
 !insertmacro WORKER_EXIT $FailureCode
 Abort
FunctionEnd

Function un.onInit
    SetRegView 64
    ReadINIStr $installMode "$INSTDIR\install-info.ini" "Installation" "Mode"
    ${If} $installMode == ""
        ReadRegStr $0 HKLM "${SETTINGS}\Install" "Path"
        ${If} $0 == $INSTDIR
            StrCpy $installMode "AllUsers"
        ${Else}
            ReadRegStr $0 HKCU "${SETTINGS}\Install" "Path"
            ${If} $0 == $INSTDIR
                StrCpy $installMode "CurrentUser" ; Legacy per-user installation.
            ${EndIf}
        ${EndIf}
    ${EndIf}
    ReadINIStr $1 "$INSTDIR\install-info.ini" "Installation" "Language"
    ${If} $1 == ${LANG_ENGLISH}
    ${OrIf} $1 == ${LANG_FRENCH}
    ${OrIf} $1 == ${LANG_POLISH}
    ${OrIf} $1 == ${LANG_DUTCH}
    ${OrIf} $1 == ${LANG_UKRAINIAN}
        StrCpy $LANGUAGE $1
    ${EndIf}
    ${If} $installMode == "AllUsers"
        UserInfo::GetAccountType
        Pop $0
        ${If} $0 != "Admin"
            ${un.GetParameters} $parameters
            ClearErrors
            ${un.GetOptionsS} $parameters "/MachineWorker" $0
            ${IfNot} ${Errors}
                SetErrorLevel 2 ; Never loop if an elevated worker lacks its token.
                Quit
            ${EndIf}
            StrCpy $elevatedParameters "/MachineWorker"
            ${If} ${Silent}
                StrCpy $elevatedParameters "$elevatedParameters /S"
            ${EndIf}
            ; This process is already NSIS's temporary uninstaller copy.
            StrCpy $elevatedParameters "$elevatedParameters _?=$INSTDIR"
            Call un.RunElevated
            ${If} $elevatedResult == 1223
                StrCpy $elevatedResult 1
            ${EndIf}
            SetErrorLevel $elevatedResult
            Quit
        ${EndIf}
        SetShellVarContext all
    ${ElseIf} $installMode == "CurrentUser"
        SetShellVarContext current
    ${Else}
        MessageBox MB_OK|MB_ICONSTOP $(ERROR_UNINSTALL) /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
    ${un.GetParameters} $0
    ClearErrors
    ${un.GetOptions} $0 "/UPGRADE_CLEANUP" $1
    ${IfNot} ${Errors}
        Call un.CleanupCommitted
        SetErrorLevel 0
        Quit
    ${EndIf}
    !insertmacro AcquireSetupMutex
FunctionEnd


; Never schedule deletion of canonical program paths: a later reinstall must
; not be removed at reboot. Only unique, installer-owned retired names qualify.
!macro RemoveMachineFile Name
 Delete "$INSTDIR\${Name}"
 IfFileExists "$INSTDIR\${Name}" 0 removed_${Name}
 ClearErrors
 Rename "$INSTDIR\${Name}" "$INSTDIR\${Name}.uninstall-$TransactionId"
 ${If} ${Errors}
  MessageBox MB_ICONSTOP "$(ERROR_UNINSTALL)" /SD IDOK
  SetErrorLevel 2
  Abort
 ${EndIf}
 Delete /REBOOTOK "$INSTDIR\${Name}.uninstall-$TransactionId"
 removed_${Name}:
!macroend

Section "Uninstall"
 SetRegView 64
 StrCpy $Uninstalling 1
 Call un.CleanupCommitted
 DeleteRegValue SHCTX "${RUNONCE}" "IconicFoldersUpgradeCleanup"
 DeleteRegValue SHCTX "${RUNONCE}" "IconicFoldersCleanup"
 DeleteRegKey SHCTX "${CLASSES}\Directory\shell\IconicFolders"
 DeleteRegKey SHCTX "${CLASSES}\CLSID\${CLSID}"
 DeleteRegKey SHCTX "${UNINSTALL}"
 DeleteRegKey SHCTX "${SETTINGS}"
 System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
 ${If} $installMode == "AllUsers"
  System::Call 'ole32::CoCreateGuid(g .r0) i.r1'
  ${If} $1 != 0
   SetErrorLevel 2
   Abort
  ${EndIf}
  StrCpy $TransactionId $0
  !insertmacro Payload RemoveMachineFile
  RMDir /REBOOTOK "$INSTDIR"
  Goto uninstall_done
 ${EndIf}
 Delete "$INSTDIR\IconicFolders.exe"
 Delete "$INSTDIR\IconicFoldersShell.dll"
 Delete "$INSTDIR\README.md"
 Delete "$INSTDIR\LICENSE.txt"
 Delete "$INSTDIR\WIL-LICENSE.txt"
 Delete "$INSTDIR\WIL-THIRD-PARTY-NOTICES.txt"
 Delete "$INSTDIR\MICROSOFT-FLUENT-EMOJI-LICENSE.txt"
 StrCmp $CleanupPending 1 deferred_cleanup
 IfFileExists "$INSTDIR\IconicFolders.exe" deferred_cleanup
 IfFileExists "$INSTDIR\IconicFoldersShell.dll" deferred_cleanup
 Delete "$INSTDIR\install-info.ini"
 Delete "$INSTDIR\Uninstall.exe"
 RMDir "$INSTDIR"
 Goto uninstall_done
 deferred_cleanup:
 ; MOVEFILE_DELAY_UNTIL_REBOOT requires administrator rights. A per-user
 ; RunOnce retries after sign-in, when the unregistered DLL is no longer loaded.
 WriteRegStr SHCTX "${RUNONCE}" "IconicFoldersCleanup" '$\"$INSTDIR\Uninstall.exe$\" /CurrentUser /S'
 uninstall_done:
SectionEnd


!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
 !insertmacro MUI_DESCRIPTION_TEXT ${SecBase} $(DESC_BASE)
 !insertmacro MUI_DESCRIPTION_TEXT ${SecContext} $(DESC_CONTEXT)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
!include Wizard.nsh
!include Scope.nsh
