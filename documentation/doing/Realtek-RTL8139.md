# Realtek RTL8139 Driver Plan

This plan defines a dedicated EXOS driver for the Realtek `RTL8139` Ethernet controller family.

The goal is to support the QEMU-exposed `rtl8139` device with a driver that stays cleanly separated from the `RTL8111 / RTL8168 / RTL8411` PCIe Gigabit family.

The implementation must avoid folding `RTL8139` support into `RTL8169.c`. The two lines share the same broad Ethernet-driver shape, but they do not share the same hardware model.

This family grouping is the right target because:
- [ ] QEMU exposes `rtl8139` directly
- [ ] it provides a realistic second non-Intel Ethernet path for EXOS
- [ ] it is close enough to reuse EXOS network-driver integration patterns
- [ ] it is different enough to require its own hardware-specific driver

## Scope

### In scope
- [ ] PCI detection and attach for the `RTL8139` family
- [ ] MMIO or I/O register access as required by the controller
- [ ] MAC retrieval
- [ ] RX and TX data path
- [ ] link-state reporting through `DF_NT_GETINFO`
- [ ] polling mode
- [ ] legacy interrupt mode if stable
- [ ] integration with `NetworkManager`
- [ ] bring-up on the QEMU `rtl8139` device

### Out of scope for the first milestone
- [ ] Wake-on-LAN
- [ ] power-management tuning
- [ ] checksum offload
- [ ] advanced hardware tuning for every board-specific revision
- [ ] support for unrelated Realtek Ethernet families such as `RTL8111 / RTL8168 / RTL8411`

## Driver family strategy

### Step 0 - Define the supported family
- [x] create `kernel/include/drivers/network/RTL8139.h`
- [x] create `kernel/source/drivers/network/RTL8139.c`
- [x] use one EXOS driver name for the `RTL8139` family
- [x] model the supported PCI IDs as a table of matches, not hardcoded single-device checks

Recommended naming:
- [x] use `RTL8139` as the source file family name

Rationale:
- `RTL8139` is a separate hardware family
- it must not be mixed into the `RTL8169` family files
- EXOS should keep one hardware family per driver file unless the register model is truly shared

### Step 1 - Start with the minimum safe PCI ID set
- [x] add the QEMU-compatible `RTL8139` PCI identifier first
- [ ] extend with nearby `RTL8139` family variants only when the hardware model is confirmed compatible
- [x] keep the match table data-driven so new IDs are added without restructuring the driver

## Relationship with the RTL8111 / RTL8168 / RTL8411 family

The `RTL8139` family is related to `RTL8111 / RTL8168 / RTL8411` only at a high architectural level:
- both are Realtek Ethernet controllers
- both use PCI discovery
- both need reset, MAC retrieval, RX and TX integration, and interrupt or polling support
- both plug into the same EXOS `NetworkManager` contract

However, they must remain separate driver implementations:
- `RTL8139` is an older family
- the hardware programming model differs
- register layout differs
- initialization sequencing differs
- RX and TX buffer management differs
- interrupt details differ
- revision quirks must be tracked separately

Driver consequence:
- `RTL8139` must have its own source and header files
- common EXOS integration concepts may be reused
- hardware-specific logic must not be merged with `RTL8169.c` through local branching

## Implementation steps

### Step 2 - Clone the E1000 and RTL8169 integration shape, not the hardware logic
- [x] copy the structural integration points from `E1000.c` and `RTL8169.c` only as references
- [x] keep the same driver contract: `DF_NT_RESET`, `DF_NT_GETINFO`, `DF_NT_SEND`, `DF_NT_POLL`, `DF_NT_SETRXCB`
- [x] register the driver through `KernelData.c`
- [x] keep `NetworkManager` unchanged except for normal attach and initialization flow

Acceptance:
- [ ] the `RTL8139` device appears in `GetPCIDeviceList()`
- [ ] `SystemDataView` shows the controller as kernel-attached
- [ ] the attached driver type is `DRIVER_TYPE_NETWORK`
- [ ] `NetworkManager_FindNetworkDevices()` picks it up without special-case code

### Step 3 - Build a reusable hardware-description layer inside the driver
- [x] define a compact per-device info table for revision-specific quirks
- [x] keep register offsets, flags, and RX or TX metadata in one shared header
- [x] separate common ring or buffer logic from revision-specific setup
- [x] avoid scattering magic values through probe, reset, TX, and RX paths

Acceptance:
- [x] adding a new compatible `RTL8139` family PCI ID is done by extending a table, not by duplicating functions

### Step 4 - Probe and register mapping
- [x] validate `PCI_CLASS_NETWORK` and `PCI_SUBCLASS_ETHERNET`
- [x] enable bus mastering and the controller register access mode required by the device
- [x] decode and map the active register BAR
- [x] validate that the mapped register block is readable
- [x] read hardware revision information early and store it in the device context

