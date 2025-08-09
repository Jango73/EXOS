import curses, subprocess, threading

LOG_FILE = "./boot-qemu-hd/debug.log"
COMMANDS = {
    curses.KEY_F1: ["./scripts/1-setup-deps.sh"],
    curses.KEY_F2: ["./scripts/2-setup-qemu-freedos.sh"],
    curses.KEY_F3: ["./scripts/3-format-all-code.sh"],
    curses.KEY_F4: ["./scripts/4-1-clean-build-exos.sh"],
    curses.KEY_F5: ["./scripts/4-2-build-exos.sh"],
    curses.KEY_F6: ["./scripts/5-1-start-qemu-hd.sh"],
    curses.KEY_F7: ["./scripts/5-2-debug-qemu-hd.sh"]
}

def tail_log(win):
    with open(LOG_FILE, "r") as f:
        f.seek(0, 2)
        while True:
            line = f.readline()
            if not line:
                curses.napms(100)
                continue
            win.addstr(line)
            win.refresh()

def main(stdscr):
    curses.curs_set(0)
    log_win = curses.newwin(curses.LINES-1, curses.COLS, 0, 0)
    status_win = curses.newwin(1, curses.COLS, curses.LINES-1, 0)
    status_win.addstr("F5=Build  F6=Run  F7=Clean  Q=Quit")
    status_win.refresh()

    threading.Thread(target=tail_log, args=(log_win,), daemon=True).start()

    while True:
        ch = stdscr.getch()
        if ch in (ord('q'), ord('Q')):
            break
        if ch in COMMANDS:
            subprocess.Popen(COMMANDS[ch])

curses.wrapper(main)
