@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Copyright (C) 2026 Benco
setlocal
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "OUT=%~2"
set "FLAGS=/nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /MT /Z7 /Iexternal\wil /Isrc"
if "%~3"=="Asan" (
 set "FLAGS=%FLAGS% /fsanitize=address /Od"
 set "LIB=%~4;%LIB%"
) else (
 set "FLAGS=%FLAGS% /O2 /guard:cf"
)
rc /nologo /fo "%OUT%\app.res" src\app.rc
if errorlevel 1 exit /b 1
cl %FLAGS% /c src\core.cpp src\ui.cpp src\icons.cpp src\native-file-selector.cpp src\shell-safety.cpp /Fo"%OUT%\\"
if errorlevel 1 exit /b 1
cl %FLAGS% src\main.cpp src\picker.cpp src\setup.cpp "%OUT%\shell-safety.obj" "%OUT%\native-file-selector.obj" "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\icons.obj" "%OUT%\app.res" /Fo"%OUT%\\" /Fe"%OUT%\IconicFolders.exe" /link /DEBUG:FULL /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib comctl32.lib advapi32.lib uxtheme.lib dwmapi.lib user32.lib gdi32.lib windowscodecs.lib uuid.lib rstrtmgr.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl %FLAGS% /LD src\shell.cpp "%OUT%\shell-safety.obj" "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\shell.obj" /Fe"%OUT%\IconicFoldersShell.dll" /link /DEBUG:FULL /DEF:src\shell.def /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT shell32.lib ole32.lib shlwapi.lib advapi32.lib user32.lib gdi32.lib bcrypt.lib
if errorlevel 1 exit /b 1
if not exist tests\tests.cpp exit /b 0
rc /nologo /fo "%OUT%\test-icons.res" tests\icon-resources.rc
if errorlevel 1 exit /b 1
cl %FLAGS% /LD tests\icon-resources.cpp "%OUT%\test-icons.res" /Fo"%OUT%\icon-resources.obj" /Fe"%OUT%\IconResources.dll" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
cl %FLAGS% tests\tests.cpp "%OUT%\shell-safety.obj" "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\icons.obj" "%OUT%\app.res" /Fo"%OUT%\tests.obj" /Fe"%OUT%\IconicFoldersTests.exe" /link /DEBUG:FULL /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib advapi32.lib user32.lib gdi32.lib windowscodecs.lib uuid.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl %FLAGS% tests\selector-tests.cpp "%OUT%\native-file-selector.obj" "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\selector-tests.obj" /Fe"%OUT%\SelectorTests.exe" /link /DEBUG:FULL /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib comctl32.lib advapi32.lib uxtheme.lib user32.lib gdi32.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl %FLAGS% tests\picker-tests.cpp src\picker.cpp "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\icons.obj" "%OUT%\app.res" /Fo"%OUT%\\" /Fe"%OUT%\PickerTests.exe" /link /DEBUG:FULL /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib comctl32.lib advapi32.lib uxtheme.lib dwmapi.lib user32.lib gdi32.lib windowscodecs.lib uuid.lib rstrtmgr.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl %FLAGS% tests\native-app-tests.cpp "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\\" /Fe"%OUT%\NativeAppTests.exe" /link /DEBUG:FULL /SUBSYSTEM:CONSOLE /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib comctl32.lib advapi32.lib uxtheme.lib dwmapi.lib user32.lib gdi32.lib windowscodecs.lib uuid.lib rstrtmgr.lib dbghelp.lib oleaut32.lib bcrypt.lib
if errorlevel 1 exit /b 1
if "%~3"=="Asan" (
 cl %FLAGS% tests\asan-probe.cpp /Fo"%OUT%\asan-probe.obj" /Fe"%OUT%\ASanProbe.exe" /link /DEBUG:FULL /INCREMENTAL:NO
 if errorlevel 1 exit /b 1
)
exit /b 0
