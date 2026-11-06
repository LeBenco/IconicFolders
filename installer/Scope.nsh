; SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
; FileSpacer scope/elevation protocol, adapted to IconicFolders payload/components.
!macro RESTORE_COMPONENT SECTION KEY
    ReadINIStr $0 "$INSTDIR\install-info.ini" "Components" "${KEY}"
    ${If} $0 == "1"
        !insertmacro SelectSection ${SECTION}
    ${ElseIf} $0 == "0"
        !insertmacro UnselectSection ${SECTION}
    ${EndIf}
!macroend

Function RestoreInstallChoices
    ${If} $choicesScope == $installMode
        Return
    ${EndIf}
    Push $0
    StrCpy $choicesScope $installMode
    StrCpy $upgrading 0
    !insertmacro SelectSection ${SecContext}
    ReadRegStr $0 SHCTX "${SETTINGS}\Install" "Path"
    ${If} $0 != ""
        StrCpy $upgrading 1
        ReadRegStr $0 SHCTX "${CLASSES}\Directory\shell\IconicFolders" "ExplorerCommandHandler"
        ${If} $0 != "${CLSID}"
            !insertmacro UnselectSection ${SecContext}
        ${EndIf}
        !insertmacro RESTORE_COMPONENT ${SecContext} "ContextMenu"
    ${EndIf}
    ClearErrors
    Pop $0
FunctionEnd

Function RestoreInstallLanguage
    ; LANGUAGE is changed only during .onInit, never while revisiting a page.
    ReadINIStr $0 "$INSTDIR\install-info.ini" "Installation" "Language"
    ${If} $0 == ${LANG_ENGLISH}
    ${OrIf} $0 == ${LANG_FRENCH}
    ${OrIf} $0 == ${LANG_POLISH}
    ${OrIf} $0 == ${LANG_DUTCH}
    ${OrIf} $0 == ${LANG_UKRAINIAN}
        StrCpy $LANGUAGE $0
    ${EndIf}
    ClearErrors
FunctionEnd

Function SetInstallScope
    ${If} $choicesScope == $installMode
        Return ; Also preserve an edited installation directory on Back/Next.
    ${EndIf}
    ${If} $installMode == "AllUsers"
        SetShellVarContext all
        StrCpy $INSTDIR "$PROGRAMFILES64\IconicFolders"
    ${Else}
        SetShellVarContext current
        GetKnownFolderPath $INSTDIR {5CD7AEE2-2219-4A67-B85D-6C9CE15660CB}
        ${If} $INSTDIR == ""
            StrCpy $INSTDIR "$LOCALAPPDATA\Programs"
        ${EndIf}
        StrCpy $INSTDIR "$INSTDIR\IconicFolders"
    ${EndIf}
    ReadRegStr $0 SHCTX "${SETTINGS}\Install" "Path"
    ${If} $0 != ""
        StrCpy $INSTDIR $0
    ${EndIf}
    Call RestoreInstallChoices
FunctionEnd

!macro WORKER_COMPONENT SECTION KEY
    ClearErrors
    ${GetOptionsS} $parameters "/${KEY}=" $0
    IfErrors worker_invalid
    ${If} $0 == "1"
        !insertmacro SelectSection ${SECTION}
    ${ElseIf} $0 == "0"
        !insertmacro UnselectSection ${SECTION}
    ${Else}
        Goto worker_invalid
    ${EndIf}
!macroend

