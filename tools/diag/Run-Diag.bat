@echo off
cd /d "%~dp0"
echo Collecting LumenBox display info, please wait...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0LumenBox-Diag.ps1"
echo.
echo Done. The report is on your Desktop: LumenBox-Diag-Report.txt
echo Please send that txt file back.
pause
