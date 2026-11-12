@echo off
setlocal
set "GAME_TITLE=Fuzion Frenzy"
set "GAME_XBE=default.xbe"
set "GAME_EXE=FuzionFrenzy"
title %GAME_TITLE% - Build
cd /d "%~dp0"
echo.
echo   %GAME_TITLE% - build
echo   ================================================
echo.

set "PF86=%ProgramFiles(x86)%"
set "VSINST=%PF86%\Microsoft Visual Studio\Installer"
set "VSWHERE=%VSINST%\vswhere.exe"
set "OUT=%CD%\out\build\release"

if not exist assets\%GAME_XBE% goto :nogame

if defined RINGHOST_TOOLCHAIN goto :bundled

rem toolchain
echo [1/3] Toolchain
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo       Visual Studio with the C++ workload was not found.
    echo       Install Visual Studio Community and tick "Desktop development with C++".
    goto :fail
)
call :setuptools
rem CI is set on GitHub Actions: no prompts there.
if not defined TOOLS_OK if defined CI goto :byhand
if not defined TOOLS_OK (
    choice /c YN /n /m "      Let Visual Studio install the missing components? [Y/N] "
    if errorlevel 2 goto :byhand
    echo       Close Visual Studio if it is open, it will be shut down anyway.
    echo       Installing. Accept the admin prompt, then wait for the installer.
    powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%VSINST%\setup.exe' -ArgumentList 'modify','--installPath','%VSDIR%','--add','Microsoft.VisualStudio.Component.VC.Llvm.Clang','--add','Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset','--add','Microsoft.VisualStudio.Component.VC.CMake.Project','--passive','--norestart','--force'"
    echo       Press a key once the Visual Studio Installer window has closed.
    pause >nul
    call :setuptools
    if not defined TOOLS_OK goto :byhand
)
for /f "tokens=*" %%v in ('clang++ --version ^| findstr /i version') do echo       %%v

rem symbols
echo [2/3] XDK symbols
cmake --preset release >nul || goto :fail
goto :afterconfig

rem bundled clang-cl. The release preset wants Visual Studio's clang.
:bundled
echo [1/3] Toolchain
set "TB=%RINGHOST_TOOLCHAIN%\bin"
set MISSING=
if not exist "%TB%\clang-cl.exe" set MISSING=%MISSING% clang-cl
if not exist "%TB%\lld-link.exe" set MISSING=%MISSING% lld-link
if not exist "%TB%\llvm-lib.exe" set MISSING=%MISSING% llvm-lib
if not exist "%TB%\llvm-rc.exe" set MISSING=%MISSING% llvm-rc
if not exist "%TB%\llvm-mt.exe" set MISSING=%MISSING% llvm-mt
if not exist "%TB%\cmake.exe" set MISSING=%MISSING% cmake
if not exist "%TB%\ninja.exe" set MISSING=%MISSING% ninja
if defined MISSING (
    echo       missing:%MISSING%
    goto :fail
)
if not defined RINGHOST_WINSDK (
    echo       RINGHOST_WINSDK is not set. The launcher must point it at the Windows SDK splat.
    goto :fail
)
if not exist "%RINGHOST_WINSDK%\VC\Tools\MSVC" (
    echo       RINGHOST_WINSDK is missing VC\Tools\MSVC.
    goto :fail
)
if not exist "%RINGHOST_WINSDK%\Windows Kits\10\Lib" (
    echo       RINGHOST_WINSDK is missing Windows Kits\10\Lib.
    goto :fail
)
set "PATH=%TB%;%PATH%"
for /f "tokens=*" %%v in ('clang-cl --version ^| findstr /i version') do echo       %%v

rem The zip stores UTC as a local time. On this clock those sources are hours
rem ahead of build.ninja, so Ninja re-runs CMake forever and never compiles.
powershell -NoProfile -Command "$now=Get-Date; function Walk([string]$d){ Get-ChildItem -LiteralPath $d -Force -ErrorAction SilentlyContinue | ForEach-Object { if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { return }; if ($_.PSIsContainer) { Walk $_.FullName } elseif ($_.LastWriteTime -gt $now) { $_.LastWriteTime = $now } } }; Walk '%CD%'"
echo [2/3] XDK symbols
rem Forward slashes. CMake parses \U in a quoted string as an escape.
set "TC=%RINGHOST_TOOLCHAIN:\=/%"
set "TF=%~dp0cmake\windows-player.cmake"
set "TF=%TF:\=/%"
set PREFIX_ARG=
if exist "%RINGHOST_TOOLCHAIN%\prefix\." set PREFIX_ARG=-DCMAKE_PREFIX_PATH="%TC%/prefix"
"%TB%\cmake.exe" -S . -B out\build\release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TF%" ^
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON ^
  -DFETCHCONTENT_SOURCE_DIR_SDL3="%TC%/third_party/SDL" ^
  -DFETCHCONTENT_SOURCE_DIR_IMGUI="%TC%/third_party/imgui" ^
  -DFETCHCONTENT_SOURCE_DIR_ZYDIS="%TC%/third_party/zydis" ^
  -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="%TC%/third_party/tomlplusplus" ^
  %PREFIX_ARG% || goto :fail

:afterconfig
findstr /b /l /c:"[symbols]" config\game.toml >nul 2>nul && echo       config\game.toml already written && goto :game
cmake --build out\build\release --target xbe_symbols >nul || goto :fail
rem the configure step wrote where the SDK is
if not exist out\build\release\sdk_dir.txt echo       out\build\release\sdk_dir.txt was not written, update the SDK. && goto :fail
set /p SDK=<out\build\release\sdk_dir.txt
if not exist config mkdir config
"%OUT%\xbe_symbols.exe" assets\%GAME_XBE% "%SDK%\xdk" --out config\game.toml || goto :fail

rem game
:game
echo [3/3] Game code
echo       first build recompiles %GAME_XBE%, expect 30 to 90 minutes ...
cmake --build out\build\release || goto :fail

echo.
echo   %GAME_TITLE% built.
echo   out\build\release\%GAME_EXE%.exe
echo.
if defined CI exit /b 0
choice /c YN /n /m "  Play now? [Y/N] "
if not errorlevel 2 (start "" /d "%OUT%" "%OUT%\%GAME_EXE%.exe") else (pause)
exit /b 0

rem helpers
:setuptools
set TOOLS_OK=
set MISSING=
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang++ >nul 2>nul || set MISSING=%MISSING% clang++
where cmake   >nul 2>nul || set MISSING=%MISSING% cmake
where ninja   >nul 2>nul || set MISSING=%MISSING% ninja
where git     >nul 2>nul || set MISSING=%MISSING% git
if defined MISSING (
    echo       missing:%MISSING%
    exit /b 0
)
set TOOLS_OK=1
exit /b 0

:byhand
echo.
echo       Install by hand, then run this again:
echo         clang++ / cmake / ninja  -  Visual Studio Installer, Modify, Individual
echo                                     components: "C++ Clang tools for Windows"
echo                                     and "C++ CMake tools for Windows"
echo         git                      -  https://git-scm.com/download/win
goto :fail

:nogame
echo   Put the full game files in the assets folder first:
echo     %CD%\assets
echo   It needs %GAME_XBE% with the game's folders beside it.
goto :fail

:fail
echo.
echo   BUILD FAILED. Read the last message above.
echo.
if not defined CI pause
exit /b 1
