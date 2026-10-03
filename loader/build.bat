@echo off
rem ---------------------------------------------------------------------------
rem Al Bhed Workshop loader build. Produces 32-bit dinput8.dll (and version.dll as the
rem fallback proxy target) into .\build\.
rem
rem Usage:
rem     build.bat              build both proxy variants
rem     build.bat dinput8      build only dinput8.dll
rem     build.bat version      build only version.dll
rem
rem Verified toolchain on this machine:
rem     Visual Studio 18 Community, MSVC 14.51.36231, vcvars32.bat
rem     C:\Program Files\Microsoft Visual Studio\18\Community
rem ---------------------------------------------------------------------------
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvars32.bat not found at:
    echo        %VCVARS%
    echo Edit VCVARS in this script to point at your x86 developer environment.
    exit /b 1
)

rem vcvars32 = host x86, target x86. This is the 32-bit toolchain we need.
call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: vcvars32.bat failed.
    exit /b 1
)

set "SRCDIR=%~dp0"
set "OUTDIR=%SRCDIR%build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
pushd "%OUTDIR%"

rem /MT   static CRT, so the game folder needs no VC redist
rem /O2   optimise; /W4 keep the warnings honest
rem /GS-  the forwarding thunks are __declspec(naked); no security cookie can be
rem       emitted into them, and there are no stack buffers worth protecting here
set "CL_FLAGS=/nologo /c /EHsc /MT /O2 /W4 /GS- /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS"
set "LINK_FLAGS=/nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF kernel32.lib user32.lib"

set "TARGETS=%~1"
if "%TARGETS%"=="" set "TARGETS=dinput8 version"

for %%T in (%TARGETS%) do call :build %%T
if errorlevel 1 goto :fail

popd
echo.
echo Built into: %OUTDIR%
dir /b "%OUTDIR%\*.dll"
echo.
echo Install: copy build\dinput8.dll next to FFX.exe. A AlBhedWorkshop\plugins folder
echo          is created on first run; drop plugin DLLs there.
exit /b 0

:build
set "T=%~1"
if /i "%T%"=="dinput8" (
    set "DEFS="
    set "OUTNAME=dinput8.dll"
    set "OBJ=workshop_proxy_dinput8.obj"
) else if /i "%T%"=="version" (
    set "DEFS=/DFFXCOOP_TARGET_VERSION"
    set "OUTNAME=version.dll"
    set "OBJ=workshop_proxy_version.obj"
) else (
    echo ERROR: unknown target "%T%" ^(expected dinput8 or version^)
    exit /b 1
)
echo === building %OUTNAME% ===
cl %CL_FLAGS% %DEFS% /Fo"%OBJ%" "%SRCDIR%workshop_proxy.cpp"
if errorlevel 1 exit /b 1
link %LINK_FLAGS% /OUT:"%OUTNAME%" "%OBJ%"
if errorlevel 1 exit /b 1
echo --- exports of %OUTNAME% ---
dumpbin /nologo /exports "%OUTNAME%" | findstr /r /c:"^ *[0-9]"
echo.
exit /b 0

:fail
popd
echo BUILD FAILED
exit /b 1