Acceptance:
- [ ] driver logs show stable probe, mapped BAR, and revision identification

### Step 5 - Reset and baseline device initialization
- [x] implement software reset with timeout handling
- [x] wait for the controller to leave reset cleanly
- [x] apply the minimum required initialization sequence for RX and TX enablement
- [x] leave advanced offloads disabled for the first version
- [x] keep link configuration conservative and stable

Acceptance:
- [ ] controller reaches a quiet idle state after probe
- [ ] repeated boot does not hang in reset or leave the device wedged

### Step 6 - MAC address and link reporting
- [x] read the permanent MAC address from the correct register path
- [x] populate `NETWORK_INFO.MAC`
- [x] report `LinkUp`, `SpeedMbps`, `DuplexFull`, and `MTU`
- [x] use `1500` MTU in the first implementation unless hardware setup requires another default

Acceptance:
- [ ] `network devices` and `SystemDataView` show a valid MAC address
- [ ] link up or down state matches the actual cable state

### Step 7 - RX and TX buffers
- [ ] define `RTL8139` transmit and receive data structures in the driver header
- [ ] allocate the RX receive area required by the controller model
- [ ] allocate TX buffers or descriptor state as required by the controller model
- [ ] program the controller with the active RX and TX memory addresses
- [ ] implement one-frame TX path first
- [ ] implement RX polling first, even before interrupts

Acceptance:
- [ ] a raw Ethernet frame can be transmitted
- [ ] received frames reach the registered RX callback in polling mode

### Step 8 - Polling-only network bring-up
- [ ] implement `DF_NT_POLL`
- [ ] make `NetworkManager` operate on the `RTL8139` device in polling mode first
- [ ] validate ARP, IPv4, DHCP, UDP, and TCP without interrupt dependency

Acceptance:
- [ ] `sys_info` or `network devices` reports initialized state
- [ ] DHCP succeeds on a working network
- [ ] `netget` can download over the `RTL8139` controller in polling mode

### Step 9 - Legacy interrupt support
- [ ] add legacy interrupt support through `DeviceInterruptRegister`
- [ ] implement a small top half
- [ ] move RX processing to deferred work or poll-style bottom-half handling when needed
- [ ] keep interrupt acknowledgement and re-arm logic explicit and traceable

Acceptance:
- [ ] RX works with interrupts enabled
- [ ] no interrupt storm appears in shared-IRQ situations

### Step 10 - Revision handling for more RTL8139 variants
- [ ] identify the smallest set of quirks needed for stable support across the targeted `RTL8139` variants
- [ ] store those quirks in a data table
- [ ] keep the common fast path shared
- [ ] only split initialization stages when real hardware differences require it

Acceptance:
- [ ] the QEMU `rtl8139` device works
- [ ] at least one additional compatible `RTL8139` family board works without code duplication

## Required architecture rules

### No local hacks
- [ ] do not add QEMU-specific code paths outside normal capability or quirk tables
- [ ] do not special-case one PCI ID outside the match and quirk tables

### Reuse shared kernel patterns
- [ ] reuse `DeviceInterrupt` for interrupt routing
- [ ] reuse `DeferredWork` for bottom-half processing when required
- [ ] reuse the existing `NetworkManager` attach and maintenance flow
- [ ] if a generic network DMA or ring helper becomes necessary for multiple drivers, place it in `kernel/include/utils` and `kernel/source/utils`

### Logging rules
- [ ] all logs must keep the `[FunctionName]` prefix
- [ ] warnings and errors must stay short and human-readable
- [ ] raw register dumps must stay under `DEBUG()`

## Validation checklist

### Bring-up validation
- [ ] PCI detection succeeds on the QEMU `rtl8139` device
- [ ] driver attaches automatically
- [ ] MAC address is stable across boots
- [ ] link state behaves consistently across repeated boots

### Network validation
- [ ] ARP request and reply flow works
- [ ] DHCP lease acquisition works
- [ ] IPv4 send and receive works
- [ ] UDP socket send and receive works
- [ ] TCP connection establishment works
- [ ] `system/netget` works on the `RTL8139` controller

### Stability validation
- [ ] no boot hang when cable is absent
- [ ] no boot hang when cable is present
- [ ] no interrupt flood after link changes
- [ ] repeated reboot does not leave the controller in a broken state

## Recommended delivery order

1. [ ] add the new driver skeleton and PCI ID table
2. [ ] implement probe, reset, register mapping, and MAC read
3. [ ] implement TX and RX polling
4. [ ] validate DHCP and `netget` on QEMU `rtl8139`
5. [ ] add legacy interrupt mode
6. [ ] extend the PCI ID and quirk tables for more `RTL8139` variants

## Completion criteria

- [ ] the QEMU `rtl8139` device is fully usable as an EXOS network device
- [ ] the driver covers the `RTL8139` family without leaking into `RTL8169` family code
- [ ] the driver integrates with the existing EXOS network stack without local architecture exceptions
