#!/usr/bin/env node
"use strict";

const Fs = require("fs");
const Crypto = require("crypto");

function fail(message) {
    console.error(`verify-epk: ${message}`);
    process.exit(1);
}

function parseArgs(argv) {
    const options = {
        file: null,
        expectSignatureSize: null,
        expectSignatureFlag: null,
        expectMagic: 0x314b5045,
        expectVersion: ((1 << 16) | 0),
        expectHeaderSize: 128,
        expectMinTocEntries: null,
        expectCompressionMethod: null,
    };

    for (let i = 0; i < argv.length; i++) {
        const argument = argv[i];
        if (argument === "--file") {
            options.file = argv[++i];
        } else if (argument === "--expect-signature-size") {
            options.expectSignatureSize = Number(argv[++i]);
        } else if (argument === "--expect-signature-flag") {
            const value = argv[++i];
            if (!["0", "1"].includes(value)) {
                fail("--expect-signature-flag must be 0 or 1");
            }
            options.expectSignatureFlag = Number(value);
        } else if (argument === "--expect-min-toc-entries") {
            options.expectMinTocEntries = Number(argv[++i]);
        } else if (argument === "--expect-compression-method") {
            const value = argv[++i];
            if (!["none", "zlib", "mixed"].includes(value)) {
                fail("--expect-compression-method must be none, zlib, or mixed");
            }
            options.expectCompressionMethod = value;
        } else {
            fail(`unknown argument '${argument}'`);
        }
    }

    if (!options.file) {
        fail("--file is required");
    }

    return options;
}

function u32(buffer, offset) {
    return buffer.readUInt32LE(offset);
}

function u64(buffer, offset) {
    return Number(buffer.readBigUInt64LE(offset));
}

function checkRange(name, offset, size, total) {
    if (offset < 0 || size < 0 || offset > total || size > (total - offset)) {
        fail(`${name} range out of file bounds`);
    }
}

function readBlockMethods(buffer, blockTableOffset, blockTableSize) {
    const entrySize = 52;
    if (blockTableSize % entrySize !== 0) {
        fail("block table size is not a multiple of 52");
    }
    const methods = [];
    const count = blockTableSize / entrySize;
    for (let i = 0; i < count; i++) {
        const offset = blockTableOffset + i * entrySize;
        methods.push(buffer.readUInt8(offset + 16));
    }
    return methods;
}

function computePackageHash(buffer, signatureOffset, signatureSize) {
    const clone = Buffer.from(buffer);
    clone.fill(0, 80, 112);

    const hasher = Crypto.createHash("sha256");

    if (signatureSize > 0) {
        hasher.update(clone.subarray(0, signatureOffset));
        hasher.update(clone.subarray(signatureOffset + signatureSize));
    } else {
        hasher.update(clone);
    }

    return hasher.digest();
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    const bytes = Fs.readFileSync(options.file);

    if (bytes.length < 128) {
        fail("file too small");
    }

    const magic = u32(bytes, 0);
    const version = u32(bytes, 4);
    const flags = u32(bytes, 8);
    const headerSize = u32(bytes, 12);

    const tocOffset = u64(bytes, 16);
    const tocSize = u64(bytes, 24);
    const blockTableOffset = u64(bytes, 32);
    const blockTableSize = u64(bytes, 40);
    const manifestOffset = u64(bytes, 48);
    const manifestSize = u64(bytes, 56);
    const signatureOffset = u64(bytes, 64);
    const signatureSize = u64(bytes, 72);

    if (magic !== options.expectMagic) {
        fail(`invalid magic: got 0x${magic.toString(16)}`);
    }
    if (version !== options.expectVersion) {
        fail(`invalid version: got 0x${version.toString(16)}`);
    }
    if (headerSize !== options.expectHeaderSize) {
        fail(`invalid header size: ${headerSize}`);
    }

    checkRange("TOC", tocOffset, tocSize, bytes.length);
    checkRange("BlockTable", blockTableOffset, blockTableSize, bytes.length);
    checkRange("Manifest", manifestOffset, manifestSize, bytes.length);
    if (signatureSize > 0) {
        checkRange("Signature", signatureOffset, signatureSize, bytes.length);
    }

    const signatureFlag = (flags & 0x2) !== 0 ? 1 : 0;
    if (options.expectSignatureFlag !== null && signatureFlag !== options.expectSignatureFlag) {
        fail(`signature flag mismatch: got ${signatureFlag}`);
    }

    if (options.expectSignatureSize !== null && signatureSize !== options.expectSignatureSize) {
        fail(`signature size mismatch: got ${signatureSize}`);
    }

    const tocEntryCount = u32(bytes, tocOffset);
    if (options.expectMinTocEntries !== null && tocEntryCount < options.expectMinTocEntries) {
        fail(`toc entry count ${tocEntryCount} is smaller than ${options.expectMinTocEntries}`);
    }

    if (options.expectCompressionMethod !== null) {
        const methods = readBlockMethods(bytes, blockTableOffset, blockTableSize);

        if (options.expectCompressionMethod === "none") {
            if (!methods.every((value) => value === 0)) {
                fail("expected all block methods to be 'none'");
            }
        } else if (options.expectCompressionMethod === "zlib") {
            if (!methods.every((value) => value === 1)) {
                fail("expected all block methods to be 'zlib'");
            }
        } else {
            const hasNone = methods.some((value) => value === 0);
            const hasZlib = methods.some((value) => value === 1);
            if (!(hasNone && hasZlib)) {
                fail("expected mixed block methods");
            }
        }
    }

    const headerHash = bytes.subarray(80, 112);
    const computedHash = computePackageHash(bytes, signatureOffset, signatureSize);
    if (!headerHash.equals(computedHash)) {
        fail("package hash mismatch");
    }

    console.log(`verify-epk: ok (${options.file})`);
}

main();
