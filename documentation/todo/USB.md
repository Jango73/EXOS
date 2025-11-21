# USB Implementation Roadmap

## Prerequisites (one-time)

- [X] **PCI + IRQs**: enumerate classes 0x0C/0x03, MSI/MSI-X if available,
    fallback INTx.\
- [X] **MMIO & DMA**: map BAR, physically contiguous pages, 4K aligned,
    "coherent" buffers.\
- [X] **Timers**: reliable delays in ms.\
- [ ] **USB core (types)**: UsbSpeed{LS,FS,HS,SS},
    EndpointType{Control,Bulk,Intr,Iso}, UsbAddress(0..127), descriptors
    (Device/Config/Interface/Endpoint/String).\
- [X] **Logs**: compact trace per event (setup, TRB, port change, stall).

## Step 1 --- xHCI Detection (no transfer)

**Goal**: initialize the controller and read port status.\

- [ ] Find **xHCI** (PCI class 0x0C03, progIF 0x30), power-on, HCRST, R/S
off.\
- [ ] Allocate **DCBAA**, **Command Ring**, **Event Ring**, **Interrupter
0**.\
- [ ] RUN controller, read **PORTSCx** (presence, speed).\

**Success**: `usbctl ports` lists ports and their speed/state.\
**QEMU quick test**: `-device qemu-xhci` (even without device).

## Step 2 --- Minimal EP0 on single device (root, no hub)

**Goal**: do Control transfers and retrieve descriptors.\

- [ ] Implement **Control Transfer** (SETUP + DATA + STATUS) + timeouts +
STALL → CLEAR_FEATURE.\
- [ ] Standard sequence: **GET_DESCRIPTOR(Device)**, **SET_ADDRESS**,
**GET_DESCRIPTOR(Config)**.\
- [ ] No **SET_CONFIGURATION** yet (read-only).\

**Success**: `usbctl probe` shows VendorID/ProductID of directly
attached device.\
**QEMU test**: `-device usb-kbd` (xHCI + keyboard).

## Step 3 --- Device/config model (tree) + SET_CONFIGURATION

**Goal**: build UsbDevice object (chosen config, interfaces,
endpoints).\

- [ ] Parse **Configuration/Interface/Endpoint**, save strings
(UTF-16LE→UTF-8).\
- [ ] Apply **SET_CONFIGURATION(1)** by default.\

**Success**: homemade `lsusb` displays tree (addr, class, ifaces,
endpoints).\
**Independent**: no class driver needed.

## Step 4 --- HUB Driver (USB2 & USB3)

**Goal**: support real hotplug through hubs.\
- Hub class: **Interrupt IN** status change → reset port,
**GetPortStatus**, **SetPortFeature**.\
- xHCI: handle **TT** for FS/LS behind HS.\
- Reuse Step 3 enumeration for each new port.\
**Success**: plugging on a hub → new device appears / disappears
cleanly.

## Step 5 --- HID Keyboard (Boot protocol, minimal)

**Goal**: functional USB keyboard.\

- [ ] Select **HID Interface**, **Get Report Descriptor** (optional for
Boot).\
- [ ] **SET_PROTOCOL(BOOT)**, **SET_IDLE**.\
- [ ] Open **Interrupt IN** (polling via xHCI ring); 8-byte report → map to
internal HID Usage.\
- [ ] Translate HID→EXOS KeyCode (separate layout).\

**Success**: keystrokes visible in EXOS TTY (6KRO OK).\
**Independent**: nothing else required.

## Step 6 --- HID Mouse (optional but easy)

**Goal**: pointer/click.\

- [ ] Interrupt IN, parse X/Y, buttons, wheel.\

**Success**: `mouse-test` tool shows deltas/buttons.

## Step 7 --- Generic Bulk + Mass Storage (BOT) read-only

**Goal**: read a USB stick.\

- [ ] Bulk IN/OUT engine (queues, timeouts, retry).\
- [ ] MSC BOT: **CBW/CSW**, SCSI **INQUIRY**, **READ CAPACITY(10)**,
**READ(10)**.\
- [ ] Expose block device `usbms0` (read-only) → attach to existing VFS.\

**Success**: homemade `fdisk/lsblk` sees capacity, `mount` (RO) works.

## Step 8 --- Mass Storage read/write + cache/flush

**Goal**: stable RW.\

- [ ] **WRITE(10)**, write-cache, **SYNCHRONIZE CACHE**.\
- [ ] SCSI error paths (UNIT ATTENTION, NOT READY), clean re-init.\

**Success**: create a file on a FS (FAT32/EXFS) on `usbms0`.

## Step 9 --- Robust hotplug & teardown

**Goal**: unplug/insert without breaking system.\

- [ ] Cancel URB/TD, flush rings, **STOP/RESET EP**, **Disable Slot**.\
- [ ] Refcount on device/interface/endpoint objects.\

**Success**: unplug during I/O → no panic, resources freed.

## Step 10 --- CDC-ACM (USB-Serial)

**Goal**: USB serial console (external debug).\

- [ ] Control + Bulk (2 endpoints), **SET_LINE_CODING**,
[ ] **SET_CONTROL_LINE_STATE**.\

**Success**: homemade `/dev/ttyACM0`, echo works.

## Step 11 --- Isochronous (UAC1 minimal audio) --- optional

**Goal**: validate Iso pipe.\

- [ ] Open Iso EP IN/OUT, manage frame/uframe, no-retry.\

**Success**: capture or playback mono 16-bit 48 kHz for a few seconds.

## Step 12 --- Perf & comfort

- [ ] **MSI-X**, interrupt coalescing, dynamic ring sizes.\
- [ ] **Scatter/Gather (SG)** if IOMMU later.\
- [ ] **U1/U2 & Link PM** if useful, throttle HID polling.\
- [ ] Stats, simple `usbmon` (dump TRB/TD/EP events).

## Step 13 --- Controller compatibility (if really needed)

- [ ] **EHCI** (USB2 HS): QEMU/older hw; reuse layers 3+ (core, hubs,
    classes).\
- [ ] **UHCI/OHCI**: only if you target very old hw (otherwise skip).\
- [ ] HCD API must be common: `hcd_submit_control/bulk/intr/iso`,
    `hcd_port_ops`, etc.

## Step Decoupling (ensuring "runs without the next")

- 1 runs alone (port diagnostics).\
- 2 runs with direct device (descriptors), no hub/class driver.\
- 3 adds device view, not mandatory for 4/5.\
- 4 adds hotplug/hubs; 5 works even without 7/8.\
- 7 (MSC RO) only needs bulk + std requests; 8 adds write.\
- 10, 11, 12, 13 are fully orthogonal.
