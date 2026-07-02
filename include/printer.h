#ifndef PRINTER_H
#define PRINTER_H

/* Standard headers */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

/* Define input interpreter */
#ifdef _WIN32  // Windows compatible

#include <conio.h>

typedef int TerminalState;
static inline int portable_getch(void) { return getch(); }
static inline int portable_ungetch(int ch) { return ungetch(ch); }
static inline int portable_kbhit(void) { return kbhit(); }
static inline TerminalState enable_raw(void) { return 0; }
static inline void disable_raw(TerminalState ts) { (void)ts; }

#else  // defined input interpreter for WIN32

#include <termios.h>
#include <sys/select.h>

typedef struct termios TerminalState;  // POSIX compatible

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

#endif // defined input interpreter for POSIX

/* Color type */
typedef struct _color {
    int16_t r;
    int16_t g;
    int16_t b;
    bool set;
} Color;

/* Color constructor */
Color _new_color(Color defaults) {
    return (Color) {
        .r = defaults.r,
        .g = defaults.g,
        .b = defaults.b,
        .set = true
    };
}

/**
 * Define a new color using the RGB format.
 * @param .r (default: 0) Set RED channel
 * @param .g (default: 0) Set GREEN channel
 * @param .b (default: 0) Set BLUE channel
 * @return new Color object
 */
#define color(...) _new_color((Color){__VA_ARGS__})

/**
 * Define a new background color using the RGB format.
 * @param .r (default: 0) Set RED channel
 * @param .g (default: 0) Set GREEN channel
 * @param .b (default: 0) Set BLUE channel
 * @return new Color object
 */
#define background(...) _new_color((Color){__VA_ARGS__})

/* Default colors */
#define red _new_color((Color){255, 0, 0})
#define green _new_color((Color){0, 255, 0})
#define gray _new_color((Color){128, 128, 128})

/* Stroke type */
typedef enum _stroke {
    bold = (1 << 0),
    underline = (1 << 1),
    italic = (1 << 2)
} Stroke;

/* Move cursor to the end of the current line */
#define endline "\033[K"

/* Style type */
typedef struct _style_args {
    Color color;
    Stroke stroke;
    Color background;
} StyleArgs;

/* Style constructor */
StyleArgs _new_style_args(StyleArgs defaults) {
    StyleArgs sta = {0};

    sta.color = defaults.color;
    sta.background = defaults.background;
    sta.stroke = defaults.stroke;

    return sta;
}

/**
 * Define a new output style.
 * @param .color  (default: terminal theme) Set the foreground (text) color
 * @param .stroke (default: terminal theme) Set the foreground (text) stroke
 * @param .background (default: terminal theme) Set the background color
 * @return new StyleArgs object
 */
#define style(...) _new_style_args((StyleArgs){__VA_ARGS__})

/* Return to terminal theme */
#define RESET_STYLE "\033[0m"

/* Dynamic type */
typedef struct _dynamic_args {
    int32_t delay;
    bool cursor;
    int8_t *speedup;
    bool _enable_raw;
} DynamicArgs;

/* Dynamic constructor */
DynamicArgs _new_dynamic_args(DynamicArgs defaults) {
    DynamicArgs da = {0};

    da.delay = defaults.delay;
    da.cursor = defaults.cursor;
    da.speedup = defaults.speedup;
    da._enable_raw = true;

    return da;
}

/**
 * Define a new output behaviour.
 * @param .delay   (default: 0) Set the time between printing characters
 * @param .cursor  (default: show) Set cursor visibility
 * @param .speedup (default: none) Keys which decrease printing time
 * @return new DynamicArgs object
 */
#define dynamic(...) _new_dynamic_args((DynamicArgs){__VA_ARGS__})

/* Cursor visibility option */
#define hide true
/* Cursor visibility option */
#define show false

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
    StyleArgs style;
    int32_t repeat;
    DynamicArgs dynamic;
} Printer;

/* Helper - apply style */
static inline void _apply_style(FILE *to, Printer p) {
    if (p.style.stroke & bold) fprintf(to, "\033[1m");
    if (p.style.stroke & underline) fprintf(to, "\033[4m");
    if (p.style.stroke & italic) fprintf(to, "\033[3m");

    if (p.style.background.set)
        fprintf(to, "\x1b[48;2;%d;%d;%dm",
            p.style.background.r, p.style.background.g, p.style.background.b);
    if (p.style.color.set)
        fprintf(to, "\x1b[38;2;%d;%d;%dm",
            p.style.color.r, p.style.color.g, p.style.color.b);
}

/* Helper - add padding (should add delay) */
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

