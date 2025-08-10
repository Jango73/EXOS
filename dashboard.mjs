import fs from 'fs';
import path from 'path';
import { spawn, exec } from 'child_process';
import blessed from 'blessed';
import { Tail } from 'tail';
import kill from 'kill-port';

// Added: spawn fallback when script file is missing in scriptsDir
function spawnWithScriptsDirFallback(scriptsDir, scriptFile, spawnImpl) {
    const candidate = require('path').join(scriptsDir ?? './scripts', scriptFile);
    const exists = require('fs').existsSync(candidate);
    if (exists) {
        return { cmd: candidate, usedFallback: false };
    }
    // Fallback: run the provided value as-is via shell
    return { cmd: scriptFile, usedFallback: true };
}
/**
 * dashboard.json format:
 * {
 *   "logs": [ "<path>", ... ],
 *   "scriptsDir": "<relative path to scripts>",
 *   "keyBindings": { "<key>": "<script file>", ... },
 *   "settings": { ... },
 *   "events": {
 *     "onDashboardStart": [
 *       { "action": "killProcess" | "closeTCPPorts" | "closeUDPPorts", "parameters": [ ... ] },
 *       ...
 *     ],
 *     "beforeStartProcess": [
 *       {
 *         "script": "<script file name, including extension>",
 *         "actions": [
 *           { "action": "killProcess" | "closeTCPPorts" | "closeUDPPorts", "parameters": [ ... ] },
 *           ...
 *         ]
 *       },
 *       ...
 *     ]
 *   }
 * }
 *
 * Notes:
 * - Script matching uses the full file name (with extension).
 * - Actions available in both places: killProcess, closeTCPPorts, closeUDPPorts.
 * - For backward compatibility, legacy top-level keys `onDashboardStart` and
 *   `beforeStartProcess` (object map) are accepted and normalized into `events`.
 */

/*
setTimeout(() => {
    output.log('[dash] Timeout reached, exiting.');
    screen.render();
    process.exit(1);
}, 60000);
*/

const defaultSettings = {
    enableCommandHistory: true,
    persistLogs: true,
    notifyOnExit: true
};

let logStream = null;

// interface LastCommand { kind: 'script' | 'custom'; value: string; }
let lastCommand = null;

function initLogFile() {
    if (config.settings?.persistLogs !== true || logStream) return;
    const logDir = path.resolve(process.cwd(), 'logs');
    if (!fs.existsSync(logDir)) fs.mkdirSync(logDir, { recursive: true });
    const timestamp = new Date().toISOString().replace(/[:]/g, '-');
    const filePath = path.join(logDir, `dash-${timestamp}.log`);
    logStream = fs.createWriteStream(filePath, { flags: 'a' });
}

function stopLogFile() {
    if (!logStream) return;
    logStream.end();
    logStream = null;
}

function loadConfig() {
    try {
        const cfgPath = path.join(process.cwd(), 'dashboard.json');
        const raw = fs.readFileSync(cfgPath, 'utf-8');
        const cfg = JSON.parse(raw);
        // Normalize legacy keys into events
        const legacyODS = cfg.onDashboardStart || [];
        const legacyBSP = cfg.beforeStartProcess || {};
        const inEvents = cfg.events || {};
        // Convert legacy object map { "scriptName": [actions] } -> array of { script, actions }
        const normalizeMapToArray = (m) => {
            if (!m || Array.isArray(m)) return Array.isArray(m) ? m : [];
            if (typeof m !== 'object') return [];
            return Object.entries(m).map(([script, actions]) => ({
                script,
                actions: Array.isArray(actions) ? actions : []
            }));
        };
        // Determine final events
        const events = {
            onDashboardStart: Array.isArray(inEvents.onDashboardStart) ? inEvents.onDashboardStart
                                : Array.isArray(legacyODS) ? legacyODS : [],
            beforeStartProcess: Array.isArray(inEvents.beforeStartProcess)
                                ? inEvents.beforeStartProcess
                                : normalizeMapToArray(legacyBSP)
        };
        return {
            ...cfg,
            events,
            settings: {
                ...defaultSettings,
                ...cfg.settings
            }
        };
    } catch {
        return { settings: { ...defaultSettings }, events: { onDashboardStart: [], beforeStartProcess: [] } };
    }
}


