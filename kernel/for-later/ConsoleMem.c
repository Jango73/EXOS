#include <stdlib.h>
#include <string.h>

#include "console_mem.h"

typedef struct {
    ConsoleCell* buf; /* rows*cols cells */
} MemState;

static uint16_t map_passthrough(ConsoleDriver* dev, uint16_t a) {
    (void)dev;
    return a;
}

static void mem_put_cell(ConsoleDriver* dev, int x, int y, ConsoleCell cell) {
    if ((unsigned)x >= (unsigned)dev->cols || (unsigned)y >= (unsigned)dev->rows) return;
    MemState* st = (MemState*)dev->priv;
    st->buf[(size_t)y * (size_t)dev->cols + (size_t)x] = cell;
}

static void mem_put_run(ConsoleDriver* dev, int x, int y, const ConsoleCell* cells, size_t count) {
    if ((unsigned)y >= (unsigned)dev->rows) return;
    if (x < 0) {
        size_t skip = (size_t)(-x);
        if (skip >= count) return;
        cells += skip;
        count -= skip;
        x = 0;
    }
    if (x + (int)count > dev->cols) count = (size_t)(dev->cols - x);
    MemState* st = (MemState*)dev->priv;
    ConsoleCell* base = st->buf + (size_t)y * (size_t)dev->cols + (size_t)x;
    memcpy(base, cells, count * sizeof(ConsoleCell));
}

static void mem_fill_rect(ConsoleDriver* dev, int x, int y, int w, int h, ConsoleCell cell) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= dev->cols || y >= dev->rows) return;
    if (x + w > dev->cols) w = dev->cols - x;
    if (y + h > dev->rows) h = dev->rows - y;

    MemState* st = (MemState*)dev->priv;
    for (int row = 0; row < h; ++row) {
        ConsoleCell* line = st->buf + (size_t)(y + row) * (size_t)dev->cols + (size_t)x;
        for (int col = 0; col < w; ++col) line[col] = cell;
    }
}

static void mem_scroll_rect(ConsoleDriver* dev, int x, int y, int w, int h, int dy, ConsoleCell blank) {
    if (w <= 0 || h <= 0 || dy == 0) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= dev->cols || y >= dev->rows) return;
    if (x + w > dev->cols) w = dev->cols - x;
    if (y + h > dev->rows) h = dev->rows - y;

    MemState* st = (MemState*)dev->priv;
    if (dy < 0) {
        int shift = -dy;
        if (shift >= h) {
            mem_fill_rect(dev, x, y, w, h, blank);
            return;
        }
        for (int row = 0; row < h - shift; ++row) {
            ConsoleCell* dst = st->buf + (size_t)(y + row) * (size_t)dev->cols + (size_t)x;
            ConsoleCell* src = st->buf + (size_t)(y + row + shift) * (size_t)dev->cols + (size_t)x;
            memmove(dst, src, (size_t)w * sizeof(ConsoleCell));
        }
        for (int row = h - shift; row < h; ++row) {
            ConsoleCell* line = st->buf + (size_t)(y + row) * (size_t)dev->cols + (size_t)x;
            for (int col = 0; col < w; ++col) line[col] = blank;
        }
    } else {
        int shift = dy;
        if (shift >= h) {
            mem_fill_rect(dev, x, y, w, h, blank);
            return;
        }
        for (int row = h - 1; row >= shift; --row) {
            ConsoleCell* dst = st->buf + (size_t)(y + row) * (size_t)dev->cols + (size_t)x;
            ConsoleCell* src = st->buf + (size_t)(y + row - shift) * (size_t)dev->cols + (size_t)x;
            memmove(dst, src, (size_t)w * sizeof(ConsoleCell));
        }
        for (int row = 0; row < shift; ++row) {
            ConsoleCell* line = st->buf + (size_t)(y + row) * (size_t)dev->cols + (size_t)x;
            for (int col = 0; col < w; ++col) line[col] = blank;
        }
    }
}

static void mem_set_cursor(ConsoleDriver* dev, int x, int y, bool visible) {
    (void)dev;
    (void)x;
    (void)y;
    (void)visible; /* No hardware cursor */
}

static void mem_present(ConsoleDriver* dev) { (void)dev; /* No-op */ }

static const ConsoleDriverOps MEM_OPS = {
    .put_cell = mem_put_cell,
    .put_run = mem_put_run,
    .fill_rect = mem_fill_rect,
    .scroll_rect = mem_scroll_rect,
    .set_cursor = mem_set_cursor,
    .present = mem_present,
    .translate_attr = map_passthrough};

bool console_mem_init(ConsoleDriver* dev, int cols, int rows) {
    if (cols <= 0 || rows <= 0) return false;
    MemState* st = (MemState*)malloc(sizeof(MemState));
    if (!st) return false;
    size_t n = (size_t)cols * (size_t)rows;
    st->buf = (ConsoleCell*)malloc(n * sizeof(ConsoleCell));
    if (!st->buf) {
        free(st);
        return false;
    }

    dev->ops = &MEM_OPS;
    dev->cols = cols;
    dev->rows = rows;
    dev->priv = st;

    /* Clear buffer */
    ConsoleCell blank = {.ch = ' ', .attr = 0};
    for (size_t i = 0; i < n; ++i) st->buf[i] = blank;
    return true;
}

const ConsoleCell* console_mem_buffer(const ConsoleDriver* dev) {
    const MemState* st = (const MemState*)dev->priv;
    return st->buf;
}
