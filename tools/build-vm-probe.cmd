@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Copyright (C) 2026 Benco
setlocal
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /MT /Z7 /O2 /Iexternal\wil /Isrc tests\client\restart-probe.cpp src\setup.cpp src\shell-safety.cpp "%~2\..\Release\core.obj" /Fo"%~2\\" /Fe"%~2\RestartProbe.exe" /link /DEBUG:FULL /INCREMENTAL:NO user32.lib advapi32.lib ole32.lib rstrtmgr.lib shell32.lib shlwapi.lib gdi32.lib bcrypt.lib
exit /b %errorlevel%