function escapeRegex(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function parseLogDate(s) {
    const iso = s.replace(/T(\d{2})-(\d{2})-(\d{2})/, 'T$1:$2:$3');
    return new Date(iso).getTime();
}

function resolveLatest(p) {
    if (!p.includes('{{latest}}')) return p;

    const [before, after] = p.split('{{latest}}');
    const dir = path.dirname(before);
    const prefix = path.basename(before);
    const directory = path.resolve(dir || '.');

    try {
        const regex = new RegExp(`^${escapeRegex(prefix)}(\\d{4}-\\d{2}-\\d{2}(?:T\\d{2}-\\d{2}-\\d{2})?)${escapeRegex(after)}$`);
        const entries = fs.readdirSync(directory)
            .map(name => {
                const match = name.match(regex);
                if (!match) return null;
                return { dateStr: match[1], timestamp: parseLogDate(match[1]) };
            })
            .filter((v) => v !== null)
            .sort((a, b) => b.timestamp - a.timestamp);

        if (entries.length === 0) return path.join(dir, `${prefix}latest${after}`);

        return path.join(dir, `${prefix}${entries[0].dateStr}${after}`);
    } catch {
        return path.join(dir, `${prefix}latest${after}`);
    }
}

// Load package.json scripts from current working directory
// CHANGED: now load from config.keyBindings instead of package.json
function loadScripts() {
    try {
        if (config.keyBindings) {
            return Object.entries(config.keyBindings).map(([key, file]) => `${key.toUpperCase()} - ${file}`);
        }
        return [];
    } catch {
        return [];
    }
}

function setOutputLabel(label) {
    output.setLabel(` ${label} `);
    screen.render();
}

// interface LogWatcher { configPath: string; currentPath: string; box: blessed.Widgets.Log; tail: Tail; }
function startTail(file, box) {
    if (!fs.existsSync(file)) fs.writeFileSync(file, '');
    const tail = new Tail(file, {
        follow: true,
        useWatchFile: true,
        fsWatchOptions: { interval: 500 }
    });
    tail.on('line', (line) => {
        box.log(line);
        screen.render();
    });
    tail.on('error', (err) => {
        box.log(`Tail error: ${err.message}`);
        screen.render();
    });
    return tail;
}

const config = loadConfig();
const scripts = loadScripts();

// Initialize blessed screen
const screen = blessed.screen({
    smartCSR: true,
    title: 'Script Runner'
});

const sidebar = blessed.box({
    top: 0,
    left: 0,
    width: '30%',
    height: '100%',
    label: 'Scripts',
    border: 'line'
});
const rightContainer = blessed.box({
    top: 0,
    left: '30%',
    width: '70%',
    height: '100%',
});

const logsContainer = blessed.box({
    parent: rightContainer,
    top: 0,
    height: '60%',
    width: '100%',
    label: 'Logs',
    border: 'line'
});

const bottomContainer = blessed.box({
    parent: rightContainer,
    bottom: 0,
    height: '40%',
    width: '100%'
});

const buttonBar = blessed.box({
    parent: bottomContainer,
    top: 0,
    height: 3,
    width: '100%'
});

const stopButton = blessed.button({
    parent: buttonBar,
    left: 1,
    mouse: true,
    keys: true,
    shrink: true,
    padding: { left: 1, right: 1 },
    content: 'Stop',
    border: 'line',
    style: {
        focus: {
            bg: 'cyan',
            fg: 'white',
            bold: true
        }
    }
});

const inputBox = blessed.textbox({
    parent: buttonBar,
    left: 15,
    width: '70%',
    height: 3,
    inputOnFocus: true,
    border: 'line',
    label: ' Custom Command ',
    style: {
        focus: {
            border: { fg: 'cyan' }
        }
    }
});

const output = blessed.log({
    parent: bottomContainer,
    top: 3,
    height: '100%-3',
    width: '100%',
    label: 'Process output',
    border: 'line',
    scrollable: true,
    alwaysScroll: true,
    scrollbar: {
        ch: ' ',
        track: { bg: 'grey' },
        style: { inverse: true }
    },
    style: {
        border: { fg: 'grey' },
        focus: {
            border: { fg: 'cyan' }
        }
    }
});

const originalOutputLog = output.log.bind(output);
output.log = ((msg) => {
    originalOutputLog(msg);
    if (logStream) {
        logStream.write(`[${new Date().toISOString()}] ${msg}\n`);
    }
});

initLogFile();

const list = blessed.list({
    parent: sidebar,
    width: '100%-2',
    height: '100%-2',
    top: 1,
    left: 1,
    keys: true,
    mouse: true,
    items: scripts,
    style: {
        selected: { bg: 'blue' },
        item: { hover: { bg: 'green' } }
    }
});

screen.append(sidebar);
screen.append(rightContainer);

const logBoxes = [];
const logWatchers = [];
(config.logs ?? []).forEach((cfgPath, idx) => {
    const width = Math.floor(100 / ((config.logs ?? []).length || 1));
    const resolved = resolveLatest(cfgPath);
    const box = blessed.log({
        parent: logsContainer,
        label: path.basename(resolved),
        left: `${idx * width}%`,
        width: `${width}%`,
        height: '100%-2',
        top: 1,
        border: 'line',
        scrollable: true,
        alwaysScroll: true,
        scrollbar: {
            ch: ' ',
            track: { bg: 'grey' },
            style: { inverse: true }
        },
        style: {
            border: { fg: 'grey' },
            focus: {
                border: { fg: 'cyan' }
            }
        }
    });
    logBoxes.push(box);
    try {
        const tail = startTail(resolved, box);
        logWatchers.push({ configPath: cfgPath, currentPath: resolved, box, tail });
    } catch (err) {
        box.log(`Cannot watch ${resolved}: ${err.message}`);
    }
});

setInterval(() => {
    logWatchers.forEach(w => {
        if (!w.configPath.includes('{{latest}}')) return;
        const newPath = resolveLatest(w.configPath);
        if (newPath === w.currentPath) return;
        try {
            w.tail.unwatch();
        } catch { /* ignore */ }
        w.box.setLabel(path.basename(newPath));
        w.tail = startTail(newPath, w.box);
        w.currentPath = newPath;
        screen.render();
    });
}, 60000);

list.focus();

const focusables = [list, ...logBoxes, stopButton, inputBox, output];
let focusIndex = 0;

screen.key('tab', () => {
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render();
});

// Bind keys from config.keyBindings (NEW)
if (config.keyBindings) {
    for (const [keyName, scriptFile] of Object.entries(config.keyBindings)) {
        screen.key(keyName, () => {
            runScriptFile(scriptFile);
        });
    }
}

screen.key(['q', 'C-c'], () => {
    if (current) {
        current.kill();
        output.log('[dash] Process killed before exit.');
    }
    output.log('[dash] Exiting.');
    screen.render();
    process.exit(0);
});

screen.key('C-r', () => {
    if (!lastCommand) {
        output.log('[dash] No previous process to run.');
        screen.render();
        return;
    }

    if (lastCommand.kind === 'script') {
        runScript(lastCommand.value).catch(err => {
            output.log(`Error: ${err.message}`);
            screen.render();
        });
    } else {
        runCustomCommand(lastCommand.value);
    }
});

// Keyboard shortcut to stop the current process
screen.key('C-s', () => {
    stopCurrentProcess();
});

focusables.forEach(el => {
    el.key('up', () => { el.scroll(-1); screen.render(); });
    el.key('down', () => { el.scroll(1); screen.render(); });
});

list.on('select', (_, index) => {
    const key = Object.keys(config.keyBindings ?? {})[index];
    const file = config.keyBindings?.[key];
    if (file) runScriptFile(file);
});

stopButton.on('press', () => {
    stopCurrentProcess();
});

inputBox.on('submit', (value) => {
    const trimmed = (value || '').trim();
    if (!trimmed) return;

    // add to command history
    commandHistory.push(trimmed);
    historyIndex = commandHistory.length;
    runCustomCommand(trimmed);
    inputBox.clearValue();
    screen.render();
});

inputBox.on('keypress', (_, key) => {
    if (config.settings?.enableCommandHistory !== true) return;
    if (commandHistory.length === 0) return;

    if (key.name === 'up') {
        if (historyIndex > 0) historyIndex--;
        inputBox.setValue(commandHistory[historyIndex] ?? '');
        inputBox.cursor = inputBox.getValue().length;
        screen.render();
    }

    if (key.name === 'down') {
        if (historyIndex < commandHistory.length - 1) {
            historyIndex++;
            inputBox.setValue(commandHistory[historyIndex] ?? '');
        } else {
            historyIndex = commandHistory.length;
            inputBox.clearValue();
        }
        inputBox.cursor = inputBox.getValue().length;
        screen.render();
    }
});

inputBox.on('cancel', () => {
    inputBox.clearValue();
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render();
});

inputBox.key(['escape'], () => {
    inputBox.cancel();
});

// NOTE: moved here exactly like your original file layout
let current = null;
const commandHistory = [];
let historyIndex = -1;

function stopCurrentProcess() {
    if (current) {
        current.kill();
        current = null;
        output.log('[dash] Current process killed.');
        screen.render();
    }
}

function runCustomCommand(command) {
    const trimmed = (command || '').trim();
    if (!trimmed) return;

    if (current) {
        current.kill();
        output.log('[dash] Previous process killed.');
    }

    output.log('--------------------------------------------------');
    output.log(`[dash] Executing custom command: ${trimmed}`);

    const args = trimmed.split(' ');
    const cmd = args.shift();

    current = spawn(cmd, args, { shell: true });
    setOutputLabel(trimmed);
    lastCommand = { kind: 'custom', value: trimmed };

    current.stdout.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.on('close', code => {
        output.log(`[dash] Process exited with code ${code}`);
        current = null;

        if (config.settings?.notifyOnExit) {
            screen.program.bell();
            output.log('[✓] Done.');
        }

        screen.render();
    });
}

async function closePorts(method, ports) {
    for (const p of ports) {
        const port = parseInt(p, 10);
        if (isNaN(port)) continue;
        try {
            await kill(port, method);
            output.log(`Closed ${method.toUpperCase()} port ${port}`);
        } catch (err) {
            output.log(`Error closing ${method.toUpperCase()} port ${port}: ${err.message}`);
        }
        screen.render();
    }
}

function execShell(cmd) {
    return new Promise((resolve) => {
        exec(cmd, { windowsHide: true }, (err, stdout, stderr) => {
            resolve({
                code: err ? (err.code || 1) : 0,
                stdout: stdout || '',
                stderr: stderr || (err ? err.message : '')
            });
        });
    });
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function isAlive(pid) {
    try { process.kill(pid, 0); return true; }
    catch (e) { return e && e.code === 'EPERM' ? true : false; }
}

async function waitForExit(pid, timeoutMs) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        if (!isAlive(pid)) return true;
        await sleep(100);
    }
    return !isAlive(pid);
}

