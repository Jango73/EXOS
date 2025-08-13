#ifndef EXOS_CONSOLE_VGA_TEXT_H
#define EXOS_CONSOLE_VGA_TEXT_H

#include "console.h"

/* Create a VGA text mode console driver (80x25, B800:0000). */
void console_vga_text_init(ConsoleDriver* dev);

#endif /* EXOS_CONSOLE_VGA_TEXT_H */
