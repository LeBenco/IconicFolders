; SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
; IconicFolders: run an elevated worker and retain its actual process exit code.
; Uses the documented ShellExecuteExW contract, with the x86 NSIS host layout.
!if ${NSIS_PTR_SIZE} != 4
    !error "IconicFolders's installer and plugins require an x86 NSIS host."
!endif

!macro ELEVATION_FUNCTION PREFIX
Function ${PREFIX}RunElevated
    System::Store S
    StrCpy $elevatedResult 8 ; ERROR_NOT_ENOUGH_MEMORY
    System::Alloc 60
    Pop $0
    System::StrAlloc 6
    Pop $1
    System::StrAlloc ${NSIS_MAX_STRLEN}
    Pop $2
    System::StrAlloc ${NSIS_MAX_STRLEN}
    Pop $3
    ${If} $0 != 0
    ${AndIf} $1 != 0
    ${AndIf} $2 != 0
    ${AndIf} $3 != 0
        System::Call '*$1(&w6 "runas")'
        System::Call '*$2(&w${NSIS_MAX_STRLEN} "$EXEPATH")'
        ; Keep embedded quotes out of System's call-expression parser.
        StrCpy $4 $elevatedParameters
        System::Call '*$3(&w${NSIS_MAX_STRLEN} r4)'
        ; SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI.
        System::Call '*$0(i 60,i 0x540,p $HWNDPARENT,p r1,p r2,p r3,p 0,i 1,p 0,p 0,p 0,p 0,i 0,p 0,p 0)'
        System::Call 'ole32::CoInitializeEx(p 0,i 2)i.r5'
        System::Call 'shell32::ShellExecuteExW(p r0)i.r6 ?e'
        Pop $elevatedResult ; Includes ERROR_CANCELLED when UAC is declined.
        ${If} $6 != 0
            System::Call '*$0(i,i,p,p,p,p,p,i,p,p,p,p,i,p,p.r4)'
            StrCpy $elevatedResult 6 ; ERROR_INVALID_HANDLE
            ${If} $4 != 0
                ; Install sections run on NSIS's worker thread; un.onInit runs
                ; before the uninstaller owns a visible wizard window.
                System::Call 'kernel32::WaitForSingleObject(p r4,i -1)i.r6 ?e'
                Pop $elevatedResult
                ${If} $6 == 0
                    System::Call 'kernel32::GetExitCodeProcess(p r4,*i .r6)i.r7 ?e'
                    Pop $elevatedResult
                    ${If} $7 != 0
                        StrCpy $elevatedResult $6
                    ${EndIf}
                ${EndIf}
                System::Call 'kernel32::CloseHandle(p r4)'
            ${EndIf}
        ${EndIf}
        ${If} $5 >= 0
            System::Call 'ole32::CoUninitialize()'
        ${EndIf}
    ${EndIf}
    System::Free $3
    System::Free $2
    System::Free $1
    System::Free $0
    System::Store L
FunctionEnd
!macroend

!insertmacro ELEVATION_FUNCTION ""
!insertmacro ELEVATION_FUNCTION "un."