// Linux: lecture /proc/*/cmdline (robuste, no ps/pgrep)
// Windows: tasklist CSV
function readCmdline(pid) {
    try {
        const raw = fs.readFileSync(`/proc/${pid}/cmdline`);
        // cmdline is \0-separated
        return raw.toString('utf8').split('\0').filter(Boolean).join(' ');
    } catch {
        return '';
    }
}

async function getPidsByName(name) {
    const needle = String(name).toLowerCase();
    const selfPid = process.pid;
    const results = new Set();

    if (process.platform === 'win32') {
        const { stdout } = await execShell(`tasklist /FO CSV /NH`);
        stdout.split(/\r?\n/).forEach(line => {
            if (!line.trim()) return;
            const parts = line.split('","').map(s => s.replace(/(^"|"$)/g, ''));
            const procName = (parts[0] || '').toLowerCase();
            const pid = parseInt(parts[1], 10);
            if (!isNaN(pid) && pid !== selfPid && procName.includes(needle)) {
                results.add(pid);
            }
        });
        return Array.from(results);
    }

    // Linux /proc scan
    let pids;
    try {
        pids = fs.readdirSync('/proc').filter(d => /^\d+$/.test(d));
    } catch {
        pids = [];
    }

    for (const d of pids) {
        const pid = parseInt(d, 10);
        if (pid === selfPid) continue;
        const cmd = readCmdline(pid).toLowerCase();
        // Si cmdline vide, certains démons: on peut fallback sur exe
        if (!cmd) continue;
        if (cmd.includes(needle)) results.add(pid);
    }
    return Array.from(results);
}

