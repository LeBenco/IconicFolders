; SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
; FileSpacer wizard layout and native artwork rendering; IconicFolders branding.
!macro WIZARD_STYLE PREFIX
Function ${PREFIX}SetWizardArtwork
    !if "${PREFIX}" == ""
        SetAutoClose true
    !endif
    InitPluginsDir
    ${If} $LANGUAGE == ${LANG_FRENCH}
        File /oname=$PLUGINSDIR\sidebar.bmp "..\build\installer-assets\wizard-fr.bmp"
    ${Else}
        File /oname=$PLUGINSDIR\sidebar.bmp "..\build\installer-assets\wizard-en.bmp"
    ${EndIf}
    File /oname=$PLUGINSDIR\WizardBitmap.dll "..\build\installer-assets\WizardBitmap.dll"
    System::Store S
    SetCtlColors $HWNDPARENT 20252E F1F2F6
    GetDlgItem $0 $HWNDPARENT 1034
    SetCtlColors $0 20252E FFFFFF
    System::Call 'user32::SetWindowPos(p r0,p 1,i 0,i 0,i 0,i 0,i 0x13)'
    GetDlgItem $0 $HWNDPARENT 1025
    StrCpy $brandingBitmap 0
    System::Call 'user32::GetClientRect(p r0,@r1)i.r9'
    ${If} $9 != 0
        System::Call '*$1(i,i,i.r2,i.r3)'
        StrCpy $4 0
        System::Call '"$PLUGINSDIR\WizardBitmap.dll"::createWizardBitmap(w "$PLUGINSDIR\sidebar.bmp",i r2,i r3)p.r4'
        ${If} $4 P<> 0
            StrCpy $brandingBitmap $4
            ; Display the already resized bitmap without another sampling pass.
            SendMessage $0 ${STM_SETIMAGE} ${IMAGE_BITMAP} $brandingBitmap
        ${EndIf}
    ${EndIf}
    ${If} $brandingBitmap P= 0
        ${NSD_SetStretchedImage} $0 "$PLUGINSDIR\sidebar.bmp" $brandingBitmap
    ${EndIf}
    ; Native Segoe UI text avoids resampling glyphs with the sidebar bitmap.
    ; Match the wizard DPI and explicitly request CLEARTYPE_QUALITY (5).
    System::Call 'user32::GetDC(p $HWNDPARENT)p.r1'
    System::Call 'gdi32::GetDeviceCaps(p r1,i 90)i.r2'
    System::Call 'user32::ReleaseDC(p $HWNDPARENT,p r1)'
    System::Call 'kernel32::MulDiv(i 16,i r2,i -72)i.r3'
    System::Call 'gdi32::CreateFontW(i r3,i 0,i 0,i 0,i 600,i 0,i 0,i 0,i 0,i 0,i 0,i 5,i 0,w "Segoe UI")p.r4'
    StrCpy $brandingTitleFont $4
    System::Call 'kernel32::MulDiv(i 9,i r2,i -72)i.r3'
    System::Call 'gdi32::CreateFontW(i r3,i 0,i 0,i 0,i 400,i 0,i 0,i 0,i 0,i 0,i 0,i 5,i 0,w "Segoe UI")p.r4'
    StrCpy $brandingCaptionFont $4
    GetDlgItem $0 $HWNDPARENT 1050
    SetCtlColors $0 20252E F1F2F6
    SendMessage $0 ${WM_SETFONT} $brandingTitleFont 1
    GetDlgItem $0 $HWNDPARENT 1051
    SetCtlColors $0 69717C F1F2F6
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(BRANDING_SUBTITLE)"
    SendMessage $0 ${WM_SETFONT} $brandingCaptionFont 1
    GetDlgItem $0 $HWNDPARENT 1256
    SetCtlColors $0 69717C F1F2F6
    GetDlgItem $0 $HWNDPARENT 1028
    SetCtlColors $0 69717C F1F2F6
    CreateFont $titleFont "$(^Font)" 14 700
    GetDlgItem $0 $HWNDPARENT 1037
    SendMessage $0 ${WM_SETFONT} $titleFont 0
    System::Store L
FunctionEnd

Function ${PREFIX}SetPagePalette
    System::Store S
    System::Call 'user32::GetWindow(p $HWNDPARENT,i 5)p.r0'
    ${While} $0 != 0
        System::Call 'user32::GetClassNameW(p r0,w .r2,i ${NSIS_MAX_STRLEN})i.r9'
        ${If} $2 == "#32770"
            SetCtlColors $0 20252E FFFFFF
            System::Call 'user32::GetWindow(p r0,i 5)p.r1'
            ${While} $1 != 0
                System::Call 'user32::GetClassNameW(p r1,w .r2,i ${NSIS_MAX_STRLEN})i.r9'
                ${If} $2 == "Static"
                ${OrIf} $2 == "Button"
                ${OrIf} $2 == "Edit"
                    SetCtlColors $1 20252E FFFFFF
                ${EndIf}
                System::Call 'user32::GetWindow(p r1,i 2)p.r1'
            ${EndWhile}
        ${EndIf}
        System::Call 'user32::GetWindow(p r0,i 2)p.r0'
    ${EndWhile}
    System::Store L
