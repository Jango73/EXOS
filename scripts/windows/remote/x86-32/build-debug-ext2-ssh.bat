@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\\..\\.."

call "%ROOT_DIR%\\scripts\\windows\\remote\\run-ssh.bat" "scripts/linux/build/build.sh" --arch x86-32 --fs ext2 --debug
exit /b %errorlevel%
