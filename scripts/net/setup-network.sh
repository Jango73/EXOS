#!/bin/bash
# Setup TAP interface for QEMU networking

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (or use sudo)"
    exit 1
fi

# Create TAP interface
if ! ip link show tap0 >/dev/null 2>&1; then
    echo "Creating TAP interface tap0..."
    ip tuntap add dev tap0 mode tap user $SUDO_USER
    ip link set dev tap0 up
    ip addr add 192.168.56.1/24 dev tap0
    echo "TAP interface tap0 created and configured with IP 192.168.56.1/24"
else
    echo "TAP interface tap0 already exists"

    # Ensure interface is up
    if ! ip link show tap0 | grep -q "state UP"; then
        echo "Bringing up tap0 interface..."
        ip link set dev tap0 up
    fi

    # Check if IP address is configured
    if ! ip addr show tap0 | grep -q "192.168.56.1/24"; then
        echo "Adding IP address 192.168.56.1/24 to tap0..."
        ip addr add 192.168.56.1/24 dev tap0
    else
        echo "IP address 192.168.56.1/24 already configured on tap0"
    fi
fi

# Enable IP forwarding (required for internet access)
if [ "$(cat /proc/sys/net/ipv4/ip_forward)" != "1" ]; then
    echo "Enabling IP forwarding..."
    echo 1 > /proc/sys/net/ipv4/ip_forward
else
    echo "IP forwarding already enabled"
fi

# Get the default route interface
DEFAULT_IFACE=$(ip route | grep default | awk '{print $5}' | head -1)

if [ -n "$DEFAULT_IFACE" ]; then
    echo "Setting up NAT from tap0 to $DEFAULT_IFACE"

    # Clear existing NAT rules for tap0
    iptables -t nat -D POSTROUTING -s 192.168.56.0/24 -o $DEFAULT_IFACE -j MASQUERADE 2>/dev/null || true
    iptables -D FORWARD -i tap0 -o $DEFAULT_IFACE -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -i $DEFAULT_IFACE -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -i tap0 -o tap0 -j ACCEPT 2>/dev/null || true
    iptables -D INPUT -i tap0 -j ACCEPT 2>/dev/null || true
    iptables -D OUTPUT -o tap0 -j ACCEPT 2>/dev/null || true

    # Add NAT rules for internet access
    iptables -t nat -A POSTROUTING -s 192.168.56.0/24 -o $DEFAULT_IFACE -j MASQUERADE
    iptables -A FORWARD -i tap0 -o $DEFAULT_IFACE -j ACCEPT
    iptables -A FORWARD -i $DEFAULT_IFACE -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT

    # Add rules for local communication on tap0 (VM to host)
    iptables -A FORWARD -i tap0 -o tap0 -j ACCEPT
    iptables -A INPUT -i tap0 -j ACCEPT
    iptables -A OUTPUT -o tap0 -j ACCEPT

    echo "NAT rules configured for internet access via $DEFAULT_IFACE"

    # Show the rules for verification
    echo "Current NAT rules:"
    iptables -t nat -L POSTROUTING -n | grep 192.168.56 || echo "No NAT rules found"

    echo "Current FORWARD rules:"
    iptables -L FORWARD -n | grep tap0 || echo "No forward rules found"
else
    echo "Error: Could not determine default interface for NAT"
    exit 1
fi

echo "Network setup complete."
echo "VM should use IP 192.168.56.10"
echo "Host is accessible at 192.168.56.1"
echo "NAT configured for internet access via $DEFAULT_IFACE"
