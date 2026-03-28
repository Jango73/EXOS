@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%..\remote\run-ssh.bat" "scripts/linux/build/build.sh" %*
exit /b %errorlevel%
