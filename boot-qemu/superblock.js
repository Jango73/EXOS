// boot-qemu/superblock.js

const fs = require('fs');
const path = require('path');

const kernelPath = path.join(__dirname, '../kernel/bin/exos.bin');
const imagePath  = path.join(__dirname, './bin/disk.img');

const kernelLBA     = 4;
// Kernel will be loaded at physical address 0x00012000
const loadAddress   = 0x00012000;
const entryOffset   = 0x0000;
const entrySegment  = loadAddress >> 4;

const superblockLBA = 2;
const superblockSize = 512;

// --- Check kernel exists ---
if (!fs.existsSync(kernelPath)) {
    console.error(`ERROR: Kernel not found at ${kernelPath}`);
    process.exit(1);
}

// --- Get kernel size in sectors ---
const kernelSize = fs.statSync(kernelPath).size;
const kernelSectors = Math.ceil(kernelSize / 512);

// --- Prepare superblock ---
const buf = Buffer.alloc(superblockSize);
buf.fill(0);

buf.write('EXOS', 0);                             // Magic
buf.writeUInt32LE(loadAddress,   0x04);           // Load address
buf.writeUInt32LE(kernelLBA,     0x08);           // Kernel LBA
buf.writeUInt32LE(entryOffset,   0x0C);           // Entry offset
buf.writeUInt32LE(entrySegment,  0x10);           // Entry segment
buf.writeUInt32LE(kernelSectors, 0x14);           // Sector count

// --- Helper to write buffer at a given offset ---
function writeBuffer(fd, buffer, offset) {
    fs.writeSync(fd, buffer, 0, buffer.length, offset);
}

// --- Write superblock and kernel to disk.img ---
const fd = fs.openSync(imagePath, 'r+');
writeBuffer(fd, buf, superblockLBA * 512);

const kernelData = fs.readFileSync(kernelPath);
writeBuffer(fd, kernelData, kernelLBA * 512);

const totalSectors = kernelLBA + kernelSectors;
fs.ftruncateSync(fd, totalSectors * 512);
fs.closeSync(fd);

console.log(`✔ SuperBlock written at LBA ${superblockLBA}`);
console.log(`  Kernel: ${kernelSectors} sectors at LBA ${kernelLBA}`);
console.log(
    `  Entry : ${entrySegment.toString(16)}:${entryOffset.toString(16).padStart(4, '0')}`
);