/* Helper - input interpreter */
static inline void _interpret_input(Printer p, bool *should_ff) {
    if (!p.dynamic.speedup) return;

    bool match = false;
    if (portable_kbhit()) {
        int key_pressed = portable_getch();
        size_t speed_len = strlen(p.dynamic.speedup);
        for (size_t idx = 0; idx < speed_len; idx++) {
            if (key_pressed == p.dynamic.speedup[idx]) {
                match = true;
                break;
            }
        }

        if (match) (*should_ff) = !(*should_ff);
        else portable_ungetch(key_pressed);
    }
}

/* Helper - correctly apply background for newlines */
static inline void _print_asc(FILE *to, int8_t *s, bool has_bg) {
    while (*s) {
        if (*s == '\n' && has_bg) fprintf(to, endline);
        fprintf(to, "%c", *s);
        s++;
    }
}

/* Helper - print text */
static inline void _add_char(FILE *to, Printer p) {
    size_t text_size = strlen(p.text);
    int32_t available_space = p.width - text_size;
    int32_t extra = 0;

    _add_pad(to, p.lpad, available_space, &extra);

    bool fast_f = false;
    int32_t ms = p.dynamic.delay;
    for (int32_t step = 0; step < p.repeat; step++) {
        int8_t size = 0;
        for (int8_t *s = p.text; *s; s++, size++) {
            if (!p.wrap && size == p.width) break;

            _interpret_input(p, &fast_f);

            if (*s == '\n') fprintf(to, endline);
            fprintf(to, "%c", *s);
            if (ms) fflush(to);

            (fast_f) ? usleep(ms / 5 * 1000) : usleep(ms * 1000);

            if (p.wrap && size == p.width - 1) _print_asc(to, "\n", p.style.background.set), size = -1;
        }
    }

    _add_pad(to, p.rpad, available_space, &extra);

    if (!extra) for (int32_t idx = 0; idx < available_space; idx++) fprintf(to, " ");
}

/* Printer constructor */
static inline Printer _makePrinter(struct _printer defaults) {
    Printer p = defaults;

    p.text = (p.text) ? p.text : (int8_t *)("");
    p.width = (p.width == 0) ? strlen(p.text) : p.width;
    p.lpad = (p.lpad) ? p.lpad : NULL;
    p.rpad = (p.rpad) ? p.rpad : NULL;
    p.start = (p.start) ? p.start : (int8_t *)("");
    p.end = (p.end) ? p.end : (int8_t *)("");
    p.out = (p.out) ? p.out : (int8_t *)("stdout");
    p.repeat = (p.repeat == 0) ? 1 : p.repeat;
    p.dynamic.speedup = (p.dynamic.speedup) ? p.dynamic.speedup : NULL;

    return p;
}

/* Helper - formatter */
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

/* GLobal cursor settings */
#define CURSOR_S "\033[?25h"
#define CURSOR_H "\033[?25l"

/* Helper - combine all printer options */
static inline void _use_printer(Printer p) {
    FILE *to = stdout;

    if (strcmp(p.out, "stdout")) to = fopen(p.out, "a");
    if (p.dynamic.cursor) fprintf(to, CURSOR_H);

    _apply_style(to, p);
    _print_asc(to, p.start, p.style.background.set);
    _add_char(to, p);
    _print_asc(to, p.end, p.style.background.set);
    fprintf(to, RESET_STYLE);
    fprintf(to, endline);
    if (p.dynamic.cursor) fprintf(to, CURSOR_S);

    if (strcmp(p.out, "stdout")) fclose(to);
}

/**
 * Prints text to a specified output stream.
 * @param .text Printable sequence of characters.
 * @param .width (default: sizeof(.text)) Number of characters being printed.
 * @param .wrap (default: false) Truncate text and move the rest to the next line
 * @param .lpad (default: NULL) Add padding to the left side of text (if able)
 * @param .rpad (default: NULL) Add padding to the right side of text (if able)
 * @param .start (default: "") Prefix (appended before text).
 * @param .end (default: "") Suffix (appended after text).
 * @param .out (default: stdout) Where text is printed.
 * @param .style (default: terminal specific) Specify styling options.
 * @param .dynamic (default: static) Specify terminal behaviour.
 * @param .repeat (default: 0) Repeats the text a number of times
 */
#define print(...) do { \
    Printer _p = _makePrinter((Printer){__VA_ARGS__}); \
    if (_p.dynamic._enable_raw) { \
        TerminalState _ts = enable_raw(); \
        _use_printer(_p); \
        disable_raw(_ts); \
    } else _use_printer(_p); \
} while (false)

#endif  // defined PRINTER_H