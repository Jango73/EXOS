#!/usr/bin/env sh
# Send gratuitous ARP regularly on the bridge/tap used by the VM.
# REQUIREMENTS: arping
# USAGE: ./ping-vm.sh <iface> <ip-to-announce>
# Example: ./ping-vm.sh tap0 192.168.56.10

IFACE="${1:-eth0}"
IP="${2:-192.168.56.10}"

echo "[INFO] Sending GARP on $IFACE for $IP (Ctrl-C to stop)"
while :; do
    # -U: Unsolicited ARP (GARP), -I: interface, -c: count
    arping -U -I "$IFACE" -c 1 "$IP" >/dev/null 2>&1
    sleep 1
done