async function killPid(pid) {
    // SIGTERM
    try { process.kill(pid, 'SIGTERM'); } catch (e) {
        if (e && e.code === 'ESRCH') return { ok: true, method: 'already-dead' };
        if (e && e.code === 'EPERM') return { ok: false, method: 'SIGTERM', error: 'EPERM' };
        // autre erreur -> on forcera quand même
    }
    const graceful = await waitForExit(pid, 1500);
    if (graceful) return { ok: true, method: 'SIGTERM' };

    // FORCE
    if (process.platform === 'win32') {
        const { code, stderr } = await execShell(`taskkill /PID ${pid} /T /F`);
        return { ok: code === 0, method: 'taskkill', error: code !== 0 ? stderr : undefined };
    } else {
        try { process.kill(pid, 'SIGKILL'); }
        catch (e) {
            if (e && e.code === 'ESRCH') return { ok: true, method: 'SIGKILL' };
            if (e && e.code === 'EPERM') return { ok: false, method: 'SIGKILL', error: 'EPERM' };
            return { ok: false, method: 'SIGKILL', error: e.message || 'unknown' };
        }
        const hard = await waitForExit(pid, 1000);
        return { ok: hard, method: 'SIGKILL', error: hard ? undefined : 'still alive' };
    }
}

async function killProcesses(params) {
    const selfPid = process.pid;

    for (const target of params) {
        // PID direct
        if (/^\d+$/.test(String(target))) {
            const pid = parseInt(String(target), 10);
            if (pid === selfPid) { output.log(`[killProcess] skip self PID ${pid}`); continue; }
            output.log(`[killProcess] target PID ${pid}`);
            const res = await killPid(pid);
            if (res.ok) output.log(`[killProcess] killed PID ${pid} via ${res.method}`);
            else output.log(`[killProcess] failed PID ${pid} via ${res.method}: ${res.error || 'unknown'}`);
            screen.render();
            continue;
        }

        // Nom / motif substring (cmdline)
        const name = String(target).trim();
        output.log(`[killProcess] find by name "${name}"`);
        const pids = await getPidsByName(name);
        output.log(`[killProcess] PIDs: ${pids.length ? pids.join(', ') : '(none)'}`);

        for (const pid of pids) {
            if (pid === selfPid) { output.log(`[killProcess] skip self PID ${pid}`); continue; }
            const res = await killPid(pid);
            if (res.ok) output.log(`[killProcess] killed "${name}" (PID ${pid}) via ${res.method}`);
            else output.log(`[killProcess] failed "${name}" (PID ${pid}) via ${res.method}: ${res.error || 'unknown'}`);
        }
        screen.render();
    }
}