FunctionEnd

Function ${PREFIX}FreeWizardArtwork
    ${If} $brandingBitmap != 0
        System::Call 'gdi32::DeleteObject(p $brandingBitmap)'
    ${EndIf}
    ${If} $titleFont != 0
        System::Call 'gdi32::DeleteObject(p $titleFont)'
    ${EndIf}
    ${If} $brandingTitleFont != 0
        System::Call 'gdi32::DeleteObject(p $brandingTitleFont)'
    ${EndIf}
    ${If} $brandingCaptionFont != 0
        System::Call 'gdi32::DeleteObject(p $brandingCaptionFont)'
    ${EndIf}
FunctionEnd
!macroend

!insertmacro WIZARD_STYLE ""
!insertmacro WIZARD_STYLE "un."

Function .onGUIEnd
    Call FreeWizardArtwork
FunctionEnd

Function un.onGUIEnd
    Call un.FreeWizardArtwork
FunctionEnd

Function FinishPageShow
    !insertmacro MUI_HEADER_TEXT $(FINISH_TITLE) ""
    System::Store S
    GetDlgItem $0 $HWNDPARENT 1
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(FINISH_BUTTON)"
    GetDlgItem $1 $HWNDPARENT 2
    System::Alloc 16
    Pop $2
    ${If} $2 != 0
        System::Call 'user32::GetWindowRect(p r1,p r2)'
        System::Call 'user32::MapWindowPoints(p 0,p $HWNDPARENT,p r2,i 2)'
        System::Call '*$2(i.r3,i.r4,i.r5,i.r6)'
        IntOp $5 $5 - $3
        IntOp $6 $6 - $4
        System::Call 'user32::SetWindowPos(p r0,p 0,i r3,i r4,i r5,i r6,i 0x14)'
        System::Free $2
    ${EndIf}
    ShowWindow $1 ${SW_HIDE}
    GetDlgItem $0 $HWNDPARENT 3
    ShowWindow $0 ${SW_HIDE}
    System::Store L
    nsDialogs::Create 1018
    Pop $0
    ${NSD_CreateLabel} 0 12u 100% 90u "$(FINISH_TEXT)"
    Pop $0
    ${NSD_CreateCheckbox} 0 110u 100% 14u "$(RestartExplorerText)"
    Pop $restartChoice
    Call SetPagePalette
    nsDialogs::Show
FunctionEnd

Function ScopePageShow
    !insertmacro MUI_HEADER_TEXT $(SCOPE_TITLE) $(SCOPE_SUBTITLE)
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == "error"
        Abort
    ${EndIf}
    ${NSD_CreateRadioButton} 15u 4u -15u 12u "$(SCOPE_CURRENT)"
    Pop $scopeCurrent
    ${NSD_CreateRadioButton} 15u 22u -15u 14u "$(SCOPE_ALL)"
    Pop $scopeAll
    ${If} $installMode == "AllUsers"
        ${NSD_Check} $scopeAll
    ${Else}
        ${NSD_Check} $scopeCurrent
    ${EndIf}
    nsDialogs::CreateControl STATIC "${DEFAULT_STYLES}|${SS_LEFT}" 0 0 30% 100% 10u "$(NOTES_TITLE)"
    Pop $0
    SetCtlColors $0 20252E FFFFFF
    nsDialogs::CreateControl EDIT "${DEFAULT_STYLES}|${WS_TABSTOP}|${WS_VSCROLL}|${ES_MULTILINE}|${ES_AUTOVSCROLL}" "${WS_EX_CLIENTEDGE}" 0 40% 100% 60% ""
    Pop $notesText
    SendMessage $notesText ${EM_SETLIMITTEXT} 1048576 0
    FileOpen $1 "$PLUGINSDIR\changelog.txt" r
    IfErrors notes_ready
    FileSeek $1 2 SET ; Skip the UTF-16LE BOM.
notes_read:
    ClearErrors
    FileReadUTF16LE $1 $0
    IfErrors notes_close
    SendMessage $notesText ${EM_SETSEL} -1 -1
    SendMessage $notesText ${EM_REPLACESEL} 0 "STR:$0"
    Goto notes_read
notes_close:
    FileClose $1
notes_ready:
    SendMessage $notesText ${EM_SETREADONLY} 1 0
    SendMessage $notesText ${EM_SETSEL} 0 0
    SendMessage $notesText ${EM_SCROLLCARET} 0 0
    Call SetPagePalette
    nsDialogs::Show
FunctionEnd

Function ScopePageLeave
    ${NSD_GetState} $scopeAll $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $installMode "AllUsers"
    ${Else}
        StrCpy $installMode "CurrentUser"
    ${EndIf}
    Call SetInstallScope
FunctionEnd

Function FinishPageLeave
    ${NSD_GetState} $restartChoice $0
    ${If} $0 == ${BST_CHECKED}
        Call RestartExplorer
    ${EndIf}
FunctionEnd
