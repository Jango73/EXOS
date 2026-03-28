# Realtek Network Driver Spec Gaps

This document records the confirmed gaps found while comparing the EXOS
`RTL8139` and `RTL8169` family drivers against the published Realtek register
documentation and programming guides.

## Sources

- Realtek RTL8139C(L) datasheet mirror:
  `https://www.alldatasheet.com/datasheet-pdf/pdf/1134609/REALTEK/RTL8139C.html`
- Realtek RTL8139D datasheet mirror:
  `https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139D_DataSheet.pdf`
- Realtek RTL8139 Programming Guide mirror:
  `https://www.cs.usfca.edu/~cruse/cs326f04/RTL8139_ProgrammersGuide.pdf`
- Realtek RTL8169 manual mirror:
  `https://www.manualslib.com/manual/1612965/Realtek-Rtl8169.html`

## RTL8139

### Corrected

- `TSD.OWN` ownership handling was inverted in transmit scheduling.
  The hardware uses `OWN=1` for a descriptor that is idle and owned by the
  driver. EXOS rejected that state and attempted to transmit when `OWN=0`.
- `CAPR` was initialized to `0` and reset to `0` on RX error.
  The RTL8139 programming model uses the current packet read pointer minus 16,
  which yields `0xFFF0` at ring offset `0`.
- `MAR0..MAR7` were never initialized although `RCR.AM` was enabled.
  The datasheet states that the driver is responsible for initializing the
  multicast filter registers and that writes must use DWORD accesses.
- `TCR` was restored from a raw `TXCONFIG` snapshot captured before reset.
  This reused control bits as if they were only a revision identifier.
- `TCR` was written before `TE` was enabled.
  The RTL8139 documentation restricts `TCR` changes to the transmit-enabled
  state.
- Interrupt acknowledgment for `RxBufOvw` and `RxFIFOOvw` happened before the
  receive path advanced `CAPR`.
  The Realtek programming guide requires advancing `CAPR` first for overflow
  dismissal.

### Still limited

- `RCR` uses a conservative receive configuration and does not yet expose
  programmable multicast hash population or alternative receive policies.
- The implementation remains focused on the QEMU-compatible `0x8139` path and
  does not yet carry revision-specific quirk handling for nearby boards.

## RTL8169 / RTL8111 / RTL8168 / RTL8411

### Corrected

- `MAR0..MAR7` were not initialized while multicast acceptance was enabled.
  The register description states that the driver must initialize the
  multicast filter.
- `RXMAXSIZE` was programmed with `1500`, which is the payload MTU rather than
  a full receive-frame ceiling.
  The register limits received packet size, so EXOS now programs it to the
  receive buffer size used by the descriptor ring.

### Confirmed remaining gaps

- The driver still treats the family as one generic descriptor engine and does
  not apply revision-specific setup sequences for `RTL8111`, `RTL8168`, or
  `RTL8411`.
  The device table already distinguishes families, but controller
  initialization does not branch on those differences yet.
- RX completion handling accepts any descriptor whose `OWN` bit is clear and
  whose length is plausible.
  It does not yet validate descriptor status bits for error, fragment, or
  multi-descriptor receive cases.
- PHY and MAC tuning remains minimal.
  The driver reads link state from `PHYSTATUS`, but it does not yet implement
  family-specific PHY programming, timer policy, or extended configuration
  registers used by later PCIe revisions.

## Shared Realtek Common Layer

### Corrected

- Interrupt handling gained a shared "acknowledge after poll" path so a family
  driver can defer clearing status bits until after it has completed the
  required register maintenance.
- A shared multicast-filter clearing helper was added so both Realtek driver
  families initialize `MAR0..MAR7` with the required DWORD writes.
