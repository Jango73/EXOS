@echo off
REM --- Print local IPv4 addresses (excluding loopback and APIPA) ---
REM Comments and logs in English per your rules
echo Getting local IPv4 addresses...
powershell -NoProfile -Command ^
    "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | ForEach-Object { Write-Host ('  ' + $_.IPAddress + '  (' + $_.InterfaceAlias + ')') }"

echo.
echo If you only need local testing, open: http://localhost:8000
echo To access from another device on the LAN, use one of the IPs above with port 8000.
echo.

REM --- Start Python HTTP server ---
echo Starting Python HTTP server on port 8000...
python -m http.server 8000
pause
