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

rem The proxy links the workshop library now, because the loader owns the ImGui
rem overlay. It used to link one object file out of it, the crash handler, and build
rem that itself. The lib supersedes that: a static lib is only searched for symbols
rem that are still unresolved, so linking the whole thing costs nothing in the
rem output beyond what is actually used.
echo === building the workshop library ===
call "%SRCDIR%build_workshop.bat"
if errorlevel 1 (
    echo ERROR: the workshop library failed to build, so the proxy cannot link.
    exit /b 1
)

set "OUTDIR=%SRCDIR%build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
pushd "%OUTDIR%"

rem /MT   static CRT, so the game folder needs no VC redist
rem /O2   optimise; /W4 keep the warnings honest
rem /GS-  the forwarding thunks are __declspec(naked); no security cookie can be
rem       emitted into them, and there are no stack buffers worth protecting here
set "CL_FLAGS=/nologo /c /EHsc /MT /O2 /W4 /GS- /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /I"%SRCDIR%workshop\include""
rem gdi32 for the ImGui win32 backend, delayimp plus the delay load for the dx11
rem one: imgui_impl_dx11 compiles its shaders with D3DCompile, and delay loading that
rem means a machine without d3dcompiler_47.dll loses the overlay instead of the whole
rem game. No d3d11.lib and no dxgi.lib on purpose, because the overlay takes the
rem game's existing device and swapchain and never creates one of its own.
set "LINK_FLAGS=/nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF kernel32.lib user32.lib gdi32.lib delayimp.lib /DELAYLOAD:d3dcompiler_47.dll"

set "TARGETS=%~1"
if "%TARGETS%"=="" set "TARGETS=dinput8 version"

for %%T in (%TARGETS%) do call :build %%T
if errorlevel 1 goto :fail

popd
echo.
echo Built into: %OUTDIR%
dir /b "%OUTDIR%\*.dll"
echo.
rem INSTALLED, NOT JUST SUGGESTED, the same lesson the plugin scripts already
rem learned. This used to print the copy command and stop, which reads exactly like
rem having done it, and the loader next to FFX.exe was then TWO DAYS STALE while
rem three rebuilds in a row reported success. Everything in the proxy, the crash
rem handler install and the overlay claim among it, was running old code.
set "GAMEDIR=G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
if not exist "%GAMEDIR%\FFX.exe" (
    echo.
    echo *** NOT INSTALLED ***  FFX.exe is not at:
    echo     "%GAMEDIR%"
    echo     Edit GAMEDIR in this script, then copy build\dinput8.dll there by hand.
    exit /b 1
)
copy /y "%OUTDIR%\dinput8.dll" "%GAMEDIR%\" >nul
if errorlevel 1 (
    echo.
    echo *** NOT INSTALLED ***  dinput8.dll could not be copied to:
    echo     "%GAMEDIR%"
    echo     The game is probably running and holding it open. Close it and run
    echo     this again, or the next test measures the old loader.
    exit /b 1
)
rem QUOTED, because this game's folder name contains an ampersand and cmd splits
rem an unquoted echo on it, which printed a path that stopped at "FFX".
echo Installed: "%GAMEDIR%\dinput8.dll"
echo            A AlBhedWorkshop\plugins folder is created on first run.
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
link %LINK_FLAGS% /OUT:"%OUTNAME%" "%OBJ%" "AlBhedWorkshop.lib"
if errorlevel 1 exit /b 1
echo --- exports of %OUTNAME% ---
dumpbin /nologo /exports "%OUTNAME%" | findstr /r /c:"^ *[0-9]"
echo.
exit /b 0

:fail
popd
echo BUILD FAILED
exit /b 1
