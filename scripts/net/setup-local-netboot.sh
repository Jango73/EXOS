#!/bin/bash
set -e

function Usage() {
    echo "Usage: $0 [--arch <x86-32|x86-64>] [--interface <name>] [--http-root <path>] [--tftp-root <path>] [--dnsmasq-conf <path>] [--ipxe-binary <path>] [--predator-mac <aa:bb:cc:dd:ee:ff>] [--predator-ip <a.b.c.d>] [--dry-run]"
}

function RequireCommand() {
    local CommandName="$1"
    if ! command -v "$CommandName" >/dev/null 2>&1; then
        echo "Missing command: $CommandName"
        exit 1
    fi
}

function IpToInt() {
    local A B C D
    IFS='.' read -r A B C D <<< "$1"
    echo $((((A << 24) | (B << 16) | (C << 8) | D)))
}

function IntToIp() {
    local Value="$1"
    echo "$(((Value >> 24) & 255)).$(((Value >> 16) & 255)).$(((Value >> 8) & 255)).$((Value & 255))"
}

function ValidateIpv4() {
    local Ip="$1"
    local A B C D Extra
    IFS='.' read -r A B C D Extra <<< "$Ip"
    if [ -n "$Extra" ] || [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ] || [ -z "$D" ]; then
        return 1
    fi
    for Part in "$A" "$B" "$C" "$D"; do
        if ! [[ "$Part" =~ ^[0-9]+$ ]] || [ "$Part" -lt 0 ] || [ "$Part" -gt 255 ]; then
            return 1
        fi
    done
    return 0
}

function DetectInterface() {
    local Detected
    Detected="$(ip route show default 2>/dev/null | awk 'NR == 1 { print $5; exit }')"
    if [ -n "$Detected" ]; then
        echo "$Detected"
        return
    fi

    Detected="$(ip -o link show up | awk -F': ' '$2 != "lo" { print $2; exit }')"
    if [ -n "$Detected" ]; then
        echo "$Detected"
        return
    fi

    echo ""
}

function FindIpxeBinary() {
    local Arch="$1"
    local Candidate
    local -a Candidates

    if [ "$Arch" = "x86-64" ]; then
        Candidates=(
            "/usr/lib/ipxe/ipxe.efi"
            "/usr/lib/ipxe/snponly.efi"
            "/usr/lib/ipxe/snp.efi"
            "/usr/share/ipxe/ipxe.efi"
        )
    else
        Candidates=(
            "/usr/lib/ipxe/ipxe32.efi"
            "/usr/lib/ipxe/i386-efi/ipxe.efi"
            "/usr/share/ipxe/ipxe32.efi"
        )
    fi

    for Candidate in "${Candidates[@]}"; do
        if [ -f "$Candidate" ]; then
            echo "$Candidate"
            return
        fi
    done

    echo ""
}

ARCH="x86-64"
IFACE=""
HTTP_ROOT="/var/www/html/exos"
TFTP_ROOT="/srv/tftp"
DNSMASQ_CONF="/etc/dnsmasq.d/exos-netboot.conf"
IPXE_BINARY=""
PREDATOR_MAC=""
PREDATOR_IP=""
DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --arch"; Usage; exit 1; }
            ARCH="$1"
            ;;
        --interface)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --interface"; Usage; exit 1; }
            IFACE="$1"
            ;;
        --http-root)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --http-root"; Usage; exit 1; }
            HTTP_ROOT="$1"
            ;;
        --tftp-root)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --tftp-root"; Usage; exit 1; }
            TFTP_ROOT="$1"
            ;;
        --dnsmasq-conf)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --dnsmasq-conf"; Usage; exit 1; }
            DNSMASQ_CONF="$1"
            ;;
        --ipxe-binary)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --ipxe-binary"; Usage; exit 1; }
            IPXE_BINARY="$1"
            ;;
        --predator-mac)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --predator-mac"; Usage; exit 1; }
            PREDATOR_MAC="$1"
            ;;
        --predator-ip)
            shift
            [ $# -gt 0 ] || { echo "Missing value for --predator-ip"; Usage; exit 1; }
            PREDATOR_IP="$1"
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        --help|-h)
            Usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            Usage
            exit 1
            ;;
    esac
    shift
done

case "$ARCH" in
    x86-32|x86-64)
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

