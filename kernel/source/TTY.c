
/***************************************************************************\

    TTY mechanism

        ┌──────────────────────────────────────────────────┐
        │                  User Process                    │
        │  (e.g., shell, htop, interactive program)        │
        └──────────────────────────────────────────────────┘
                    │                   ▲
        write() ----┘                   │---- read()
                    ▼                   │
        ┌──────────────────────────────────────────────────┐
        │             TTY Interface (syscalls)             │
        │  - tty_write(): push to output buffer            │
        │  - tty_read():  fetch from input buffer          │
        └──────────────────────────────────────────────────┘
                    │                   ▲
                    ▼                   │
        ┌──────────────────────────────────────────────────┐
        │           Line Discipline Layer                  │
        │  - Canonical mode: local editing, echo, CR→LF    │
        │  - Raw mode: direct character feed               │
        │  - Handles Ctrl+C, Ctrl+D, ESC sequences         │
        │  - Circular input/output buffers                 │
        └──────────────────────────────────────────────────┘
                    │                   ▲
         output --->│                   │<--- input
                    ▼                   │
        ┌──────────────────────────────────────────────────┐
        │          Physical Terminal Driver                │
        │ (VGA text, framebuffer, serial, network term)    │
        │  - Writes display data to screen                 │
        │  - Receives keyboard/serial input                │
        └──────────────────────────────────────────────────┘
                    │                   ▲
        to screen   │                   │ from keyboard
                    ▼                   │
        ┌──────────────────────────────────────────────────┐
        │             Hardware / Device Layer              │
        │  - Keyboard controller (PS/2, USB)               │
        │  - Video controller (VGA, GPU)                   │
        │  - Serial UART                                   │
        └──────────────────────────────────────────────────┘

\***************************************************************************/

// TODO...