@echo off
rem P04 GCBudget - build one target from the command line (reproducible; no IDE needed).
rem Usage: build.bat [Target] [Configuration]
rem   Target:        GCBudgetEditor (default) | GCBudget
rem   Configuration: Development (default) | Shipping | DebugGame
rem Note: the launcher (installed) engine cannot build the Test configuration.
rem Run it through the machine-wide UE job queue, never directly (see Explanation\09_Testing_And_Benchmarks.md).
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\GCBudget\GCBudget.uproject"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=GCBudgetEditor"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE
exit /b %ERRORLEVEL%
