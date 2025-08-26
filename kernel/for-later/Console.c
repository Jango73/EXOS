#include "console.h"
#include <string.h>

/* Internal helpers */
static inline uint16_t map_attr(ConsoleDriver* dev, uint16_t a) {
	if (dev->ops->translate_attr) return dev->ops->translate_attr(dev, a);
	return a;
}

void console_host_init(ConsoleHost* host, ConsoleDriver* dev, uint16_t default_attr) {
	/* Basic preconditions */
	host->dev = dev;
	host->cols = dev->cols;
	host->rows = dev->rows;
	host->cur_x = 0;
	host->cur_y = 0;
	host->cursor_visible = true;
	host->cur_attr = default_attr;
	host->clear_cell.ch = ' ';
	host->clear_cell.attr = map_attr(dev, default_attr);
	console_host_clear(host);
}

void console_host_clear(ConsoleHost* host) {
	ConsoleCell cell = host->clear_cell;
	host->dev->ops->fill_rect(host->dev, 0, 0, host->cols, host->rows, cell);
	host->cur_x = 0;
	host->cur_y = 0;
	host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, host->cursor_visible);
	host->dev->ops->present(host->dev);
}

void console_host_set_cursor(ConsoleHost* host, int x, int y, bool visible) {
	if (x < 0) x = 0; if (x >= host->cols) x = host->cols - 1;
	if (y < 0) y = 0; if (y >= host->rows) y = host->rows - 1;
	host->cur_x = x;
	host->cur_y = y;
	host->cursor_visible = visible;
	host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, visible);
}

void console_host_set_attr(ConsoleHost* host, uint16_t attr) {
	host->cur_attr = attr;
}

static void newline(ConsoleHost* host) {
	host->cur_x = 0;
	host->cur_y++;
	if (host->cur_y >= host->rows) {
		/* Scroll up one line */
		console_host_scroll_up(host, 1);
		host->cur_y = host->rows - 1;
	}
	host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, host->cursor_visible);
}

void console_host_putc(ConsoleHost* host, uint16_t ch) {
	/* Minimal control handling: \n, \r, \t, backspace. ANSI is out of scope here. */
	if (ch == '\n') { newline(host); return; }
	if (ch == '\r') { host->cur_x = 0; host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, host->cursor_visible); return; }
	if (ch == '\t') {
		int next_tab = (host->cur_x + 8) & ~7;
		if (next_tab >= host->cols) { newline(host); return; }
		ConsoleCell sp = { .ch = ' ', .attr = map_attr(host->dev, host->cur_attr) };
		host->dev->ops->fill_rect(host->dev, host->cur_x, host->cur_y, next_tab - host->cur_x, 1, sp);
		host->cur_x = next_tab;
		host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, host->cursor_visible);
		return;
	}
	if (ch == '\b') {
		if (host->cur_x > 0) {
			host->cur_x--;
			ConsoleCell sp = { .ch = ' ', .attr = map_attr(host->dev, host->cur_attr) };
			host->dev->ops->put_cell(host->dev, host->cur_x, host->cur_y, sp);
			host->dev->ops->set_cursor(host->dev, host->cur_x, host->cur_y, host->cursor_visible);
		}
		return;
	}

	ConsoleCell cell = { .ch = ch, .attr = map_attr(host->dev, host->cur_attr) };
	host->dev->ops->put_cell(host->dev, host->cur_x, host->cur_y, cell);

	host->cur_x++;
	if (host->cur_x >= host->cols) {
		newline(host);
	}
}

void console_host_write(ConsoleHost* host, const uint16_t* text, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		console_host_putc(host, text[i] & 0xFF); /* ASCII for now */
	}
}

void console_host_write_ascii(ConsoleHost* host, const char* s) {
	while (*s) {
		console_host_putc(host, (uint8_t)*s++);
	}
}

void console_host_fill(ConsoleHost* host, int x, int y, int w, int h, ConsoleCell cell) {
	cell.attr = map_attr(host->dev, cell.attr);
	host->dev->ops->fill_rect(host->dev, x, y, w, h, cell);
}

void console_host_scroll_up(ConsoleHost* host, int lines) {
	if (lines <= 0) return;
	if (lines > host->rows) lines = host->rows;
	ConsoleCell blank = host->clear_cell;
	host->dev->ops->scroll_rect(host->dev, 0, 0, host->cols, host->rows, -lines, blank);
}

void console_host_present(ConsoleHost* host) {
	host->dev->ops->present(host->dev);
}
