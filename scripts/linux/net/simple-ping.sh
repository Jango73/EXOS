#!/bin/bash
# Simple ping test for IPv4 layer

echo "[INFO] Pinging 192.168.56.16 (IPv4 layer should receive ICMP packets)"

for i in {1..5}; do
    ping -c 1 -W 1 192.168.56.16 > /dev/null 2>&1
    echo "Ping $i sent to 192.168.56.16"
    sleep 1
done

echo "All IPv4 pings sent"