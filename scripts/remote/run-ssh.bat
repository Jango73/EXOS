@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "CONFIG_FILE=%SCRIPT_DIR%ssh-config.bat"

if not exist "%CONFIG_FILE%" (
    echo Missing config: %CONFIG_FILE%
    exit /b 1
)

call "%CONFIG_FILE%"

if "%EXOS_REMOTE_REPO%"=="" (
    echo EXOS_REMOTE_REPO is not set in %CONFIG_FILE%
    exit /b 1
)

set "REMOTE_SCRIPT=%~1"
if "%REMOTE_SCRIPT%"=="" (
    echo Usage: %~nx0 ^<script_path^> [args...]
    exit /b 1
)
shift

set "REMOTE_ARGS=%*"
if defined REMOTE_ARGS set "REMOTE_ARGS= %REMOTE_ARGS%"

set "REMOTE_CMD=cd \"%EXOS_REMOTE_REPO%\" && \"%REMOTE_SCRIPT%\"%REMOTE_ARGS%"

if not "%EXOS_SSH_PASS%"=="" (
    where plink >nul 2>nul
    if errorlevel 1 (
        echo plink is required when EXOS_SSH_PASS is set.
        echo Install PuTTY or configure key-based auth and clear EXOS_SSH_PASS.
        exit /b 1
    )
    plink -ssh -P %EXOS_SSH_PORT% -l %EXOS_SSH_USER% -pw %EXOS_SSH_PASS% %EXOS_SSH_HOST% "%REMOTE_CMD%"
    exit /b %errorlevel%
)

ssh -p %EXOS_SSH_PORT% %EXOS_SSH_USER%@%EXOS_SSH_HOST% "%REMOTE_CMD%"
exit /b %errorlevel%
