#ifndef PRINTER_H
#define PRINTER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#ifdef _WIN32

#include <conio.h>

typedef int TerminalState;
static inline int portable_getch(void) { return getch(); }
static inline int portable_ungetch(int ch) { return ungetch(ch); }
static inline int portable_kbhit(void) { return kbhit(); }
static inline TerminalState enable_raw(void) { return 0; }
static inline void disable_raw(TerminalState ts) { (void)ts; }

#else

#include <termios.h>
#include <sys/select.h>

typedef struct termios TerminalState;

static inline TerminalState enable_raw(void) {
    struct termios old, raw;
    tcgetattr(STDIN_FILENO, &old);
    raw = old;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    return old;
}

static inline void disable_raw(TerminalState term) { tcsetattr(STDIN_FILENO, TCSANOW, &term); }

static inline int portable_getch(void) {
    TerminalState old = enable_raw();
    int ch = getchar();
    disable_raw(old);
    return ch;
}

static inline int portable_ungetch(int ch) { return ungetc(ch, stdin); }

static inline int portable_kbhit(void) {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

#endif

/* Printer definition */
typedef struct _printer {
    int8_t *text;
    int32_t width;
    bool wrap;
    int8_t *lpad;
    int8_t *rpad;
    int8_t *start;
    int8_t *end;
    int8_t *out;
    int8_t style;
    int32_t delay;
    int8_t *speedup;
    int32_t repeat;
    bool cursor;
} Printer;

/* Colors and stroke */
#define red (1 << 0)
#define green (1 << 1)
#define gray (1 << 2)
#define bold (1 << 3)

static inline void _apply_style(FILE *to, Printer p) {
    if (p.style & bold) fprintf(to, "\033[1m");

    if (p.style & red) fprintf(to, "\033[31m");
    else if (p.style & green) fprintf(to, "\033[32m");
    else if (p.style & gray) fprintf(to, "\033[90m");
}

/* add delay */
static inline void _add_pad(FILE *to, int8_t *side, int32_t space, int32_t *remaining) {
    if (!side) return;

    int32_t pad_size = strlen((char *)side);
    while (*remaining < space) {
        if (*remaining + pad_size <= space) {
            fprintf(to, "%s", side);
            (*remaining) += pad_size;
        } else {
            int32_t fill = space - (*remaining);
            for (int32_t idx = 0; idx < fill; idx++)
                fprintf(to, "%c", side[idx]);
            (*remaining) = space;
        }
    }
}

static inline void _interpret_input(Printer p, bool *should_ff) {
    if (!p.speedup) return;

    bool match = false;
    if (portable_kbhit()) {
        int key_pressed = portable_getch();
        size_t speed_len = strlen(p.speedup);
        for (size_t idx = 0; idx < speed_len; idx++) {
            if (key_pressed == p.speedup[idx]) {
                match = true;
                break;
            }
        }

        if (match) (*should_ff) = !(*should_ff);
        else portable_ungetch(key_pressed);
    }
}

static inline void _add_char(FILE *to, Printer p, int32_t ms) {
    size_t text_size = strlen(p.text);
    int32_t available_space = p.width - text_size;
    int32_t extra = 0;

    _add_pad(to, p.lpad, available_space, &extra);

    bool fast_f = false;
    for (int32_t step = 0; step < p.repeat; step++) {
        int8_t size = 0;
        for (int8_t *s = p.text; *s; s++, size++) {
            if (!p.wrap && size == p.width) break;

            _interpret_input(p, &fast_f);

            fprintf(to, "%c", *s);
            fflush(to);

            if (fast_f) usleep(ms / 5 * 1000);
            else usleep(ms * 1000);

            if (p.wrap && size == p.width - 1) fprintf(to, "\n"), size = -1;
        }
    }

    _add_pad(to, p.rpad, available_space, &extra);

    if (!extra) for (int32_t idx = 0; idx < available_space; idx++) fprintf(to, " ");
}

#define hide false
#define show true

static inline Printer _makePrinter(struct _printer defaults) {
    Printer p = defaults;

    p.text = (p.text) ? p.text : (int8_t *)("");
    p.width = (p.width == 0) ? strlen(p.text) : p.width;
    p.lpad = (p.lpad) ? p.lpad : NULL;
    p.rpad = (p.rpad) ? p.rpad : NULL;
    p.start = (p.start) ? p.start : (int8_t *)("");
    p.end = (p.end) ? p.end : (int8_t *)("");
    p.out = (p.out) ? p.out : (int8_t *)("stdout");
    p.style = (p.style) ? p.style : 0;
    p.repeat = (p.repeat == 0) ? 1 : p.repeat;
    p.speedup = (p.speedup) ? p.speedup : NULL;

    return p;
}

static inline int8_t *_fmt(int8_t *buff, size_t buff_size, int8_t *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buff, buff_size, fmt, args);
    va_end(args);
    return buff;
}

/**
 * Define a formatted string.
 * @param fmt Text to be printed just like in printf
 */
#define f(fmt, ...) _fmt((int8_t[1024]){0}, 1024, fmt, __VA_ARGS__)

#define RESET_STYLE "\033[0m"
#define CURSOR_S "\033[?25h"
#define CURSOR_H "\033[?25l"

static inline void _use_printer(Printer p) {
    FILE *to = stdout;

    if (strcmp(p.out, "stdout")) to = fopen(p.out, "a");
    if (!p.cursor) fprintf(to, CURSOR_H);

    _apply_style(to, p);
    fprintf(to, "%s", p.start);
    _add_char(to, p, p.delay);
    fprintf(to, "%s", p.end);
    fprintf(to, RESET_STYLE);
    fprintf(to, CURSOR_S);

    if (strcmp(p.out, "stdout")) fclose(to);
}

/**
 * Prints text to a specified output stream.
 * @param .text Printable sequence of characters.
 * @param .width (default: sizeof(.text)) Number of characters being printed.
 * @param .wrap (default: false) Truncate text and move the rest to a newline
 * @param .lpad (default: NULL) Left padding the left side of text (if there is space)
 * @param .rpad (default: NULL) Right padding the right side of text (if there is space)
 * @param .start (default: "") Prefix appended before text.
 * @param .end (default: "") Suffix appended after text.
 * @param .out (default: stdout) Where text is printed.
 * @param .style (default: terminal specific) Specify styling options (color and stroke)
 * @param .delay (default: 0) Time between each character print
 * @param .speedup (default: NULL) Provide keypresses which speed up text printing
 * @param .cursor (default: hide) Toggle the terminal cursor during execution. Succesful prints restore it.
 * @param .repeat (default: 0) Repeats the text a number of times
 */
#define print(...) do { \
    Printer _p = _makePrinter((Printer){__VA_ARGS__}); \
    TerminalState _ts = enable_raw(); \
    _use_printer(_p); \
    disable_raw(_ts); \
} while (false)

#endif