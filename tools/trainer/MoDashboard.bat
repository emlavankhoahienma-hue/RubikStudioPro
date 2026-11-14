@echo off
chcp 65001 >nul
cd /d "%~dp0..\.."
set "PY_EXE=%LocalAppData%\Programs\Python\Python312\python.exe"
if not exist "%PY_EXE%" set "PY_EXE=python.exe"
start "" "http://localhost:8765"
"%PY_EXE%" tools\trainer\dashboard_server.py --port 8765
pause