if [ -n "$PREDATOR_MAC" ] && ! [[ "$PREDATOR_MAC" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then
    echo "Invalid --predator-mac: $PREDATOR_MAC"
    exit 1
fi

if [ -n "$PREDATOR_IP" ] && ! ValidateIpv4 "$PREDATOR_IP"; then
    echo "Invalid --predator-ip: $PREDATOR_IP"
    exit 1
fi

if [ -n "$PREDATOR_MAC" ] && [ -z "$PREDATOR_IP" ]; then
    echo "When --predator-mac is set, --predator-ip is required."
    exit 1
fi

if [ -z "$PREDATOR_MAC" ] && [ -n "$PREDATOR_IP" ]; then
    echo "When --predator-ip is set, --predator-mac is required."
    exit 1
fi

RequireCommand ip
RequireCommand awk
RequireCommand install
RequireCommand dnsmasq

if [ -z "$IFACE" ]; then
    IFACE="$(DetectInterface)"
fi

if [ -z "$IFACE" ]; then
    echo "Cannot detect network interface. Use --interface."
    exit 1
fi

IFACE_CIDR="$(ip -o -4 addr show dev "$IFACE" scope global | awk 'NR == 1 { print $4; exit }')"
if [ -z "$IFACE_CIDR" ]; then
    echo "No IPv4 address on interface $IFACE"
    exit 1
fi

SERVER_IP="${IFACE_CIDR%/*}"
PREFIX="${IFACE_CIDR#*/}"

if ! [[ "$PREFIX" =~ ^[0-9]+$ ]] || [ "$PREFIX" -lt 16 ] || [ "$PREFIX" -gt 30 ]; then
    echo "Unsupported network prefix /$PREFIX on $IFACE. Use /16 to /30."
    exit 1
fi

SERVER_INT="$(IpToInt "$SERVER_IP")"
MASK_INT=$((0xFFFFFFFF ^ ((1 << (32 - PREFIX)) - 1)))
NETWORK_INT=$((SERVER_INT & MASK_INT))
BROADCAST_INT=$((NETWORK_INT | (0xFFFFFFFF ^ MASK_INT)))

RANGE_START_INT=$((NETWORK_INT + 50))
RANGE_END_INT=$((NETWORK_INT + 150))
MIN_CLIENT_INT=$((NETWORK_INT + 2))
MAX_CLIENT_INT=$((BROADCAST_INT - 2))

if [ "$RANGE_START_INT" -lt "$MIN_CLIENT_INT" ]; then
    RANGE_START_INT="$MIN_CLIENT_INT"
fi
if [ "$RANGE_END_INT" -gt "$MAX_CLIENT_INT" ]; then
    RANGE_END_INT="$MAX_CLIENT_INT"
fi

if [ "$RANGE_START_INT" -gt "$RANGE_END_INT" ]; then
    echo "Cannot derive DHCP range from $IFACE_CIDR"
    exit 1
fi

DHCP_RANGE_START="$(IntToIp "$RANGE_START_INT")"
DHCP_RANGE_END="$(IntToIp "$RANGE_END_INT")"
NETWORK_IP="$(IntToIp "$NETWORK_INT")"
NETMASK_IP="$(IntToIp "$MASK_INT")"

if [ -n "$PREDATOR_IP" ]; then
    PREDATOR_IP_INT="$(IpToInt "$PREDATOR_IP")"
    if [ "$PREDATOR_IP_INT" -le "$NETWORK_INT" ] || [ "$PREDATOR_IP_INT" -ge "$BROADCAST_INT" ]; then
        echo "Predator IP $PREDATOR_IP is outside subnet $NETWORK_IP/$PREFIX"
        exit 1
    fi
fi

if [ -z "$IPXE_BINARY" ]; then
    IPXE_BINARY="$(FindIpxeBinary "$ARCH")"
fi
if [ -z "$IPXE_BINARY" ] || [ ! -f "$IPXE_BINARY" ]; then
    echo "Cannot find iPXE EFI binary for $ARCH."
    echo "Install it or pass --ipxe-binary <path>."
    exit 1
fi

if [ "$ARCH" = "x86-64" ]; then
    DHCP_MATCH="dhcp-match=set:efi64,option:client-arch,7"
    DHCP_BOOT="dhcp-boot=tag:efi64,ipxe.efi"
    EFI_BOOT_FILE="BOOTX64.EFI"
    IPXE_NAME="ipxe.efi"
    ARCH_FOLDER="x86-64"
else
    DHCP_MATCH="dhcp-match=set:efi32,option:client-arch,6"
    DHCP_BOOT="dhcp-boot=tag:efi32,ipxe32.efi"
    EFI_BOOT_FILE="BOOTIA32.EFI"
    IPXE_NAME="ipxe32.efi"
    ARCH_FOLDER="x86-32"
fi

PREDATOR_RESERVATION_LINE=""
if [ -n "$PREDATOR_MAC" ]; then
    PREDATOR_RESERVATION_LINE="dhcp-host=$PREDATOR_MAC,$PREDATOR_IP,12h"
fi

read -r -d '' DNSMASQ_CONTENT <<EOF || true
# Generated by scripts/net/setup-local-netboot.sh
interface=$IFACE
bind-interfaces

dhcp-range=$DHCP_RANGE_START,$DHCP_RANGE_END,$NETMASK_IP,12h
dhcp-option=option:router,$SERVER_IP
dhcp-option=option:dns-server,$SERVER_IP

enable-tftp
tftp-root=$TFTP_ROOT
$DHCP_MATCH
$DHCP_BOOT
$PREDATOR_RESERVATION_LINE
EOF

read -r -d '' IPXE_SCRIPT <<EOF || true
#!ipxe
set server $SERVER_IP
menu EXOS Netboot
item latest   latest (debug courant)
item previous previous (rollback)
item safe     safe (stable)
choose target || goto cancel

:latest
chain http://\${server}/exos/latest/$ARCH_FOLDER/EFI/BOOT/$EFI_BOOT_FILE || goto failed
:previous
chain http://\${server}/exos/previous/$ARCH_FOLDER/EFI/BOOT/$EFI_BOOT_FILE || goto failed
:safe
chain http://\${server}/exos/safe/$ARCH_FOLDER/EFI/BOOT/$EFI_BOOT_FILE || goto failed

:failed
echo Chainload failed
sleep 2
goto latest

:cancel
exit
EOF

echo "[setup-local-netboot] Interface      : $IFACE ($IFACE_CIDR)"
echo "[setup-local-netboot] Network        : $NETWORK_IP/$PREFIX"
echo "[setup-local-netboot] DHCP range     : $DHCP_RANGE_START - $DHCP_RANGE_END"
echo "[setup-local-netboot] HTTP root      : $HTTP_ROOT"
echo "[setup-local-netboot] TFTP root      : $TFTP_ROOT"
echo "[setup-local-netboot] dnsmasq conf   : $DNSMASQ_CONF"
echo "[setup-local-netboot] iPXE binary    : $IPXE_BINARY"
if [ -n "$PREDATOR_MAC" ]; then
    echo "[setup-local-netboot] Predator DHCP  : $PREDATOR_MAC -> $PREDATOR_IP"
fi

if [ "$DRY_RUN" -eq 1 ]; then
    echo "[setup-local-netboot] Dry run enabled; no changes applied."
    exit 0
fi

if [ "$EUID" -ne 0 ]; then
    echo "Run as root (sudo)."
    exit 1
fi

install -d "$TFTP_ROOT"
install -d "$HTTP_ROOT"
install -d "$HTTP_ROOT/latest/$ARCH_FOLDER"
install -d "$HTTP_ROOT/previous/$ARCH_FOLDER"
install -d "$HTTP_ROOT/safe/$ARCH_FOLDER"
install -m 0644 "$IPXE_BINARY" "$TFTP_ROOT/$IPXE_NAME"
printf "%s\n" "$DNSMASQ_CONTENT" > "$DNSMASQ_CONF"
printf "%s\n" "$IPXE_SCRIPT" > "$HTTP_ROOT/boot.ipxe"
chmod 0644 "$HTTP_ROOT/boot.ipxe"

if command -v systemctl >/dev/null 2>&1; then
    systemctl restart dnsmasq
else
    service dnsmasq restart
fi

echo "[setup-local-netboot] Setup complete."
echo "[setup-local-netboot] Next: deploy artifacts with scripts/net/deploy-local-netboot.sh --arch $ARCH --slot latest --rotate"
