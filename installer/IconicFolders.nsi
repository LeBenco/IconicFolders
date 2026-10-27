; SPDX-License-Identifier: GPL-3.0-only
; Copyright (C) 2026 Benco
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
Name "IconicFolders"
OutFile "..\dist\IconicFolders-1.0.1-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\IconicFolders"
InstallDirRegKey HKCU "Software\IconicFolders\Install" "Path"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "1.0.1.0"
VIAddVersionKey /LANG=1033 "ProductName" "IconicFolders"
VIAddVersionKey /LANG=1033 "FileDescription" "IconicFolders Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "1.0.1"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) 2026 Benco"
!define MUI_ICON "..\assets\icon.ico"
!define MUI_UNICON "..\assets\icon.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!define CLSID "{75F4D684-8623-4CE1-B3B2-464EF177FDDE}"
!define UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\IconicFolders"
LangString RegistrationFailed ${LANG_ENGLISH} "The context menu could not be registered. Installation failed."
LangString RegistrationFailed ${LANG_FRENCH} "Le menu contextuel n’a pas pu être enregistré. L’installation a échoué."

Function .onInit
 ${IfNot} ${AtLeastWin10}
  MessageBox MB_ICONSTOP "Windows 10 or later is required."
  Abort
 ${EndIf}
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP "This installer requires 64-bit Windows (x64)."
  Abort
 ${EndIf}
 System::Alloc 64
 Pop $0
 System::Call 'kernel32::GetNativeSystemInfo(p r0)'
 System::Call '*$0(&i2 .r1)'
 System::Free $0
 ${If} $1 != 9
  MessageBox MB_ICONSTOP "This build requires an x64 processor."
  Abort
 ${EndIf}
 System::Call 'kernel32::GetUserDefaultUILanguage() i .r0'
 IntOp $0 $0 & 0x3FF
 ${If} $0 = 12
  StrCpy $LANGUAGE 1036
 ${Else}
  StrCpy $LANGUAGE 1033
 ${EndIf}
FunctionEnd

Section
 SetRegView 64
 SetShellVarContext current
 SetOutPath "$INSTDIR"
 SetOverwrite ifnewer
 File "..\build\Release\IconicFolders.exe"
 File "..\build\Release\IconicFoldersShell.dll"
 File /oname=README.md "..\README.md"
 File /oname=LICENSE.txt "..\LICENSE"
 File /oname=WIL-LICENSE.txt "..\external\wil\LICENSE"
 File /oname=WIL-THIRD-PARTY-NOTICES.txt "..\external\wil\ThirdPartyNotices.txt"
 File /oname=MICROSOFT-FLUENT-EMOJI-LICENSE.txt "..\assets\third-party\LICENSE-Microsoft-Fluent-Emoji.txt"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 ClearErrors
 WriteRegStr HKCU "Software\IconicFolders\Install" "Path" "$INSTDIR"
 WriteRegStr HKCU "Software\Classes\CLSID\${CLSID}\InprocServer32" "" "$INSTDIR\IconicFoldersShell.dll"
 WriteRegStr HKCU "Software\Classes\CLSID\${CLSID}\InprocServer32" "ThreadingModel" "Apartment"
 WriteRegStr HKCU "Software\Classes\Directory\shell\IconicFolders" "MUIVerb" "@$INSTDIR\IconicFoldersShell.dll,-100"
 WriteRegStr HKCU "Software\Classes\Directory\shell\IconicFolders" "ExplorerCommandHandler" "${CLSID}"
 WriteRegStr HKCU "Software\Classes\Directory\shell\IconicFolders" "MultiSelectModel" "Player"
 WriteRegStr HKCU "${UNINSTALL}" "DisplayName" "IconicFolders"
 WriteRegStr HKCU "${UNINSTALL}" "DisplayVersion" "1.0.1"
 WriteRegStr HKCU "${UNINSTALL}" "Publisher" "IconicFolders"
 WriteRegStr HKCU "${UNINSTALL}" "InstallLocation" "$INSTDIR"
 WriteRegStr HKCU "${UNINSTALL}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
 WriteRegStr HKCU "${UNINSTALL}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
 WriteRegDWORD HKCU "${UNINSTALL}" "NoModify" 1
 WriteRegDWORD HKCU "${UNINSTALL}" "NoRepair" 1
 IfErrors 0 registration_ok
 MessageBox MB_ICONSTOP "$(RegistrationFailed)" /SD IDOK
 SetErrorLevel 1
 Quit
 registration_ok:
 System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Function un.onInit
 System::Call 'kernel32::GetUserDefaultUILanguage() i .r0'
 IntOp $0 $0 & 0x3FF
 ${If} $0 = 12
  StrCpy $LANGUAGE 1036
 ${Else}
  StrCpy $LANGUAGE 1033
 ${EndIf}
FunctionEnd

Section "Uninstall"
 SetRegView 64
 DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\RunOnce" "IconicFoldersCleanup"
 DeleteRegKey HKCU "Software\Classes\Directory\shell\IconicFolders"
 DeleteRegKey HKCU "Software\Classes\CLSID\${CLSID}"
 DeleteRegKey HKCU "${UNINSTALL}"
 DeleteRegKey HKCU "Software\IconicFolders"
 System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
 Delete "$INSTDIR\IconicFolders.exe"
 Delete "$INSTDIR\IconicFoldersShell.dll"
 Delete "$INSTDIR\README.md"
 Delete "$INSTDIR\LICENSE.txt"
 Delete "$INSTDIR\WIL-LICENSE.txt"
 Delete "$INSTDIR\WIL-THIRD-PARTY-NOTICES.txt"
 Delete "$INSTDIR\MICROSOFT-FLUENT-EMOJI-LICENSE.txt"
 IfFileExists "$INSTDIR\IconicFolders.exe" deferred_cleanup
 IfFileExists "$INSTDIR\IconicFoldersShell.dll" deferred_cleanup
 Delete "$INSTDIR\Uninstall.exe"
 RMDir "$INSTDIR"
 Goto uninstall_done
 deferred_cleanup:
 ; MOVEFILE_DELAY_UNTIL_REBOOT requires administrator rights. A per-user
 ; RunOnce retries after sign-in, when the unregistered DLL is no longer loaded.
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\RunOnce" "IconicFoldersCleanup" '$\"$INSTDIR\Uninstall.exe$\" /S'
 uninstall_done:
SectionEnd
