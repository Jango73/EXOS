// check_exos_magic.js
const fs = require('fs');
const path = process.argv[2];

if (!path) {
	console.error("Usage: node check_exos_magic.js <binary_file>");
	process.exit(2);
}

const buf = fs.readFileSync(path);
if (buf.length < 8) {
	console.error(`ERROR: file too small (< 8 bytes): ${path}`);
	process.exit(2);
}

function u32le(buf, ofs) {
	return buf[ofs] | (buf[ofs+1]<<8) | (buf[ofs+2]<<16) | (buf[ofs+3]<<24);
}

const MAGIC = 0x534F5845; // 'EXOS' little-endian

const magic_2nd  = u32le(buf, 4);
const magic_last = u32le(buf, buf.length-4);

console.log(`[info] 2nd u32:  0x${magic_2nd.toString(16).toUpperCase().padStart(8,"0")}`);
console.log(`[info] last u32: 0x${magic_last.toString(16).toUpperCase().padStart(8,"0")}`);

if (magic_2nd === MAGIC && magic_last === MAGIC) {
	console.log("EXOS magic signature present at both positions.");
	process.exit(0);
} else {
	console.error("ERROR: EXOS magic signature NOT found at both positions!");
	process.exit(1);
}
