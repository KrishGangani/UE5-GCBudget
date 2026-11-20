@echo off
rem P04 GCBudget - smoke test: start the game, load the default map, quit.
rem Usage: smoke_game.bat [uncooked|packaged] [extra args...]
rem   uncooked (default): the editor binary in -game mode, no rendering (-nullrhi). Needs only the editor target.
rem   packaged:           the packaged Development build on the real GPU, windowed 1280x720. Needs package.bat first.
rem Run it through the machine-wide UE job queue, never directly.
setlocal
rem Capture this script's folder first: "shift" below also shifts %0, after which %~dp0 no longer points here.
set "HERE=%~dp0"
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%HERE%..\GCBudget\GCBudget.uproject"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=uncooked"
if /I "%MODE%"=="uncooked" goto uncooked
if /I "%MODE%"=="packaged" goto packaged
echo smoke_game.bat: unknown mode "%MODE%" (use uncooked or packaged)
exit /b 2

:uncooked
shift
set "LOG=%HERE%..\GCBudget\Saved\Logs\SmokeGame.log"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%" %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:packaged
shift
set "LOG=%HERE%..\Build\Windows\GCBudget\Saved\Logs\Smoke_Packaged.log"
"%HERE%..\Build\Windows\GCBudget.exe" -windowed -ResX=1280 -ResY=720 -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%" %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%
