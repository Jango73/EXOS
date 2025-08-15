@echo off
REM Send ARP requests repeatedly to generate L2 traffic seen by the VM NIC.
REM REQUIREMENTS: nping (comes with Nmap)
REM USAGE: ping-vm.bat <iface-name> <target-ip>
REM Example: ping-vm.bat Ethernet 192.168.56.10

set IFACE=%1
set IP=%2
if "%IFACE%"=="" set IFACE=Ethernet
if "%IP%"=="" set IP=192.168.56.10

echo [INFO] Sending ARP to %IP% on %IFACE% (Ctrl-C to stop)
:loop
nping --arp --dest-mac ff:ff:ff:ff:ff:ff --interface "%IFACE%" %IP% >nul
ping -n 2 127.0.0.1 >nul
goto loop
