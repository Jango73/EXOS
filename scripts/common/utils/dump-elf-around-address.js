const fs = require('fs');

if (process.argv.length < 4) {
    console.error(`Usage: node ${process.argv[1]} <file.elf> <hex_address> [size=64] [mode=around|from]`);
    process.exit(1);
}

const filePath = process.argv[2];
const addr = parseInt(process.argv[3], 16);
const size = process.argv[4] ? parseInt(process.argv[4], 10) : 64;
const mode = process.argv[5] || 'around';

const fd = fs.openSync(filePath, 'r');
const buf = Buffer.alloc(64 * 1024); // read header blocks

// --- ELF header parsing ---
fs.readSync(fd, buf, 0, 64, 0);

// Validate ELF magic
if (buf.readUInt32BE(0) !== 0x7f454c46) { // 0x7F 'E' 'L' 'F'
    console.error("Not an ELF file");
    process.exit(2);
}

const is64 = buf[4] === 2; // EI_CLASS: 1=32bit, 2=64bit
const littleEndian = buf[5] === 1;

function readUInt(buf, offset, size) {
    if (size === 4) return littleEndian ? buf.readUInt32LE(offset) : buf.readUInt32BE(offset);
    if (size === 8) return Number(littleEndian ? buf.readBigUInt64LE(offset) : buf.readBigUInt64BE(offset));
    throw new Error("Unsupported size");
}

// Program header offset and count
let e_phoff = Number(littleEndian ? buf.readBigUInt64LE(32) : buf.readBigUInt64BE(32)); // for 64-bit
let e_phentsize = littleEndian ? buf.readUInt16LE(54) : buf.readUInt16BE(54);
let e_phnum = littleEndian ? buf.readUInt16LE(56) : buf.readUInt16BE(56);

// For 32-bit ELF, different offsets
if (!is64) {
    const e_phoff_32 = littleEndian ? buf.readUInt32LE(28) : buf.readUInt32BE(28);
    const e_phentsize_32 = littleEndian ? buf.readUInt16LE(42) : buf.readUInt16BE(42);
    const e_phnum_32 = littleEndian ? buf.readUInt16LE(44) : buf.readUInt16BE(44);
    e_phoff = e_phoff_32;
    e_phentsize = e_phentsize_32;
    e_phnum = e_phnum_32;
}

let foundSegment = null;

const phdrBuf = Buffer.alloc(e_phentsize);

for (let i = 0; i < e_phnum; i++) {
    fs.readSync(fd, phdrBuf, 0, e_phentsize, e_phoff + i * e_phentsize);

    // parse program header fields for LOAD segment
    const p_type = littleEndian ? phdrBuf.readUInt32LE(0) : phdrBuf.readUInt32BE(0);
    if (p_type !== 1) continue; // PT_LOAD

    const p_offset = littleEndian ? phdrBuf.readUInt32LE(4) : phdrBuf.readUInt32BE(4);
    const p_vaddr = littleEndian ? phdrBuf.readUInt32LE(8) : phdrBuf.readUInt32BE(8);
    const p_filesz = littleEndian ? phdrBuf.readUInt32LE(16) : phdrBuf.readUInt32BE(16);
    const p_memsz = littleEndian ? phdrBuf.readUInt32LE(20) : phdrBuf.readUInt32BE(20);

    if (addr >= p_vaddr && addr < p_vaddr + p_memsz) {
        foundSegment = {p_offset, p_vaddr, p_filesz, p_memsz};
        break;
    }
}

if (!foundSegment) {
    console.error(`Address 0x${addr.toString(16)} not found in any LOAD segment`);
    process.exit(3);
}

const fileOffset = foundSegment.p_offset + (addr - foundSegment.p_vaddr);

let start;
if (mode === 'from') {
    start = fileOffset;
} else {
    // default 'around'
    start = Math.max(fileOffset - Math.floor(size / 2), 0);
}

const readSize = size;

const dataBuf = Buffer.alloc(readSize);
fs.readSync(fd, dataBuf, 0, readSize, start);

function hexDump(buffer, offset) {
    for (let i = 0; i < buffer.length; i += 16) {
        const slice = buffer.slice(i, i + 16);
        const addrStr = (offset + i).toString(16).padStart(8, '0');
        const hexBytes = Array.from(slice).map(b => b.toString(16).padStart(2, '0')).join(' ');
        const ascii = Array.from(slice).map(b => (b >= 32 && b <= 126) ? String.fromCharCode(b) : '.').join('');
        console.log(`${addrStr}  ${hexBytes.padEnd(47, ' ')}  ${ascii}`);
    }
}

hexDump(dataBuf, start);

fs.closeSync(fd);
