#ifndef EXOS_CONSOLE_H
#define EXOS_CONSOLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Console cell: character + attribute (driver-defined semantics). */
typedef struct {
	uint16_t ch;     /* Unicode codepoint if you want; ASCII for now */
	uint16_t attr;   /* Color/flags as understood by the device */
} ConsoleCell;

/* Forward decl. */
struct ConsoleDriver;

/* Driver vtable for a console device (hardware or virtual). */
typedef struct {
	/* Write a single cell at (x,y). Must clip internally. */
	void (*put_cell)(struct ConsoleDriver* dev, int x, int y, ConsoleCell cell);

	/* Fast path: write a run of cells on the same row starting at (x,y). */
	void (*put_run)(struct ConsoleDriver* dev, int x, int y, const ConsoleCell* cells, size_t count);

	/* Fill a rectangular region [x, x+w), [y, y+h) with cell. */
	void (*fill_rect)(struct ConsoleDriver* dev, int x, int y, int w, int h, ConsoleCell cell);

	/* Scroll a rectangular region by dy rows (positive = down, negative = up). Newly exposed area is filled with 'blank'. */
	void (*scroll_rect)(struct ConsoleDriver* dev, int x, int y, int w, int h, int dy, ConsoleCell blank);

	/* Set cursor position/visibility if supported. */
	void (*set_cursor)(struct ConsoleDriver* dev, int x, int y, bool visible);

	/* Present/flush if buffered; can be a no-op for immediate devices. */
	void (*present)(struct ConsoleDriver* dev);

	/* Optional: translate abstract attrs to device attrs. Can be NULL. */
	uint16_t (*translate_attr)(struct ConsoleDriver* dev, uint16_t abstract_attr);
} ConsoleDriverOps;

/* A concrete console device (VGA text, framebuffer, offscreen memory...). */
typedef struct ConsoleDriver {
	const ConsoleDriverOps* ops;
	int cols;
	int rows;
	void* priv; /* backend-private state */
} ConsoleDriver;

/* High-level host providing convenient operations and cursor management. */
typedef struct {
	ConsoleDriver* dev;
	int cols;
	int rows;
	int cur_x;
	int cur_y;
	bool cursor_visible;
	uint16_t cur_attr;       /* current attribute for text output */
	ConsoleCell clear_cell;  /* background used for clear/scroll */
} ConsoleHost;

/* Host lifecycle */
void console_host_init(ConsoleHost* host, ConsoleDriver* dev, uint16_t default_attr);
void console_host_clear(ConsoleHost* host);
void console_host_set_cursor(ConsoleHost* host, int x, int y, bool visible);
void console_host_set_attr(ConsoleHost* host, uint16_t attr);

/* Text output primitives (no ANSI parsing here, raw only) */
void console_host_putc(ConsoleHost* host, uint16_t ch);
void console_host_write(ConsoleHost* host, const uint16_t* text, size_t count); /* UTF-16-ish or ASCII in low byte */
void console_host_write_ascii(ConsoleHost* host, const char* s);

/* Region ops */
void console_host_fill(ConsoleHost* host, int x, int y, int w, int h, ConsoleCell cell);
void console_host_scroll_up(ConsoleHost* host, int lines);

/* Flush if needed */
void console_host_present(ConsoleHost* host);

#endif /* EXOS_CONSOLE_H */
