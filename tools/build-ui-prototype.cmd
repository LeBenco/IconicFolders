@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Copyright (C) 2026 Benco
setlocal
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
rc /nologo /fo "%~2\app.res" src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /Iexternal\wil tools\ui-prototype.cpp "%~2\app.res" /Fo"%~2\prototype.obj" /Fe"%~2\IconicFolders-UI-Prototype.exe" /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest shell32.lib ole32.lib comctl32.lib user32.lib gdi32.lib uxtheme.lib dwmapi.lib
exit /b %errorlevel%
