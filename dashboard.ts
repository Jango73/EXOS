import fs from 'fs';
import path from 'path';
import { spawn, ChildProcessWithoutNullStreams } from 'child_process';
import blessed from 'blessed';
import { Tail } from 'tail';
import kill from 'kill-port';

/*
setTimeout(() => {
    output.log('[dash] Timeout reached, exiting.');
    screen.render();
    process.exit(1);
}, 60000);
*/

interface Scripts {
    [key: string]: string;
}

interface ActionDescriptor {
    action: string;
    parameters?: string[];
}

interface Config {
    logs?: string[];
    beforeStartProcess?: {
        [key: string]: ActionDescriptor[];
    };
    settings?: {
        enableCommandHistory?: boolean;
        persistLogs?: boolean;
        notifyOnExit?: boolean;
    };
    scriptsDir?: string; // NEW: directory for scripts
    keyBindings?: {      // NEW: key -> script file mapping
        [key: string]: string;
    };
}

const defaultSettings = {
    enableCommandHistory: true,
    persistLogs: true,
    notifyOnExit: true
};

let logStream: fs.WriteStream | null = null;

interface LastCommand {
    kind: 'script' | 'custom';
    value: string;
}

let lastCommand: LastCommand | null = null;

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

function loadConfig(): Config {
    try {
        const cfgPath = path.join(process.cwd(), 'dash.json');
        const raw = fs.readFileSync(cfgPath, 'utf-8');
        const cfg = JSON.parse(raw) as Config;
        return {
            ...cfg,
            settings: {
                ...defaultSettings,
                ...cfg.settings
            }
        };
    } catch {
        return { settings: { ...defaultSettings } };
    }
}

function escapeRegex(s: string): string {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function parseLogDate(s: string): number {
    const iso = s.replace(/T(\d{2})-(\d{2})-(\d{2})/, 'T$1:$2:$3');
    return new Date(iso).getTime();
}

function resolveLatest(p: string): string {
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
            .filter((v): v is { dateStr: string; timestamp: number } => v !== null)
            .sort((a, b) => b.timestamp - a.timestamp);

        if (entries.length === 0) return path.join(dir, `${prefix}latest${after}`);

        return path.join(dir, `${prefix}${entries[0].dateStr}${after}`);
    } catch {
        return path.join(dir, `${prefix}latest${after}`);
    }
}

// Load scripts from keyBindings in config (instead of package.json)
function loadScripts(): string[] {
    if (config.keyBindings) {
        return Object.entries(config.keyBindings).map(([key, script]) => `${key.toUpperCase()} - ${script}`);
    }
    return [];
}

function setOutputLabel(label: string) {
    output.setLabel(` ${label} `);
    screen.render();
}

interface LogWatcher {
    configPath: string;
    currentPath: string;
    box: blessed.Widgets.Log;
    tail: Tail;
}

function startTail(file: string, box: blessed.Widgets.Log): Tail {
    if (!fs.existsSync(file)) fs.writeFileSync(file, '');
    const tail = new Tail(file, {
        follow: true,
        useWatchFile: true,
        fsWatchOptions: { interval: 500 }
    });
    tail.on('line', (line: string) => {
        box.log(line);
        screen.render();
    });
    tail.on('error', (err: Error) => {
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
output.log = ((msg: string) => {
    originalOutputLog(msg);
    if (logStream) {
        logStream.write(`[${new Date().toISOString()}] ${msg}\n`);
    }
}) as typeof output.log;

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

const logBoxes: blessed.Widgets.Log[] = [];
const logWatchers: LogWatcher[] = [];
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
        box.log(`Cannot watch ${resolved}: ${(err as Error).message}`);
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

const focusables: blessed.Widgets.BlessedElement[] = [list, ...logBoxes, stopButton, inputBox, output];
let focusIndex = 0;

screen.key('tab', () => {
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render();
});

// Bind keys from config.keyBindings
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
            output.log(`Error: ${(err as Error).message}`);
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
    el.key('up', () => { (el as any).scroll(-1); screen.render(); });
    el.key('down', () => { (el as any).scroll(1); screen.render(); });
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
    const trimmed = value.trim();
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
        (inputBox as any).cursor = inputBox.getValue().length;
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
        (inputBox as any).cursor = inputBox.getValue().length;
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

let current: ChildProcessWithoutNullStreams | null = null;
const commandHistory: string[] = [];
let historyIndex = -1;

function stopCurrentProcess() {
    if (current) {
        current.kill();
        current = null;
        output.log('[dash] Current process killed.');
        screen.render();
    }
}

function runCustomCommand(command: string) {
    const trimmed = command.trim();
    if (!trimmed) return;

    if (current) {
        current.kill();
        output.log('[dash] Previous process killed.');
    }

    output.log('--------------------------------------------------');
    output.log(`[dash] Executing custom command: ${trimmed}`);

    const args = trimmed.split(' ');
    const cmd = args.shift()!;

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

async function closePorts(method: 'tcp' | 'udp', ports: string[]) {
    for (const p of ports) {
        const port = parseInt(p, 10);
        if (isNaN(port)) continue;
        try {
            await kill(port, method);
            output.log(`Closed ${method.toUpperCase()} port ${port}`);
        } catch (err) {
            output.log(`Error closing ${method.toUpperCase()} port ${port}: ${(err as Error).message}`);
        }
        screen.render();
    }
}

async function executeBeforeActions(name: string) {
    const actions = config.beforeStartProcess?.[name];
    if (!actions) return;

    output.log(`Executing pre-launch actions...`);

    for (const act of actions) {
        const params = act.parameters ?? [];
        if (act.action === 'closeTCPPorts') {
            output.log(`Closing processes on TCP ports ${params}`);
            await closePorts('tcp', params);
        } else if (act.action === 'closeUDPPorts') {
            output.log(`Closing processes on UDP ports ${params}`);
            await closePorts('udp', params);
        }
    }
}

async function runScript(name: string) {
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

// NEW: Run script file directly from scriptsDir
async function runScriptFile(scriptFile: string) {
    if (current) {
        current.kill();
        current = null;
    }

    const scriptPath = path.join(config.scriptsDir ?? './scripts', scriptFile);

    output.log(`--------------------------------------------------`);
    output.log(`[dash] Running: ${scriptPath}`);

    await executeBeforeActions(path.basename(scriptFile, path.extname(scriptFile)));

    current = spawn(scriptPath, { shell: true });
    lastCommand = { kind: 'custom', value: scriptPath };

    setOutputLabel(path.basename(scriptPath));

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

screen.render();

export { initLogFile, stopLogFile };
