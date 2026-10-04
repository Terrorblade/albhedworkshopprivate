@echo off
rem ---------------------------------------------------------------------------
rem Builds and runs the crash handler test, with no game involved.
rem
rem Two reports come out of a run, one asked for by hand and one from a real
rem access violation, so the on-demand path and the unhandled filter path are
rem both covered along with the PEB module walk, the frame walk, the stack scan
rem and the file write.
rem
rem Both land in .\build\crashtest\AlBhedWorkshop\albhed_crash.log, because the
rem handler always puts the file next to the running exe, the same way it does
rem in the game folder.
rem
rem WHAT TO LOOK FOR in the report:
rem   - "bytes at ei" should start C7 05 10 00 00 00 44 43 42 41, which is the
rem     "mov dword ptr [0x10], 0x41424344" the test faults on. If those bytes
rem     are wrong or unreadable, the handler is not reading real code.
rem   - "faulted at" and frame 0 should both be crashtest.exe, and the "ida"
rem     column should read 0x0040xxxx and stay the same across runs even though
rem     the raw address moves with ASLR. If it tracks the raw address, the
rem     preferred base is being taken from memory instead of the file.
rem   - the four breadcrumbs should read main, Outer, Middle, Innermost.
rem
rem WHAT TO LOOK FOR in the third report, which is one thread reporting another
rem the way the hang watchdog does:
rem   - the "thread" line should name the spin thread and then say it was read by
rem     the watchdog on a different thread.
rem   - the call chain should contain Spinner, SpinMiddle and SpinOuter in
rem     crashtest.exe. If BOTH chains come out empty, the subject's stack bounds
rem     are wrong and the walk is rejecting every address.
rem ---------------------------------------------------------------------------
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvars32.bat not found at:
    echo        %VCVARS%
    exit /b 1
)
call "%VCVARS%" >nul 2>nul
if errorlevel 1 (
    echo ERROR: vcvars32.bat failed. Run it by hand to see why:
    echo        "%VCVARS%"
    exit /b 1
)

set "LOADER=%~dp0"
set "OUTDIR=%LOADER%build\crashtest"

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OUTDIR%\AlBhedWorkshop" mkdir "%OUTDIR%\AlBhedWorkshop"
if exist "%OUTDIR%\AlBhedWorkshop\albhed_crash.log" del "%OUTDIR%\AlBhedWorkshop\albhed_crash.log"

pushd "%OUTDIR%"

rem Same flags as the proxy, which is the other thing that links this one file on
rem its own rather than through the library.
cl /nologo /EHsc /MT /O2 /W4 /GS- /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%LOADER%workshop\include" ^
   /Fe"crashtest.exe" "%LOADER%workshop\test\crashtest.cpp" ^
   "%LOADER%workshop\src\workshop\CrashHandler.cpp" ^
   /link kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo.
echo === run 1, the on demand report ===
.\crashtest.exe
echo.
echo === run 2, a real access violation. -1073741819 is 0xC0000005, so that
echo === exit code is the test working, not the test failing.
.\crashtest.exe crash
echo exit code %errorlevel%

echo.
echo === run 3, the hang watchdog path: one thread reports another ===
.\crashtest.exe hang

echo.
echo === the report ===
type "%OUTDIR%\AlBhedWorkshop\albhed_crash.log"

popd
exit /b 0

:fail
popd
echo CRASH TEST BUILD FAILED
exit /b 1
