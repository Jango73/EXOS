@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\\..\\.."

call "%ROOT_DIR%\\scripts\\remote\\run-ssh.bat" "scripts/build.sh" --arch i386 --fs ext2 --debug
exit /b %errorlevel%
