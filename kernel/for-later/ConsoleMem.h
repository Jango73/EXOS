#ifndef EXOS_CONSOLE_MEM_H
#define EXOS_CONSOLE_MEM_H

#include "console.h"

/* Create an offscreen memory console (cols×rows), no hardware. */
bool console_mem_init(ConsoleDriver* dev, int cols, int rows);

/* Access raw buffer for debugging/snapshots (each cell is ConsoleCell). */
const ConsoleCell* console_mem_buffer(const ConsoleDriver* dev);

#endif /* EXOS_CONSOLE_MEM_H */