// Execute a list of actions with shared handlers
async function executeActions(actions, label = 'actions') {
    // Normalize and no-op
    if (!actions || actions.length === 0) return;

    output.log(`Executing ${label}...`);
    for (const act of actions) {
        const params = Array.isArray(act.parameters)
            ? act.parameters
            : (act.parameters != null ? [act.parameters] : []);

        if (act.action === 'closeTCPPorts') {
            output.log(`Closing TCP ports ${params}`);
            await closePorts('tcp', params);
        } else if (act.action === 'closeUDPPorts') {
            output.log(`Closing UDP ports ${params}`);
            await closePorts('udp', params);
        } else if (act.action === 'killProcess') {
            output.log(`Killing processes: ${params}`);
            await killProcesses(params);
        } else {
            output.log(`[${label}] unknown action "${act.action}"`);
        }
    }
    screen.render();
}


async function executeBeforeActions(name) {
    const entries = (config.events?.beforeStartProcess || []).filter(e => e && e.script === name);
    const actions = entries.flatMap(e => Array.isArray(e.actions) ? e.actions : []);
    await executeActions(actions, 'pre-launch actions');
}
// Execute actions once when the dashboard starts
async function executeStartupActions() {
    const actions = config.events?.onDashboardStart;
    await executeActions(actions, 'startup actions');
}
async function runScript(name) {
    if (current) {
        current.kill();
        current = null;
    }

    output.log(`--------------------------------------------------`);

    await executeBeforeActions(name);
    current = spawn('npm', ['run', name], { shell: true });
    lastCommand = { kind: 'script', value: name };

    setOutputLabel(name);

    current.stdout.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.on('close', code => {
        output.log(`Process exited with code ${code}`);
        current = null;
        screen.render();
    });
}

async function runScriptFile(scriptFile) {
    if (current) {
        current.kill();
        current = null;
    }

    const scriptPath = path.join(config.scriptsDir ?? './scripts', scriptFile);

    output.log(`--------------------------------------------------`);
    output.log(`[dash] Running: ${scriptPath}`);

    await executeBeforeActions(path.basename(scriptFile, path.extname(scriptFile)));

    // If the script file doesn't exist in scriptsDir, treat it as a shell command
    if (!fs.existsSync(scriptPath)) {
        output.log(`[dash] Not found in scriptsDir. Executing as shell command: ${scriptFile}`);
        current = spawn(scriptFile, { shell: true });
        lastCommand = { kind: 'custom', value: scriptFile };
        setOutputLabel(scriptFile);
    } else {
        current = spawn(scriptPath, { shell: true });
        lastCommand = { kind: 'custom', value: scriptPath };
        setOutputLabel(path.basename(scriptPath));
    }

    current.stdout.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        screen.render();
    });

    current.on('close', code => {
        output.log(`Process exited with code ${code}`);
        current = null;
        screen.render();
    });
}

// Kick off one-shot startup actions
(async () => {
    try {
        await executeStartupActions();
    } catch (err) {
        output.log(`[startup] error: ${err?.message || err}`);
        screen.render();
    }
})();

screen.render();

export { initLogFile, stopLogFile };