Function InitializeInstall
    StrCpy $requestedDirectory $INSTDIR ; NSIS's optional /D=, kept last on the command line.
    ${GetParameters} $parameters
    ClearErrors
    ${GetOptionsS} $parameters "/Log=" $logPath
    ${If} ${Errors}
        StrCpy $logPath "$TEMP\IconicFolders-install.log"
    ${EndIf}
    ClearErrors
    ${GetOptionsS} $parameters "/MachineWorker" $0
    ${IfNot} ${Errors}
        StrCpy $machineWorker 1
        SetSilent silent
        ClearErrors
        ${GetOptionsS} $parameters "/Log=" $logPath
        ${If} ${Errors}
            StrCpy $logPath ""
        ${EndIf}
        ClearErrors
        !insertmacro WORKER_LOG "Elevated worker started with: $parameters"
        ${GetTime} "" "L" $R0 $R1 $R2 $R3 $R4 $R5 $R6
        !insertmacro WORKER_LOG "Worker local time: $R2-$R1-$R0 $R4:$R5:$R6"
        UserInfo::GetAccountType
        Pop $0
        !insertmacro WORKER_LOG "Machine worker token account type: $0"
        ${If} $0 != "Admin"
        ${OrIf} $INSTDIR == ""
            Goto worker_invalid
        ${EndIf}
        StrCpy $installMode "AllUsers"
        SetShellVarContext all
        ClearErrors
        ${GetOptionsS} $parameters "/AllUsers" $0
        IfErrors worker_invalid

        !insertmacro WORKER_COMPONENT ${SecContext} "ContextMenu"

        ClearErrors
        ${GetOptionsS} $parameters "/Language=" $0
        IfErrors worker_invalid
        ${If} $0 != ${LANG_ENGLISH}
        ${AndIf} $0 != ${LANG_FRENCH}
        ${AndIf} $0 != ${LANG_POLISH}
        ${AndIf} $0 != ${LANG_DUTCH}
        ${AndIf} $0 != ${LANG_UKRAINIAN}
            Goto worker_invalid
        ${EndIf}
        StrCpy $LANGUAGE $0
        Return
    ${EndIf}
    ClearErrors
    FileOpen $R9 "$logPath" w
    ${IfNot} ${Errors}
        FileWriteUTF16LE /BOM $R9 ""
        FileClose $R9
    ${EndIf}
    ClearErrors
    !insertmacro WORKER_LOG "Setup started: $EXEPATH; parameters=$parameters"
    ${GetTime} "" "L" $R0 $R1 $R2 $R3 $R4 $R5 $R6
    !insertmacro WORKER_LOG "Setup local time: $R2-$R1-$R0 $R4:$R5:$R6"
    ${GetOptionsS} $parameters "/UpdateResult=" $updateResult
    ${If} ${Errors}
        StrCpy $updateResult ""
    ${EndIf}
    ClearErrors
    StrCpy $installMode "CurrentUser"
    ReadRegStr $0 HKCU "${SETTINGS}\Install" "Path"
    ${If} $0 == ""
        ReadRegStr $0 HKLM "${SETTINGS}\Install" "Path"
        ${If} $0 != ""
            StrCpy $installMode "AllUsers"
        ${EndIf}
    ${EndIf}
    ClearErrors
    ${GetOptionsS} $parameters "/CurrentUser" $0
    ${IfNot} ${Errors}
        StrCpy $installMode "CurrentUser"
    ${EndIf}
    ClearErrors
    ${GetOptionsS} $parameters "/AllUsers" $0
    ${IfNot} ${Errors}
        StrCpy $installMode "AllUsers"
    ${EndIf}
    Call SetInstallScope
    ${If} $requestedDirectory != ""
        StrCpy $INSTDIR $requestedDirectory
        StrCpy $choicesScope ""
        Call RestoreInstallChoices
    ${EndIf}
    Call RestoreInstallLanguage
    Return
worker_invalid:
    !insertmacro WORKER_LOG "ERROR: invalid machine-worker options or unelevated token; directory=$INSTDIR; parameters=$parameters"
    SetErrorLevel 2
    Quit
FunctionEnd

