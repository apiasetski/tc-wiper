@echo off
rem Builds TCWiper.wfx (32-bit) and TCWiper.wfx64 (64-bit) and the install zip.
rem Usage: build.cmd          - release build + dist\TCWiper.zip
rem        build.cmd test     - also builds and runs the test harness
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto novs
rem vcvarsall looks for vswhere on PATH
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

call :plugin x86 TCWiper.wfx || exit /b 1
call :plugin x64 TCWiper.wfx64 || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%package.ps1" || exit /b 1

if /i "%~1"=="test" (
    call :test x86 || exit /b 1
    call :test x64 || exit /b 1
)
exit /b 0

:novs
echo Visual Studio C++ Build Tools not found.
exit /b 1

rem %1 = architecture, %2 = output file name
:plugin
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul || exit /b 1
set "OBJ=%ROOT%build\obj\%1"
if not exist "%OBJ%" mkdir "%OBJ%"
echo Building %2 (%1)...
rc /nologo /fo "%OBJ%\tcwiper.res" "%ROOT%src\tcwiper.rc" || exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /MT /LD /DUNICODE /D_UNICODE /Fo"%OBJ%\\" ^
   "%ROOT%src\tcwiper.cpp" "%OBJ%\tcwiper.res" ^
   /link /DEF:"%ROOT%src\tcwiper.def" /OUT:"%ROOT%build\%2" /IMPLIB:"%OBJ%\tcwiper.lib" ^
   bcrypt.lib user32.lib gdi32.lib comctl32.lib || exit /b 1
endlocal & exit /b 0

rem Test build: same source, message boxes answered automatically.
:test
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul || exit /b 1
set "OUT=%ROOT%build\test\%1"
if not exist "%OUT%" mkdir "%OUT%"
echo Building tests (%1)...
rc /nologo /fo "%OUT%\tcwiper.res" "%ROOT%src\tcwiper.rc" || exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /MT /LD /DUNICODE /D_UNICODE /DTCWIPER_TEST /Fo"%OUT%\\" ^
   "%ROOT%src\tcwiper.cpp" "%OUT%\tcwiper.res" ^
   /link /DEF:"%ROOT%src\tcwiper.def" /OUT:"%OUT%\tcwiper_test.dll" /IMPLIB:"%OUT%\tcwiper_test.lib" ^
   bcrypt.lib user32.lib gdi32.lib comctl32.lib || exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /Fo"%OUT%\\" /Fe"%OUT%\test_harness.exe" ^
   "%ROOT%tests\test_harness.cpp" kernel32.lib || exit /b 1
"%OUT%\test_harness.exe" "%OUT%\tcwiper_test.dll" "%OUT%\work" || exit /b 1
endlocal & exit /b 0
