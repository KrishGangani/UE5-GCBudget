@echo off
rem P04 GCBudget - run the automation tests headless in the editor and exit with their result.
rem Usage: run_tests.bat [Filter]   (default filter: GCBudget, i.e. every test of this project)
rem Exit code 0 = all passed; non-zero = a failure (the engine prints "TEST COMPLETE. EXIT CODE").
rem Needs the Development editor target: build.bat GCBudgetEditor Development
rem Run it through the machine-wide UE job queue, never directly.
setlocal
rem Capture this script's folder first (a later "shift" would change %~dp0).
set "HERE=%~dp0"
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%HERE%..\GCBudget\GCBudget.uproject"
set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=GCBudget"
set "REPORT=%HERE%..\GCBudget\Saved\Automation\Reports"
set "LOG=%HERE%..\GCBudget\Saved\Logs\Tests.log"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -ExecCmds="Automation RunTests %FILTER%;Quit" -unattended -nullrhi -nosound -nosplash -nopause -log -abslog="%LOG%" -ReportExportPath="%REPORT%"
exit /b %ERRORLEVEL%
