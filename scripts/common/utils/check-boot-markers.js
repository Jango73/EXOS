#!/usr/bin/env node

const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..", "..");

function readText(relativePath) {
    return fs.readFileSync(path.join(ROOT, relativePath), "utf8");
}

function walkFiles(relativeDir, predicate, out) {
    const absDir = path.join(ROOT, relativeDir);
    const entries = fs.readdirSync(absDir, { withFileTypes: true });

    for (const entry of entries) {
        const absPath = path.join(absDir, entry.name);
        const relPath = path.relative(ROOT, absPath);

        if (entry.isDirectory()) {
            walkFiles(relPath, predicate, out);
            continue;
        }

        if (predicate(relPath)) {
            out.push(relPath);
        }
    }
}

function fail(message) {
    process.stderr.write(`${message}\n`);
    process.exit(1);
}

function parseIntegerLiteral(token) {
    let value = token.trim();
    value = value.replace(/[uUlL]+$/g, "");

    if (/^0x[0-9a-fA-F]+$/.test(value)) {
        return Number.parseInt(value, 16);
    }
    if (/^[0-9]+$/.test(value)) {
        return Number.parseInt(value, 10);
    }

    return null;
}

function parseDefine(text, name) {
    const re = new RegExp(`^\\s*#define\\s+${name}\\s+([^\\s/]+)`, "m");
    const match = text.match(re);
    if (match == null) {
        return null;
    }

    return parseIntegerLiteral(match[1]);
}

function parseBootUefiEnumValues(text) {
    const values = new Map();
    const enumBlockMatch = text.match(/enum\s*\{([\s\S]*?)\}\s*;/m);

    if (enumBlockMatch == null) {
        fail("[check-boot-markers] BOOT_UEFI_STAGE enum not found");
    }

    const body = enumBlockMatch[1];
    const lineRe = /^\s*(BOOT_UEFI_STAGE_[A-Z0-9_]+)\s*=\s*([^,\n]+)\s*,?/gm;
    let line = null;

    while ((line = lineRe.exec(body)) !== null) {
        const name = line[1];
        const value = parseIntegerLiteral(line[2]);
        if (value == null) {
            fail(`[check-boot-markers] Could not parse enum value for ${name}: ${line[2].trim()}`);
        }
        values.set(name, value);
    }

    return values;
}

