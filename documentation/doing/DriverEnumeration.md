# Driver Enumeration Roadmap

## Goal

Provide a generic enumeration API for drivers so the shell can list
devices/ports/buses without including any driver header. Each driver
exposes a common iteration interface plus a driver-owned "pretty print"
function that formats each enumerated item.

## Requirements

- Shell must not include any driver header.
- Enumeration is done through a kernel service that returns all
  "enumerable" drivers currently loaded.
- Each driver exposes:
  - Enumeration of items (ports/bus/devices).
  - Pretty print for each item.
- Domain-specific data lives in the driver. The shell uses only the
  generic API and driver-provided formatted strings.

## Proposed Kernel API

### Generic domains

Assign U32 IDs (ENUM_DOMAIN_*) to describe each enumerable class:

- ENUM_DOMAIN_PCI_DEVICE
- ENUM_DOMAIN_AHCI_PORT
- ENUM_DOMAIN_ATA_DEVICE
- ENUM_DOMAIN_XHCI_PORT

Add others later if needed (disk list, network devices, etc.).

### Driver enumeration storage

Store enumeration capability on the driver object:

- Driver.EnumDomainCount
- Driver.EnumDomains[]

This avoids an extra DF_ENUM_CAPS call and makes the info visible from
the driver metadata itself.

### Driver enumeration structures

Use Size/Version headers for compatibility.

- DRIVER_ENUM_QUERY
  - Header.Size, Header.Version
  - Domain (U32)
  - Flags (U32)

- DRIVER_ENUM_ITEM
  - Header.Size, Header.Version
  - Domain (U32)
  - Index (U32) or ItemID (U64)
  - DataSize (U32)
  - Data[] (opaque payload owned by the driver)

- DRIVER_ENUM_PRETTY
  - Header.Size, Header.Version
  - Domain (U32)
  - Item (DRIVER_ENUM_ITEM or ItemID)
  - OutBuffer (LPSTR)
  - OutBufferSize (U32)

### Driver command IDs

Add generic driver commands (DF_ENUM_*):

- DF_ENUM_BEGIN (optional)
- DF_ENUM_NEXT
- DF_ENUM_END (optional)
- DF_ENUM_PRETTY

### Kernel service layer

Provide kernel functions used by the shell:

- KernelEnumerateDrivers(DRIVER_ENUM_QUERY, ...)
  - Returns drivers that support the domain.
- KernelDriverEnumNext(Driver, DRIVER_ENUM_QUERY, DRIVER_ENUM_ITEM)
  - Calls driver->Command(DF_ENUM_NEXT, ...).
- KernelDriverEnumPretty(Driver, DRIVER_ENUM_PRETTY)
  - Calls driver->Command(DF_ENUM_PRETTY, ...).

The shell only calls these generic functions.

## Driver Mapping (Phase 1)

Implement enumeration and pretty print in:

- PCI: ENUM_DOMAIN_PCI_DEVICE
- SATA (AHCI): ENUM_DOMAIN_AHCI_PORT
- ATA: ENUM_DOMAIN_ATA_DEVICE
- xHCI: ENUM_DOMAIN_XHCI_PORT

## Output Strategy

Pretty print is handled by the driver:

- Example (xHCI):
  - "Port 1: CCS=1 PED=1 Speed=HS Raw=0x00000000"

The shell simply displays strings returned by the driver.

## Open Choices

- Stateless iteration (index-based) vs stateful iteration (handle-based).
- Whether DF_ENUM_BEGIN/END are needed for each driver.
- Whether to allow filtering in DRIVER_ENUM_QUERY (e.g., by PCI class).
