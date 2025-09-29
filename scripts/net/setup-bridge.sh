#!/bin/bash
# Setup bridge network for QEMU networking
# This creates a proper bridge instead of NAT to allow bidirectional traffic

set -e

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (or use sudo)"
    exit 1
fi

BRIDGE_NAME="br0"
TAP_NAME="tap0"
BRIDGE_IP="192.168.56.1/24"
NETWORK="192.168.56.0/24"

echo "Setting up bridge network for EXOS VM..."

# Create bridge interface
if ! ip link show $BRIDGE_NAME >/dev/null 2>&1; then
    echo "Creating bridge interface $BRIDGE_NAME..."
    ip link add name $BRIDGE_NAME type bridge
    ip link set dev $BRIDGE_NAME up
    ip addr add $BRIDGE_IP dev $BRIDGE_NAME
    echo "Bridge $BRIDGE_NAME created and configured with IP $BRIDGE_IP"
else
    echo "Bridge $BRIDGE_NAME already exists"

    # Ensure bridge is up
    if ! ip link show $BRIDGE_NAME | grep -q "state UP"; then
        echo "Bringing up bridge $BRIDGE_NAME..."
        ip link set dev $BRIDGE_NAME up
    fi

    # Check if IP address is configured
    if ! ip addr show $BRIDGE_NAME | grep -q "192.168.56.1/24"; then
        echo "Adding IP address $BRIDGE_IP to $BRIDGE_NAME..."
        ip addr add $BRIDGE_IP dev $BRIDGE_NAME
    else
        echo "IP address $BRIDGE_IP already configured on $BRIDGE_NAME"
    fi
fi

# Create TAP interface
if ! ip link show $TAP_NAME >/dev/null 2>&1; then
    echo "Creating TAP interface $TAP_NAME..."
    ip tuntap add dev $TAP_NAME mode tap user $SUDO_USER
    ip link set dev $TAP_NAME up
    echo "TAP interface $TAP_NAME created"
else
    echo "TAP interface $TAP_NAME already exists"

    # Ensure TAP is up
    if ! ip link show $TAP_NAME | grep -q "state UP"; then
        echo "Bringing up TAP interface $TAP_NAME..."
        ip link set dev $TAP_NAME up
    fi
fi

# Add TAP to bridge
if ! ip link show $TAP_NAME | grep -q "master $BRIDGE_NAME"; then
    echo "Adding $TAP_NAME to bridge $BRIDGE_NAME..."
    ip link set dev $TAP_NAME master $BRIDGE_NAME
    echo "$TAP_NAME added to bridge $BRIDGE_NAME"
else
    echo "$TAP_NAME already in bridge $BRIDGE_NAME"
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
    echo "Setting up NAT from $BRIDGE_NAME to $DEFAULT_IFACE"

    # Clear existing NAT rules for the bridge network
    iptables -t nat -D POSTROUTING -s $NETWORK -o $DEFAULT_IFACE -j MASQUERADE 2>/dev/null || true
    iptables -D FORWARD -i $BRIDGE_NAME -o $DEFAULT_IFACE -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -i $DEFAULT_IFACE -o $BRIDGE_NAME -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -i $BRIDGE_NAME -o $BRIDGE_NAME -j ACCEPT 2>/dev/null || true
    iptables -D INPUT -i $BRIDGE_NAME -j ACCEPT 2>/dev/null || true
    iptables -D OUTPUT -o $BRIDGE_NAME -j ACCEPT 2>/dev/null || true

    # Add NAT rules for internet access
    iptables -t nat -A POSTROUTING -s $NETWORK -o $DEFAULT_IFACE -j MASQUERADE
    iptables -A FORWARD -i $BRIDGE_NAME -o $DEFAULT_IFACE -j ACCEPT
    iptables -A FORWARD -i $DEFAULT_IFACE -o $BRIDGE_NAME -m state --state RELATED,ESTABLISHED -j ACCEPT

    # Add rules for local communication on bridge (VM to host and VM to VM)
    iptables -A FORWARD -i $BRIDGE_NAME -o $BRIDGE_NAME -j ACCEPT
    iptables -A INPUT -i $BRIDGE_NAME -j ACCEPT
    iptables -A OUTPUT -o $BRIDGE_NAME -j ACCEPT

    echo "NAT rules configured for internet access via $DEFAULT_IFACE"

    # Show the rules for verification
    echo "Current NAT rules:"
    iptables -t nat -L POSTROUTING -n | grep 192.168.56 || echo "No NAT rules found"

    echo "Current FORWARD rules:"
    iptables -L FORWARD -n | grep "192.168.56\|$BRIDGE_NAME" || echo "No forward rules found"
else
    echo "Error: Could not determine default interface for NAT"
    exit 1
fi

echo "Bridge network setup complete."
echo "Bridge: $BRIDGE_NAME with IP $BRIDGE_IP"
echo "TAP: $TAP_NAME attached to bridge"
echo "VM should use IP 192.168.56.10"
echo "Host is accessible at 192.168.56.1"
echo "NAT configured for internet access via $DEFAULT_IFACE"

# Show final network configuration
echo ""
echo "Network configuration:"
echo "Bridge interfaces:"
ip link show type bridge
echo ""
echo "Bridge details:"
ip addr show $BRIDGE_NAME 2>/dev/null || echo "Bridge not found"
echo ""
echo "TAP interface:"
ip addr show $TAP_NAME 2>/dev/null || echo "TAP not found"

# Setup network debugging tools capabilities
echo "Configuring network debugging tools capabilities..."

# List of network tools that need capabilities
declare -A NETWORK_TOOLS=(
    ["/usr/bin/tcpdump"]="cap_net_raw,cap_net_admin=eip"
    ["/usr/sbin/tcpdump"]="cap_net_raw,cap_net_admin=eip"
    ["/usr/bin/wireshark"]="cap_net_raw,cap_net_admin=eip"
    ["/usr/bin/tshark"]="cap_net_raw,cap_net_admin=eip"
    ["/usr/bin/nmap"]="cap_net_raw,cap_net_admin,cap_net_bind_service=eip"
    ["/usr/sbin/nmap"]="cap_net_raw,cap_net_admin,cap_net_bind_service=eip"
    ["/usr/bin/ping"]="cap_net_raw=ep"
    ["/bin/ping"]="cap_net_raw=ep"
    ["/usr/bin/arping"]="cap_net_raw=ep"
    ["/usr/sbin/arping"]="cap_net_raw=ep"
    ["/usr/bin/ncat"]="cap_net_bind_service=ep"
    ["/usr/bin/socat"]="cap_net_bind_service=ep"
)

for tool_path in "${!NETWORK_TOOLS[@]}"; do
    if [ -f "$tool_path" ]; then
        required_caps="${NETWORK_TOOLS[$tool_path]}"
        current_caps=$(getcap "$tool_path" 2>/dev/null || echo "none")

        if [[ "$current_caps" != *"$required_caps"* ]]; then
            echo "Setting capabilities for $(basename $tool_path)..."
            setcap "$required_caps" "$tool_path"
        else
            echo "$(basename $tool_path) capabilities already configured"
        fi
    fi
done

echo "Network debugging tools capabilities configured"
