#!/usr/bin/env node

const dgram = require("dgram");
const fs = require("fs");

function usage() {
    console.log("Usage: uefi-udp-log-listen.js [--bind <ip>] [--port <port>] [--hex] [--raw <file>]");
    console.log("Defaults: --bind 0.0.0.0 --port 18194");
}

function parseArgs(argv) {
    const options = {
        bind: "0.0.0.0",
        port: 18194,
        hex: false,
        raw: null,
    };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === "--bind") {
            i++;
            if (i >= argv.length) {
                throw new Error("Missing value for --bind");
            }
            options.bind = argv[i];
        } else if (arg === "--port") {
            i++;
            if (i >= argv.length) {
                throw new Error("Missing value for --port");
            }
            const port = Number(argv[i]);
            if (!Number.isInteger(port) || port < 1 || port > 65535) {
                throw new Error(`Invalid port: ${argv[i]}`);
            }
            options.port = port;
        } else if (arg === "--hex") {
            options.hex = true;
        } else if (arg === "--raw") {
            i++;
            if (i >= argv.length) {
                throw new Error("Missing value for --raw");
            }
            options.raw = argv[i];
        } else if (arg === "--help" || arg === "-h") {
            usage();
            process.exit(0);
        } else {
            throw new Error(`Unknown option: ${arg}`);
        }
    }

    return options;
}

function sanitizeAscii(buffer) {
    let output = "";
    for (let i = 0; i < buffer.length; i++) {
        const b = buffer[i];
        if ((b >= 0x20 && b <= 0x7e) || b === 0x0a || b === 0x0d || b === 0x09) {
            output += String.fromCharCode(b);
        } else {
            output += ".";
        }
    }
    return output;
}

function toHex(buffer) {
    return Array.from(buffer).map((b) => b.toString(16).padStart(2, "0")).join(" ");
}

function nowText() {
    return new Date().toISOString();
}

let options;
try {
    options = parseArgs(process.argv.slice(2));
} catch (error) {
    console.error(error.message);
    usage();
    process.exit(1);
}

let rawStream = null;
if (options.raw !== null) {
    rawStream = fs.createWriteStream(options.raw, { flags: "a" });
}

const socket = dgram.createSocket("udp4");
socket.on("error", (error) => {
    console.error(`[${nowText()}] socket error: ${error.message}`);
});

socket.on("message", (message, remote) => {
    const prefix = `[${nowText()}] ${remote.address}:${remote.port} len=${message.length}`;
    if (options.hex) {
        console.log(`${prefix} hex=${toHex(message)}`);
    } else {
        process.stdout.write(`${prefix} ${sanitizeAscii(message)}`);
        if (message.length === 0 || (message[message.length - 1] !== 0x0a && message[message.length - 1] !== 0x0d)) {
            process.stdout.write("\n");
        }
    }

    if (rawStream !== null) {
        rawStream.write(message);
    }
});

socket.on("listening", () => {
    const address = socket.address();
    console.log(`[${nowText()}] listening on ${address.address}:${address.port}`);
});

socket.bind(options.port, options.bind);

process.on("SIGINT", () => {
    if (rawStream !== null) {
        rawStream.end();
    }
    socket.close(() => process.exit(0));
});

