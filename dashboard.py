import curses
import subprocess
import threading
import os

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

def tail_log(win, height, width):
    """Affiche le log en continu dans la zone prévue."""
    if not os.path.exists(LOG_FILE):
        open(LOG_FILE, "w").close()

    with open(LOG_FILE, "r") as f:
        f.seek(0, 2)  # se placer à la fin
        while True:
            line = f.readline()
            if not line:
                curses.napms(100)
                continue
            win.addstr(line[:width-1])  # tronquer la ligne
            if win.getyx()[0] >= height-1:
                win.scroll(1)
            win.refresh()

def main(stdscr):
    curses.curs_set(0)
    curses.start_color()
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)  # barre du bas

    h, w = stdscr.getmaxyx()

    # Fenêtre log avec cadre
    log_frame = curses.newwin(h-2, w, 0, 0)
    log_frame.box()
    log_frame.addstr(0, 2, " LOG ")
    log_frame.refresh()

    # Fenêtre intérieure pour afficher le log
    log_pad = curses.newwin(h-4, w-2, 1, 1)
    log_pad.addstr("(en attente de log...)\n")
    log_pad.refresh()

    # Barre de statut
    status_win = curses.newwin(1, w, h-1, 0)
    status_win.bkgd(' ', curses.color_pair(1))
    status_win.addstr(0, 0, "F1..F7=Scripts  Q=Quit")
    status_win.refresh()

    # Thread tail log
    threading.Thread(target=tail_log, args=(log_pad, h-4, w-2), daemon=True).start()

    while True:
        ch = stdscr.getch()
        if ch in (ord('q'), ord('Q')):
            break
        if ch in COMMANDS:
            subprocess.Popen(COMMANDS[ch])

curses.wrapper(main)
