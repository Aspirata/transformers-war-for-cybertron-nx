@echo off
setlocal
python "%~dp0tools\build.py" --run-pc %*
exit /b %errorlevel%
