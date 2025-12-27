@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\\..\\.."

call "%ROOT_DIR%\\scripts\\remote\\run-ssh.bat" "scripts/i386/4-5-build-debug-ext2.sh"
exit /b %errorlevel%
