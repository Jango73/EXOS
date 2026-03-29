#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const REPO_ROOT = path.resolve(__dirname, "..", "..");
const IGNORED_DIRECTORIES = new Set([
    ".git",
    "build",
    "log",
    "node_modules",
    "temp",
    ".cache",
    ".idea",
    ".vscode",
]);

function main() {
    const findings = [];
    walk(REPO_ROOT, findings);
    findings.sort((a, b) => {
        if (a.file === b.file) {
            return a.line - b.line;
        }
        return a.file.localeCompare(b.file);
    });

    for (const finding of findings) {
        console.log(`${finding.file}:${finding.name}`);
    }
}

function walk(directory, findings) {
    const entries = fs.readdirSync(directory, { withFileTypes: true });
    for (const entry of entries) {
        if (entry.isDirectory()) {
            if (IGNORED_DIRECTORIES.has(entry.name)) {
                continue;
            }
            walk(path.join(directory, entry.name), findings);
        } else if (entry.isFile() && path.extname(entry.name).toLowerCase() === ".c") {
            const filePath = path.join(directory, entry.name);
            const relativePath = path.relative(REPO_ROOT, filePath);
            const missingHeaders = analyzeFile(filePath);
            for (const missing of missingHeaders) {
                findings.push({
                    file: relativePath,
                    name: missing.name,
                    line: missing.line,
                });
            }
        }
    }
}

function analyzeFile(filePath) {
    const content = fs.readFileSync(filePath, "utf8");
    const missing = [];

    let line = 1;
    let state = "code";
    let blockType = null;
    let stringQuote = null;
    let braceDepth = 0;
    let parenDepth = 0;
    let declActive = false;
    let currentDeclHasDoxygen = false;
    let signatureBuffer = "";
    let signatureStartLine = 1;
    let lastContext = "none";
    let seenNonSpaceOnLine = false;

    function resetDeclaration() {
        declActive = false;
        currentDeclHasDoxygen = false;
        signatureBuffer = "";
        parenDepth = 0;
        signatureStartLine = line;
    }

    for (let index = 0; index < content.length; index++) {
        const char = content[index];
        const next = content[index + 1];

        if (state === "code") {
            if (char === "/" && next === "*") {
                const nextNext = content[index + 2];
                state = "block";
                blockType = nextNext === "*" ? "doxygen" : "comment";
                index += nextNext === "*" ? 2 : 1;
                seenNonSpaceOnLine = false;
                continue;
            }
            if (char === "/" && next === "/") {
                state = "line";
                blockType = "comment";
                index += 1;
                continue;
            }
            if (char === '"' || char === "'") {
                state = "string";
                stringQuote = char;
                continue;
            }
            if (char === "\r") {
                continue;
            }
            if (char === "\n") {
                line += 1;
                seenNonSpaceOnLine = false;
                if (declActive && braceDepth === 0) {
                    signatureBuffer += "\n";
                }
                continue;
            }
            if (!seenNonSpaceOnLine) {
                if (char === " " || char === "\t") {
                    continue;
                }
                if (char === "#") {
                    state = "line";
                    blockType = "preprocessor";
                    continue;
                }
                seenNonSpaceOnLine = true;
                if (braceDepth === 0 && !declActive) {
                    declActive = true;
                    currentDeclHasDoxygen = lastContext === "doxygen";
                    signatureBuffer = "";
                    signatureStartLine = line;
                    parenDepth = 0;
                }
            }

            if (braceDepth === 0 && declActive) {
                signatureBuffer += char;
                if (char === "(") {
                    parenDepth += 1;
                } else if (char === ")") {
                    if (parenDepth > 0) {
                        parenDepth -= 1;
                    }
                } else if (char === ";" && parenDepth === 0) {
                    resetDeclaration();
                    lastContext = "code";
                    continue;
                } else if (char === "{" && parenDepth === 0) {
                    const candidate = extractFunctionName(signatureBuffer.slice(0, -1));
                    if (candidate !== null && !currentDeclHasDoxygen) {
                        missing.push({ name: candidate, line: signatureStartLine });
                    }
                    lastContext = "code";
                    braceDepth += 1;
                    resetDeclaration();
                    continue;
                }
            }

            if (char === "{") {
                braceDepth += 1;
                lastContext = "code";
                continue;
            }
            if (char === "}") {
                if (braceDepth > 0) {
                    braceDepth -= 1;
                }
                lastContext = "code";
                continue;
            }
        } else if (state === "block") {
            if (char === "\n") {
                line += 1;
            }
            if (char === "*" && next === "/") {
                if (blockType === "doxygen") {
                    lastContext = "doxygen";
                } else if (blockType === "comment") {
                    lastContext = "code";
                }
                state = "code";
                blockType = null;
                index += 1;
                seenNonSpaceOnLine = false;
            }
            continue;
        } else if (state === "line") {
            if (char === "\n") {
                line += 1;
                if (blockType === "comment") {
                    lastContext = "code";
                }
                state = "code";
                blockType = null;
                seenNonSpaceOnLine = false;
            }
            continue;
        } else if (state === "string") {
            if (char === "\\" && next !== undefined) {
                index += 1;
                continue;
            }
            if (char === stringQuote) {
                state = "code";
                stringQuote = null;
                continue;
            }
            if (char === "\n") {
                line += 1;
                state = "code";
                stringQuote = null;
                seenNonSpaceOnLine = false;
            }
            continue;
        }
    }

    return missing;
}

function extractFunctionName(rawSignature) {
    if (!rawSignature) {
        return null;
    }
    const condensed = rawSignature.replace(/\s+/g, " ").trim();
    if (condensed.indexOf("(") === -1) {
        return null;
    }
    const cleaned = stripTrailingAttributes(condensed);
    const matches = [...cleaned.matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*\(/g)];
    if (matches.length === 0) {
        return null;
    }
    const candidate = matches[matches.length - 1][1];
    if (/^(?:if|for|while|switch|return|sizeof)$/.test(candidate)) {
        return null;
    }
    return candidate;
}

function stripTrailingAttributes(signature) {
    let result = signature.trimEnd();
    while (result.endsWith(")")) {
        let depth = 0;
        let position = result.length - 1;
        for (; position >= 0; position -= 1) {
            const current = result[position];
            if (current === ")") {
                depth += 1;
            } else if (current === "(") {
                depth -= 1;
                if (depth === 0) {
                    break;
                }
            }
        }
        if (depth !== 0 || position <= 0) {
            break;
        }
        let identifierEnd = position - 1;
        while (identifierEnd >= 0 && /\s/.test(result[identifierEnd])) {
            identifierEnd -= 1;
        }
        let identifierStart = identifierEnd;
        while (identifierStart >= 0 && /[A-Za-z0-9_]/.test(result[identifierStart])) {
            identifierStart -= 1;
        }
        if (identifierStart === identifierEnd) {
            break;
        }
        const identifier = result.slice(identifierStart + 1, identifierEnd + 1);
        const prefix = result.slice(0, identifierStart + 1);
        if (prefix.trim().length === 0) {
            break;
        }
        if (!/^(?:[A-Z0-9_]+|__\w+)$/.test(identifier)) {
            break;
        }
        result = prefix.trimEnd();
    }
    return result.trim();
}

main();
