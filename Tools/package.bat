@echo off
rem P04 GCBudget - package a standalone Win64 build (BuildCookRun).
rem Usage: package.bat [Configuration]   Configuration: Development (default; all headline benchmarks) | Shipping (no CSV profiler)
rem Note: the launcher (installed) engine cannot build the Test configuration ("Targets cannot be built in the Test configuration with this engine distribution").
rem Output: ..\Build\Windows\GCBudget.exe   (Build\ is git-ignored)
rem Run it through the machine-wide UE job queue, never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\GCBudget\GCBudget.uproject"
set "ARCHIVE=%~dp0..\Build"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory="%ARCHIVE%"
exit /b %ERRORLEVEL%
