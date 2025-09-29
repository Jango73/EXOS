# MBR Structure, FAT32 VBR Structure & INT 0x13 BIOS Functions

## 1. Master Boot Record (MBR)

- **Size:** 512 bytes (sector 0 of the disk)
- **Purpose:** BIOS bootloader + partition table

| Offset   | Size   | Description                        |
|----------|--------|------------------------------------|
| 0x000    | 446    | Boot code (primary bootloader)     |
| 0x1BE    | 64     | Partition table (4 × 16 bytes)     |
| 0x1FE    | 2      | Signature 0x55AA                   |

### Partition Table Entry (4 × 16 bytes)

| Offset | Size | Meaning                                              |
|--------|------|------------------------------------------------------|
| 0x00   | 1    | Status (0x80=bootable, 0x00=non-bootable)            |
| 0x01   | 1    | Start head (CHS)                                     |
| 0x02   | 1    | Start sector (bits 0–5), high cylinder bits 6–7      |
| 0x03   | 1    | Start cylinder (CHS)                                 |
| 0x04   | 1    | Partition type                                       |
| 0x05   | 1    | End head                                             |
| 0x06   | 1    | End sector (bits 0–5), high cylinder bits 6–7        |
| 0x07   | 1    | End cylinder                                         |
| 0x08   | 4    | LBA of partition start                               |
| 0x0C   | 4    | Number of sectors                                    |

---

## 2. FAT32 Volume Boot Record (VBR)

- **Size:** 512 bytes (first sector of a FAT32 partition)
- **Purpose:** Boot code + FAT32 volume information

| Offset   | Size | Description                                  |
|----------|------|----------------------------------------------|
| 0x000    | 3    | Jump instruction (e.g. EB 58 90)             |
| 0x003    | 8    | OEM Name                                     |
| 0x00B    | 2    | Bytes per sector (usually 512)               |
| 0x00D    | 1    | Sectors per cluster                          |
| 0x00E    | 2    | Reserved sectors (before FAT)                |
| 0x010    | 1    | Number of FATs                               |
| 0x011    | 2    | Max root dir entries (always 0 for FAT32)    |
| 0x013    | 2    | Total sectors (if < 65536, else 0)           |
| 0x015    | 1    | Media descriptor                             |
| 0x016    | 2    | Sectors per FAT (0 for FAT32)                |
| 0x018    | 2    | Sectors per track                            |
| 0x01A    | 2    | Number of heads                              |
| 0x01C    | 4    | Hidden sectors                               |
| 0x020    | 4    | Total sectors (if >= 65536)                  |
| 0x024    | 4    | Sectors per FAT (FAT32)                      |
| 0x028    | 2    | Flags                                        |
| 0x02A    | 2    | FAT32 version                                |
| 0x02C    | 4    | Root dir first cluster                       |
| 0x030    | 2    | FSInfo sector                                |
| 0x032    | 2    | Backup boot sector                           |
| 0x034    | 12   | Reserved                                     |
| 0x040    | 1    | Physical drive number                        |
| 0x041    | 1    | Reserved                                     |
| 0x042    | 1    | Extended boot signature (=0x29 if present)   |
| 0x043    | 4    | Volume serial number                         |
| 0x047    | 11   | Volume label                                 |
| 0x052    | 8    | File system type (e.g. "FAT32   ")           |
| 0x05A    | ...  | Boot code                                    |
| 0x1FE    | 2    | Signature 0x55AA                             |

---

## 3. INT 0x13 (BIOS Disk Services) – Documented Functions

| AH   | Function Name                     | Description                                                              |
|------|-----------------------------------|--------------------------------------------------------------------------|
| 00h  | Reset Disk System                 | Reset disk controller                                                    |
| 01h  | Get Status                        | Return error code from previous disk access                              |
| 02h  | Read Sectors (CHS)                | Read physical sectors (CHS)                                              |
| 03h  | Write Sectors (CHS)               | Write physical sectors (CHS)                                             |
| 04h  | Verify Sectors                    | Verify physical sectors                                                  |
| 05h  | Format Track                      | Format track (floppy only)                                               |
| 08h  | Get Drive Parameters              | Return drive info (CHS max, heads, sectors, etc.)                        |
| 09h  | Init Fixed Disk                   | Initialize hard disk (obsolete)                                          |
| 0Ah  | Read Long Sectors                 | Long read (with ECC, rarely used)                                        |
| 0Bh  | Write Long Sectors                | Long write                                                              |
| 10h  | Check Drive Ready                 | Check if disk is ready                                                   |
| 11h  | Recalibrate Drive                 | Recalibrate heads (floppy/old HDDs)                                      |
| 12h  | Controller Diagnostics            | Disk controller diagnostics                                              |
| 13h  | Controller Reset                  | Reset disk controller                                                    |
| 14h  | Controller Internal Diagnostic    | Internal controller diagnostics                                          |
| 15h  | Read Disk Type                    | Return inserted media type                                               |
| 16h  | Detect Disk Change                | Detect disk change (removable)                                           |
| 41h  | Check Extensions Present          | Check for LBA extension presence                                         |
| 42h  | Extended Read Sectors (LBA)       | Read sectors via LBA (extended mode, large partitions, etc.)             |
| 43h  | Extended Write Sectors (LBA)      | Write sectors via LBA                                                    |
| 44h  | Verify Sectors (LBA)              | Verify sectors via LBA                                                   |
| 45h  | Lock/Unlock Drive                 | Lock/unlock removable drive                                              |
| 46h  | Eject Media                       | Eject removable media                                                    |
| 47h  | Extended Seek                     | Seek to LBA sector                                                       |
| 48h  | Get Drive Parameters (LBA)        | Drive info (LBA mode, 64-bit capacity, etc.)                             |
| 49h  | Get Media Status                  | Get media status                                                         |
| 4Eh  | Get Device Path                   | Get device path (rare)                                                   |
| 4Fh  | Get Disk Access Mode              | Get disk access mode (rare)                                              |

---

**Notes:**
- For sector read/write: CHS = AH=02h/03h, LBA = AH=42h/43h.
- Error codes are returned in AH if CF=1.
- Functions not listed here are obsolete or non-standard.

---
