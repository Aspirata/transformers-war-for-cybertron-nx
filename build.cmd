@echo off
setlocal
if exist "%~dp0out\deps\pc-env.cmd" (
    call "%~dp0out\deps\pc-env.cmd"
    if errorlevel 1 exit /b 1
)
python "%~dp0tools\build.py" %*
exit /b %errorlevel%
