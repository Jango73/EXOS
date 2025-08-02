// boot-qemu/calc_kernel_sectors.js
const fs = require('fs');
const path = require('path');

const kernelPath = path.join(__dirname, '../kernel/bin/exos.bin');
const outputPath = path.join(__dirname, 'kernel_sectors.inc');

if (!fs.existsSync(kernelPath)) {
    console.error(`ERROR: Kernel not found at ${kernelPath}`);
    process.exit(1);
}

const size = fs.statSync(kernelPath).size;
const sectors = Math.ceil(size / 512);

fs.writeFileSync(outputPath, `NUM_SECTORS equ ${sectors}\n`);
console.log(`Kernel size: ${sectors} sectors`);

