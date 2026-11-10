@echo off
setlocal
rem PS5 Hookshot: the build on a Windows PC, through Ubuntu 24.04 under WSL 2.
rem
rem   Drag your game's .nup file onto this file, or run:  build-windows.bat C:\path\to\your-game.nup
rem
rem It runs build-title.sh (the Linux build: keep it in the same folder as this file) inside Ubuntu, in Ubuntu's own
rem files (~/PS5_Homebrews), and puts the finished PPSA99024 folder beside this file. Run it again after an
rem interruption: what is done is not done again. Nothing made from your game leaves your PC.
set "DISTRO=Ubuntu-24.04"

if "%~1"=="" (
    echo Drag your game's .nup file onto build-windows.bat, or run:
    echo     build-windows.bat C:\path\to\your-game.nup
    goto :fail
)
if not exist "%~f1" (
    echo No such file: %~f1
    goto :fail
)
if not exist "%~dp0build-title.sh" (
    echo build-title.sh must be in the same folder as this file: %~dp0
    goto :fail
)

wsl.exe -d %DISTRO% -- true >nul 2>&1
if errorlevel 1 (
    echo Ubuntu 24.04 under WSL is not set up yet. Open a terminal as administrator and run:
    echo.
    echo     wsl --install -d %DISTRO%
    echo.
    echo Restart Windows if it asks, open "Ubuntu 24.04" once from the Start menu to choose a user name and a
    echo password, then run this file again.
    goto :fail
)

rem (the three paths go to Ubuntu as its own paths: WSLENV's /p)
set "HOOKSHOT_GAME=%~f1"
set "HOOKSHOT_OUT=%~dp0."
set "HOOKSHOT_SCRIPT=%~dp0build-title.sh"
set "WSLENV=HOOKSHOT_GAME/p:HOOKSHOT_OUT/p:HOOKSHOT_SCRIPT/p"

echo == Ubuntu's packages
wsl.exe -d %DISTRO% -u root -- bash "$HOOKSHOT_SCRIPT" --install-packages
if errorlevel 1 goto :fail

echo == The build: an hour or more the first time
wsl.exe -d %DISTRO% --cd ~ -- bash "$HOOKSHOT_SCRIPT"
if errorlevel 1 goto :fail

echo.
echo Done: %~dp0PPSA99024
pause
exit /b 0

:fail
echo.
echo The build did not finish.
pause
exit /b 1
