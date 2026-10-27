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
cl %FLAGS% /c src\core.cpp src\ui.cpp /Fo"%OUT%\\"
if errorlevel 1 exit /b 1
cl %FLAGS% src\main.cpp "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\main.obj" /Fe"%OUT%\IconicFolders.exe" /link /DEBUG:FULL /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib comctl32.lib advapi32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl %FLAGS% /LD src\shell.cpp "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\shell.obj" /Fe"%OUT%\IconicFoldersShell.dll" /link /DEBUG:FULL /DEF:src\shell.def /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT shell32.lib ole32.lib shlwapi.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl %FLAGS% tests\tests.cpp "%OUT%\core.obj" "%OUT%\ui.obj" "%OUT%\app.res" /Fo"%OUT%\tests.obj" /Fe"%OUT%\IconicFoldersTests.exe" /link /DEBUG:FULL /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib shlwapi.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
if "%~3"=="Asan" (
 cl %FLAGS% tests\asan-probe.cpp /Fo"%OUT%\asan-probe.obj" /Fe"%OUT%\ASanProbe.exe" /link /DEBUG:FULL /INCREMENTAL:NO
 if errorlevel 1 exit /b 1
)
exit /b 0
