#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "console_vga_text.h"

/* VGA text mode memory base */
#define VGA_MEM_PHYS 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25

/* Backend state (could hold cursor HW state later) */
typedef struct {
    volatile uint16_t* vram; /* each cell: [attr(hi) | ch(lo)] */
    uint16_t cached_attr;    /* last translated attr */
} VgaTextState;

/* Attribute mapping:
 * abstract attr: 0x0RGB (foreground low nibble) | (background << 8, optional)
 * For simplicity, treat low byte: fore (0-15), high byte: back (0-15).
 */
static uint16_t vga_translate(ConsoleDriver* dev, uint16_t a);
static void vga_put_cell(ConsoleDriver* dev, int x, int y, ConsoleCell cell);
static void vga_put_run(ConsoleDriver* dev, int x, int y, const ConsoleCell* cells, size_t count);
static void vga_fill_rect(ConsoleDriver* dev, int x, int y, int w, int h, ConsoleCell cell);
static void vga_scroll_rect(ConsoleDriver* dev, int x, int y, int w, int h, int dy, ConsoleCell blank);
static void vga_set_cursor(ConsoleDriver* dev, int x, int y, bool visible);
static void vga_present(ConsoleDriver* dev);

static const ConsoleDriverOps VGA_OPS = {
    .put_cell = vga_put_cell,
    .put_run = vga_put_run,
    .fill_rect = vga_fill_rect,
    .scroll_rect = vga_scroll_rect,
    .set_cursor = vga_set_cursor,
    .present = vga_present,
    .translate_attr = vga_translate};

void console_vga_text_init(ConsoleDriver* dev) {
    static VgaTextState state; /* single instance for simplicity */
    state.vram = (volatile uint16_t*)(uintptr_t)VGA_MEM_PHYS;
    state.cached_attr = 0x0700; /* light gray on black */

    dev->ops = &VGA_OPS;
    dev->cols = VGA_COLS;
    dev->rows = VGA_ROWS;
    dev->priv = &state;
}

/* ---- ops impl ---- */

static inline size_t idx(int x, int y) { return (size_t)y * VGA_COLS + (size_t)x; }

static uint16_t vga_translate(ConsoleDriver* dev, uint16_t a) {
    /* low nibble = fg, high nibble (byte) = bg; map to VGA attribute (bg<<12 | fg<<8) within word. */
    uint8_t fg = (uint8_t)(a & 0x0F);
    uint8_t bg = (uint8_t)((a >> 8) & 0x0F);
    uint16_t vga_attr = (uint16_t)((bg << 12) | (fg << 8));
    (void)dev;
    return vga_attr;
}

static void vga_put_cell(ConsoleDriver* dev, int x, int y, ConsoleCell cell) {
    if ((unsigned)x >= (unsigned)dev->cols || (unsigned)y >= (unsigned)dev->rows) return;
    VgaTextState* st = (VgaTextState*)dev->priv;
    uint16_t attr = vga_translate(dev, cell.attr);
    st->vram[idx(x, y)] = (uint16_t)((attr & 0xFF00) | (cell.ch & 0x00FF));
}

static void vga_put_run(ConsoleDriver* dev, int x, int y, const ConsoleCell* cells, size_t count) {
    if ((unsigned)y >= (unsigned)dev->rows) return;
    if (x < 0) {
        size_t skip = (size_t)(-x);
        if (skip >= count) return;
        cells += skip;
        count -= skip;
        x = 0;
    }
    if (x + (int)count > dev->cols) count = (size_t)(dev->cols - x);
    VgaTextState* st = (VgaTextState*)dev->priv;
    volatile uint16_t* base = st->vram + idx(x, y);
    for (size_t i = 0; i < count; ++i) {
        uint16_t attr = vga_translate(dev, cells[i].attr);
        base[i] = (uint16_t)((attr & 0xFF00) | (cells[i].ch & 0x00FF));
    }
}

static void vga_fill_rect(ConsoleDriver* dev, int x, int y, int w, int h, ConsoleCell cell) {
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

    VgaTextState* st = (VgaTextState*)dev->priv;
    uint16_t attr = vga_translate(dev, cell.attr);
    uint16_t word = (uint16_t)((attr & 0xFF00) | (cell.ch & 0x00FF));
    for (int row = 0; row < h; ++row) {
        volatile uint16_t* line = st->vram + idx(x, y + row);
        for (int col = 0; col < w; ++col) {
            line[col] = word;
        }
    }
}

static void vga_scroll_rect(ConsoleDriver* dev, int x, int y, int w, int h, int dy, ConsoleCell blank) {
    /* dy < 0: scroll up; dy > 0: scroll down */
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

    VgaTextState* st = (VgaTextState*)dev->priv;
    uint16_t blank_attr = vga_translate(dev, blank.attr);
    uint16_t blank_word = (uint16_t)((blank_attr & 0xFF00) | (blank.ch & 0x00FF));

    if (dy < 0) {
        int shift = -dy;
        if (shift >= h) {
            /* Fill everything with blank */
            for (int row = 0; row < h; ++row) {
                volatile uint16_t* line = st->vram + idx(x, y + row);
                for (int col = 0; col < w; ++col) line[col] = blank_word;
            }
            return;
        }
        for (int row = 0; row < h - shift; ++row) {
            volatile uint16_t* dst = st->vram + idx(x, y + row);
            volatile uint16_t* src = st->vram + idx(x, y + row + shift);
            for (int col = 0; col < w; ++col) dst[col] = src[col];
        }
        /* Blank the freed rows at bottom */
        for (int row = h - shift; row < h; ++row) {
            volatile uint16_t* line = st->vram + idx(x, y + row);
            for (int col = 0; col < w; ++col) line[col] = blank_word;
        }
    } else {
        int shift = dy;
        if (shift >= h) {
            for (int row = 0; row < h; ++row) {
                volatile uint16_t* line = st->vram + idx(x, y + row);
                for (int col = 0; col < w; ++col) line[col] = blank_word;
            }
            return;
        }
        for (int row = h - 1; row >= shift; --row) {
            volatile uint16_t* dst = st->vram + idx(x, y + row);
            volatile uint16_t* src = st->vram + idx(x, y + row - shift);
            for (int col = 0; col < w; ++col) dst[col] = src[col];
        }
        for (int row = 0; row < shift; ++row) {
            volatile uint16_t* line = st->vram + idx(x, y + row);
            for (int col = 0; col < w; ++col) line[col] = blank_word;
        }
    }
    (void)dev;
}

static void vga_set_cursor(ConsoleDriver* dev, int x, int y, bool visible) {
    /* Optional: program the VGA cursor via CRT controller ports 0x3D4/0x3D5.
     * For now, do nothing (headless cursor). Safe placeholder. */
    (void)dev;
    (void)x;
    (void)y;
    (void)visible;
}

static void vga_present(ConsoleDriver* dev) {
    /* Immediate mode: nothing to flush. */
    (void)dev;
}
