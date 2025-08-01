# Kernel documentation

## Architecture

To be completed.

## Modules and functions

### Clock.c

Implements the system timer and time utilities.

#### Functions in Clock.c

- InitializeClock: Configures the timer chip and enables IRQ 0.
- GetSystemTime: Returns the raw system time in milliseconds.
- MilliSecondsToHMS: Converts milliseconds to a HH:MM:SS string.
- ClockHandler: Interrupt handler that updates the system time and triggers the scheduler.
- ReadCMOS: Reads a byte from CMOS memory.
- WriteCMOS: Writes a byte to CMOS memory.
- GetLocalTime: Reads date and time from CMOS.

### Console.c

Provides text console output and input features.

#### Functions in Console.c

- SetConsoleCursorPosition: Moves the hardware cursor to a given position.
- SetConsoleCharacter: Writes a character with attributes at the current cursor location.
- ScrollConsole: Scrolls the console one line up when needed.
- ClearConsole: Clears the entire console screen.
- ConsolePrintChar: Displays a character and handles newlines and tabs.
- ConsoleBackSpace: Deletes the character before the cursor.
- SetConsoleBackColor: Changes the background color.
- SetConsoleForeColor: Changes the foreground color.
- ConsolePrint: Displays a string on the console.
- SkipAToI: Converts numeric characters to an integer (internal).
- VarKernelPrintNumber: Formats and prints a number.
- VarKernelPrint: Core printf-style formatter used by KernelPrint.
- KernelPrint: Prints formatted text to the console.
- ConsoleGetString: Reads a line of text from the console.
- ConsoleInitialize: Initializes the console state.

### Crypt.c

Provides basic password handling functions.

#### Functions in Crypt.c

- MakePassword: Generates an encrypted password from plain text.
- CheckPassword: Verifies a plain text password against an encrypted one.
