# EXOS File System - XFS
### Version 1.0

---

## Notations used in this document

| Abbrev | Meaning                |
|--------|------------------------|
| U8     | unsigned byte          |
| I8     | signed byte            |
| U16    | unsigned word          |
| I16    | signed word            |
| U32    | unsigned long          |
| I32    | signed long            |

| Abbrev | Meaning                           |
|--------|-----------------------------------|
| EXOS   | Extensible Operating System       |
| BIOS   | Basic Input/Output System         |
| CHS    | Cylinder-Head-Sector              |
| MBR    | Master Boot Record                |
| OS     | Operating System                  |

---

## Structure of the Master Boot Record

| Offset   | Type | Description                                  |
|----------|------|----------------------------------------------|
| 0..445   | U8x? | The boot sequence                            |
| 446..461 | ?    | CHS location of partition No 1               |
| 462..477 | ?    | CHS location of partition No 2               |
| 478..493 | ?    | CHS location of partition No 3               |
| 494..509 | ?    | CHS location of partition No 4               |
| 510      | U16  | BIOS signature : 0x55AA (`_*_*_*_**_*_*_*_`) |

---

## Structure of SuperBlock

The SuperBlock is always **1024 bytes** in size.

| Offset | Type   | Description                                   |
|--------|--------|-----------------------------------------------|
| 0      | U32    | Magic number, must be `"EXOS"`                |
| 4      | U32    | Version (high word = major, low word = minor) |
| 8      | U32    | Size of a cluster in bytes                    |
| 12     | U32    | Number of clusters                            |
| 16     | U32    | Number of free clusters                       |
| 20     | U32    | Cluster index of cluster bitmap               |
| 24     | U32    | Cluster index of bad cluster page             |
| 28     | U32    | Cluster index of root FileRecord ("/")        |
| 32     | U32    | Cluster index of security info                |
| 36     | U32    | Index in root for OS kernel main file         |
| 40     | U32    | Number of folders (excluding "." and "..")    |
| 44     | U32    | Number of files                               |
| 48     | U32    | Max mount count before check is forced        |
| 52     | U32    | Current mount count                           |
| 56     | U32    | Format of the volume name                     |
| 60–63  | U8x4   | Reserved                                      |
| 64     | U8x32  | Password (optional)                           |
| 96     | U8x32  | Name of this file system's creator            |
| 128    | U8x128 | Name of the volume                            |

---

## Structure of FileRecord

| Offset | Type   | Description                        |
|--------|--------|------------------------------------|
| 0      | U32    | SizeLow                            |
| 4      | U32    | SizeHigh                           |
| 8      | U64    | Creation time                      |
| 16     | U64    | Last access time                   |
| 24     | U64    | Last modification time             |
| 32     | U32    | Cluster index for ClusterTable     |
| 36     | U32    | Standard attributes                |
| 40     | U32    | Security attributes                |
| 44     | U32    | Group owner of this file           |
| 48     | U32    | User owner of this file            |
| 52     | U32    | Format of name                     |
| 56–127 | U8x?   | Reserved, should be zero           |
| 128    | U8x128 | Name of the file (NULL terminated) |

---

## FileRecord fields

**Time fields (bit layout):**

- Bits 0..21  : Year (max: 4,194,303)  
- Bits 22..25 : Month in the year (max: 15)  
- Bits 26..31 : Day in the month (max: 63)  
- Bits 32..37 : Hour in the day (max: 63)  
- Bits 38..43 : Minute in the hour (max: 63)  
- Bits 44..49 : Second in the minute (max: 63)  
- Bits 50..59 : Millisecond in the second (max: 1023)  

**Standard attributes field:**

- Bit 0 : 1 = folder, 0 = file  
- Bit 1 : 1 = read-only, 0 = read/write  
- Bit 2 : 1 = system  
- Bit 3 : 1 = archive  
- Bit 4 : 1 = hidden  

**Security attributes field:**

- Bit 0 : 1 = only kernel has access to the file  
- Bit 1 : 1 = fill the file's clusters with zeroes on delete  

**Name format:**

- 0 : ASCII (8 bits per character)  
- 1 : Unicode (16 bits per character)  

---

## Structure of folders and files

- A cluster that contains 32-bit indices to other clusters is called a **page**.  
- FileRecord contains a cluster index for its first page.  
- A page is filled with cluster indices pointing to file/folder data.  
- For folders: data = series of FileRecords.  
- For files: data = arbitrary user data.  
- The **last entry** of a page is `0xFFFFFFFF`.  
- If more than one page is needed, the last index points to the **next page**.  

---

## Clusters

- All cluster pointers are 32-bit.  
- Cluster 0 = boot sector (1024 bytes).  
- Cluster 1 = SuperBlock (1024 bytes).  
- First usable cluster starts at byte 2048.  

**Max addressable bytes by cluster size:**

| Cluster size | Max addressable bytes  |
|--------------|-------------------------|
| 1024         | 4,398,046,510,080       |
| 2048         | 8,796,093,020,160       |
| 4096         | 17,592,186,040,320      |
| 8192         | 35,184,372,080,640      |

**Number of clusters formula:**  

```
(Disc size in bytes - 2048) / Cluster size
```

Fractional part = unusable space.  

**Examples:**

| Disc size               | Cluster size | Total clusters |
|-------------------------|--------------|----------------|
| 536,870,912 (500 MB)    | 1,024 (1 KB) | 524,286        |
| 536,870,912 (500 MB)    | 2,048 (2 KB) | 262,143        |
| 536,870,912 (500 MB)    | 4,096 (4 KB) | 131,071        |
| 536,870,912 (500 MB)    | 8,192 (8 KB) | 65,535         |
| 4,294,967,296 (4 GB)    | 1,024 (1 KB) | 4,194,302      |
| 4,294,967,296 (4 GB)    | 2,048 (2 KB) | 2,097,151      |
| 4,294,967,296 (4 GB)    | 4,096 (4 KB) | 1,048,575      |
| 4,294,967,296 (4 GB)    | 8,192 (8 KB) | 524,287        |

---

## Cluster bitmap

- A bit array showing free/used clusters.  
- `0 = free`, `1 = used`.  
- Size of bitmap =  

```
(Total disc size / Cluster size) / 8
```

**Examples:**

| Disc size              | Cluster size | Bitmap size | Num. clusters |
|------------------------|--------------|-------------|---------------|
| 536,870,912 (500 MB)   | 1,024 (1 KB) | 65,536      | 64            |
| 536,870,912 (500 MB)   | 2,048 (2 KB) | 32,768      | 16            |
| 536,870,912 (500 MB)   | 4,096 (4 KB) | 16,384      | 4             |
| 536,870,912 (500 MB)   | 8,192 (8 KB) | 8,192       | 1             |
| 4,294,967,296 (4 GB)   | 1,024 (1 KB) | 524,288     | 512           |
| 4,294,967,296 (4 GB)   | 2,048 (2 KB) | 262,144     | 128           |
| 4,294,967,296 (4 GB)   | 4,096 (4 KB) | 131,072     | 32            |
| 4,294,967,296 (4 GB)   | 8,192 (8 KB) | 65,536      | 8             |
| 17,179,869,184 (16 GB) | 1,024 (1 KB) | 4,194,304   | 8,192         |
| 17,179,869,184 (16 GB) | 2,048 (2 KB) | 1,048,576   | 512           |
| 17,179,869,184 (16 GB) | 4,096 (4 KB) | 524,288     | 128           |
| 17,179,869,184 (16 GB) | 8,192 (4 KB) | 262,144     | 32            |