Function InstallForAllUsers
    SectionGetFlags ${SecContext} $1
    IntOp $1 $1 & ${SF_SELECTED}
    ; The worker is silent: it logs into a file the parent replays afterwards.
    StrCpy $logPath "$TEMP\IconicFolders-install-machine.log"
    ClearErrors
    FileOpen $R9 "$logPath" w
    ${IfNot} ${Errors}
        FileWriteUTF16LE /BOM $R9 ""
        FileClose $R9
    ${EndIf}
    ClearErrors
    DetailPrint $(MACHINE_WAIT)
    FindWindow $R8 "#32770" "" $HWNDPARENT
    GetDlgItem $R8 $R8 1006
    SendMessage $R8 ${WM_SETTEXT} 0 "STR:$(MACHINE_WAIT)"
    StrCpy $elevatedParameters "/S /AllUsers /MachineWorker /ContextMenu=$1 /Language=$LANGUAGE /Log=$\"$logPath$\" /D=$INSTDIR"
    Call RunElevated
    !insertmacro WORKER_LOG "Machine worker launch/wait result: $elevatedResult"
    Call ReplayWorkerLog
FunctionEnd

Function ReplayWorkerLog
    ${If} $logPath == ""
        Return
    ${EndIf}
    ClearErrors
    FileOpen $R9 "$logPath" r
    ${If} ${Errors}
        ClearErrors
        Return
    ${EndIf}
    ${Do}
        FileReadUTF16LE $R9 $R8
        ${If} ${Errors}
            ${ExitDo}
        ${EndIf}
        StrCpy $R7 $R8 2 -2
        ${If} $R7 == "$\r$\n"
            StrCpy $R8 $R8 -2
        ${EndIf}
        ${If} $R8 != ""
            DetailPrint $R8
        ${EndIf}
    ${Loop}
    FileClose $R9
    ClearErrors
FunctionEnd

Function ExplainWorkerFailure
    ${Switch} $elevatedResult
        ${Case} ${EXIT_SCOPE}
            StrCpy $R8 $(ERROR_SCOPE)
            ${Break}
        ${Case} ${EXIT_FILES}
            StrCpy $R8 $(ERROR_MACHINE_FILES)
            ${Break}
        ${Case} ${EXIT_REGISTRY}
            StrCpy $R8 $(ERROR_MACHINE_REGISTRY)
            ${Break}
        ${Case} ${EXIT_CONTEXT}
            StrCpy $R8 $(ERROR_MACHINE_CONTEXT)
            ${Break}
        ${Case} ${EXIT_OPTIONS}
            StrCpy $R8 $(ERROR_MACHINE_OPTIONS)
            ${Break}
        ${Default}
            StrCpy $R8 "$(ERROR_MACHINE) ($elevatedResult)"
    ${EndSwitch}
    MessageBox MB_YESNO|MB_ICONSTOP "$R8$\r$\n$\r$\n$(OPEN_LOG)" /SD IDNO IDNO explain_done
    ExecShell "open" "$logPath"
explain_done:
FunctionEnd

Function WriteUpdateResult
    ; Only the initiating parent writes the result, under its original identity.
    ; An elevated worker must finish all machine changes before this is called.
    ${If} $machineWorker == 1
    ${OrIf} $updateResult == ""
        Return
    ${EndIf}
    ClearErrors
    FileOpen $R9 "$updateResult" w
    IfErrors result_failed
    FileWriteUTF16LE /BOM $R9 "[Installation]$\r$\nMode=$installMode$\r$\nExecutable=$INSTDIR\IconicFolders.exe$\r$\nComplete=1$\r$\n"
    ${If} ${Errors}
        FileClose $R9
        Delete "$updateResult"
        Goto result_failed
    ${EndIf}
    FileClose $R9
    ${IfNot} ${Errors}
        Return
    ${EndIf}
    Delete "$updateResult"
result_failed:
    !insertmacro WORKER_LOG "ERROR: writing installation result: $updateResult"
    MessageBox MB_OK|MB_ICONSTOP $(ERROR_INSTALL) /SD IDOK
    SetErrorLevel 2
    Abort
FunctionEnd
