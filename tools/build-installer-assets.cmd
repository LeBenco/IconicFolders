@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Copyright (C) 2026 Benco
setlocal
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "OUT=%~2"
rc /nologo /fo "%OUT%\wizard-ui.res" installer\wizard-ui.rc
if errorlevel 1 exit /b 1
link /nologo /dll /noentry /machine:x86 /out:"%OUT%\wizard-ui.dll" "%OUT%\wizard-ui.res"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /external:env:INCLUDE /external:W0 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /MT /O2 /LD installer\WizardBitmap.cpp /Fo"%OUT%\WizardBitmap.obj" /Fd"%OUT%\WizardBitmap.pdb" /Fe"%OUT%\WizardBitmap.dll" /link /incremental:no Gdiplus.lib Gdi32.lib User32.lib
exit /b %errorlevel%