function collectBootStageMarkers(text, relativePath) {
    const markers = [];
    const callRe = /BootStageMarkerFromConsole\s*\(\s*([^,\n]+)\s*,/g;
    let call = null;

    while ((call = callRe.exec(text)) !== null) {
        const expr = call[1].trim();
        const value = parseIntegerLiteral(expr);
        if (value == null) {
            continue;
        }

        markers.push({
            value,
            location: `${relativePath}:${offsetToLine(text, call.index)}`,
            kind: "kernel-fixed",
        });
    }

    return markers;
}

function collectBootUefiMarkers(text, relativePath, enumValues) {
    const markers = [];
    const callRe = /BootUefiMarkStage\s*\(\s*[^,]+,\s*(BOOT_UEFI_STAGE_[A-Z0-9_]+)\s*,/g;
    let call = null;

    while ((call = callRe.exec(text)) !== null) {
        const symbol = call[1];
        if (!enumValues.has(symbol)) {
            fail(`[check-boot-markers] Unknown UEFI marker symbol ${symbol}`);
        }

        markers.push({
            value: enumValues.get(symbol),
            location: `${relativePath}:${offsetToLine(text, call.index)}`,
            kind: "uefi-fixed",
            symbol,
        });
    }

    return markers;
}

function offsetToLine(text, offset) {
    let line = 1;
    for (let i = 0; i < offset; i++) {
        if (text.charCodeAt(i) === 10) {
            line++;
        }
    }
    return line;
}

function addCollision(collisions, value, marker) {
    if (!collisions.has(value)) {
        collisions.set(value, []);
    }
    collisions.get(value).push(marker);
}

function assertUnique(markers) {
    const collisions = new Map();
    const seen = new Map();

    for (const marker of markers) {
        if (!seen.has(marker.value)) {
            seen.set(marker.value, marker);
            continue;
        }

        addCollision(collisions, marker.value, seen.get(marker.value));
        addCollision(collisions, marker.value, marker);
    }

    if (collisions.size === 0) {
        return;
    }

    process.stderr.write("[check-boot-markers] Duplicate marker indexes found:\n");
    for (const [value, entries] of collisions.entries()) {
        process.stderr.write(`  - ${value}:\n`);
        for (const entry of entries) {
            process.stderr.write(`      ${entry.kind} at ${entry.location}\n`);
        }
    }
    process.exit(1);
}

function assertKernelDriverRanges(kernelFixedMarkers, kernelSourceText) {
    const preBase = parseDefine(kernelSourceText, "BOOT_STAGE_DRIVER_PRE_BASE");
    const memoryBase = parseDefine(kernelSourceText, "BOOT_STAGE_MEMORY_MANAGER_BASE");
    const memoryCount = parseDefine(kernelSourceText, "BOOT_STAGE_MEMORY_MANAGER_COUNT");

    if (preBase == null || memoryBase == null || memoryCount == null) {
        if (kernelFixedMarkers.length !== 0) {
            fail("[check-boot-markers] Missing kernel marker range defines while kernel markers still exist");
        }

        return null;
    }

    const memoryEnd = memoryBase + memoryCount - 1;
    const memoryDriverAfter = memoryBase + memoryCount;
    const postDriverStart = memoryBase + memoryCount + 1;

    if (preBase + 4 >= memoryBase) {
        fail("[check-boot-markers] Driver pre-range overlaps memory-manager marker range");
    }

    const maxKernelFixed = kernelFixedMarkers.reduce((max, marker) => Math.max(max, marker.value), 0);
    if (maxKernelFixed >= postDriverStart) {
        fail(
            `[check-boot-markers] Kernel fixed marker ${maxKernelFixed} overlaps dynamic driver markers (start=${postDriverStart})`
        );
    }

    if (kernelFixedMarkers.some((marker) => marker.value === memoryDriverAfter)) {
        fail(
            `[check-boot-markers] Kernel fixed marker uses reserved memory-driver post-load index (${memoryDriverAfter})`
        );
    }

    return {
        preRange: `${preBase}-${preBase + 4}`,
        memoryRange: `${memoryBase}-${memoryEnd}`,
        memoryDriverAfter,
        postDriverStart,
    };
}

function main() {
    const kernelFiles = [];
    walkFiles(
        "kernel/source",
        (relativePath) => relativePath.endsWith(".c") || relativePath.endsWith(".h") || relativePath.endsWith(".S"),
        kernelFiles
    );

    const bootUefiFile = "boot-uefi/source/boot-uefi.c";
    const kernelFileContents = new Map();
    for (const relativePath of kernelFiles) {
        kernelFileContents.set(relativePath, readText(relativePath));
    }

    const bootUefiText = readText(bootUefiFile);
    const uefiEnumValues = parseBootUefiEnumValues(bootUefiText);

    const kernelFixedMarkers = [];
    for (const [relativePath, text] of kernelFileContents.entries()) {
        kernelFixedMarkers.push(...collectBootStageMarkers(text, relativePath));
    }

    const uefiFixedMarkers = collectBootUefiMarkers(bootUefiText, bootUefiFile, uefiEnumValues);
    const allFixedMarkers = kernelFixedMarkers.concat(uefiFixedMarkers);

    assertUnique(allFixedMarkers);
    const ranges = assertKernelDriverRanges(kernelFixedMarkers, readText("kernel/source/Kernel.c"));

    process.stdout.write("[check-boot-markers] OK\n");
    process.stdout.write(`- Kernel fixed markers: ${kernelFixedMarkers.length}\n`);
    process.stdout.write(`- UEFI fixed markers: ${uefiFixedMarkers.length}\n`);
    if (ranges == null) {
        process.stdout.write("- Kernel marker ranges: disabled (no kernel markers)\n");
    } else {
        process.stdout.write(`- Driver pre-range: ${ranges.preRange}\n`);
        process.stdout.write(`- Memory-manager range: ${ranges.memoryRange}\n`);
        process.stdout.write(`- Memory driver post-load marker: ${ranges.memoryDriverAfter}\n`);
        process.stdout.write(`- Dynamic driver markers start at: ${ranges.postDriverStart}\n`);
    }
}

main();
