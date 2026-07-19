#ifndef PRINTER_H
#define PRINTER_H

/* Standard headers */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <stdatomic.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <pthread.h>
#endif

/* Define input interpreter */
#ifdef _WIN32  // Windows compatible

#include <conio.h>

typedef int32_t TerminalState;
static inline int portable_getch(void) { return getch(); }
static inline int portable_ungetch(int32_t ch) { return ungetch(ch); }
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

/* TerminalCursor definition */
typedef struct _terminal_cursor {
    size_t x;
    size_t y;
} TerminalCursor;

/* TerminalDimensiona definition */
typedef struct _terminal_dimensions {
    size_t rows;
    size_t cols;
} TerminalDimensions;

/* Terminal definition */
typedef struct _terminal {
    TerminalDimensions dimensions;
    TerminalState state;
    TerminalCursor cursor;
    bool buffered;
} Terminal;

/* Helper - get current terminal size */
static inline TerminalDimensions getTerminalDimensions(void) {
    TerminalDimensions td = {0};

    #ifdef _WIN32

    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    td.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    td.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    #else

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    td.rows = w.ws_row;
    td.cols = w.ws_col;

    #endif

    return td;
}

/* Terminal constructor */
static inline Terminal initTerminal(void) {
    Terminal t = {0};

    t.dimensions = getTerminalDimensions();
    t.cursor = (TerminalCursor){1, 1};
    t.buffered = true;

    return t;
}

/* Dynamic terminal API */
static inline void terminalEnableRaw(Terminal *t) { t->state = enable_raw(); }
static inline void terminalDisableRaw(Terminal *t) { disable_raw(t->state); }
static inline void terminalDisableBuffer(Terminal *t) {
    if (!t->buffered) return;

    setvbuf(stdout, NULL, _IONBF, 0);
    t->buffered = false;
}
static inline void terminalEnableBuffer(Terminal *t) {
    if (t->buffered) return;

    setvbuf(stdout, NULL, _IOFBF, BUFSIZ);
    t->buffered = true;
}

/* RowRelativeMovement definition */
typedef enum _row_relative_movement {
    up = 1,
    down
} RowRelativeMovement;

/* Row relative movement */
static inline void move_row(RowRelativeMovement rrm, size_t row) {
    switch (rrm) {
        case up:
            printf("\x1b[%ldA", row);
            break;
        case down:
            printf("\x1b[%ldB", row);
            break;
        default: break;
    }
}

/* ColRelativeMovement definition */
typedef enum _col_relative_movement {
    left = 0,
    right,
    center
} ColRelativeMovement;

/* Col relative movement */
static inline void move_col(ColRelativeMovement crm, size_t col) {
    switch (crm) {
        case left:
            printf("\x1b[%ldD", col);
            break;
        case right:
            printf("\x1b[%ldC", col);
            break;
        default: break;
    }
}

/* Color type */
typedef struct _color {
    int16_t r;
    int16_t g;
    int16_t b;
    bool set;
} Color;

/* Color constructor */
static inline Color _new_color(Color defaults) {
    return (Color) {
        .r = defaults.r,
        .g = defaults.g,
        .b = defaults.b,
        .set = true
    };
}

/* Print debugging info for colors */
void debugColor(Color c) { fprintf(stderr, "Color({%d, %d, %d}", c.r, c.g, c.b);}

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
#define red (Color){255, 0, 0, true}
#define green (Color){0, 255, 0, true}
#define gray (Color){128, 128, 128, true}

/* Stroke type */
typedef enum _stroke {
    bold = (1 << 0),
    underline = (1 << 1),
    italic = (1 << 2)
} Stroke;

/* Move cursor to the end of the current line */
#define endline "\033[K"
/* Move cursor to the next line */
#define newline "\n"
/* Clear the active screen and move the cursor to (1, 1) */
#define clrscrn .text = "\033[2J\033[H", .style = style(.clear = bleed)

/* Style type */
typedef struct _style_args {
    Color color;
    Stroke stroke;
    Color background;
    int8_t clear;
} StyleArgs;

/* Style constructor */
static inline StyleArgs _new_style_args(StyleArgs defaults) {
    StyleArgs sta = {0};

    sta.color = defaults.color;
    sta.background = defaults.background;
    sta.stroke = defaults.stroke;
    sta.clear = defaults.clear;

    return sta;
}

#define bleed -1

/**
 * Define a new output style.
 * @param .color      (default: terminal theme) Set the foreground (text) color.
 * @param .stroke     (default: terminal theme) Set the foreground (text) stroke.
 * @param .background (default: terminal theme) Set the background color.
 * @param .clear      (default: restore terminal theme) Specify if style bleeds.
 * @return new StyleArgs object
 */
#define style(...) _new_style_args((StyleArgs){__VA_ARGS__})

/* Return to terminal theme */
#define RESET_STYLE "\033[0m"
#define RESET_COLOR "\x1b[39m"
#define RESET_BACKGROUND "\x1b[49m"
#define RESET_BOLD "\x1b[22m"
#define RESET_ITALIC "\x1b[23m"
#define RESET_UNDERLINE "\x1b[24m"

/* Create a color hashmap for inline styling */
typedef struct _color_entry {
    int8_t *id;
    Color color;
} ColorEntry;

#define BUCKET_SIZE 8

/* Map definition */
typedef struct _color_bucket {
    ColorEntry colors[BUCKET_SIZE];
    size_t size;
} ColorBucket;

/* Helper - add a new color to the bucket */
void _insertColor(ColorBucket *b, int8_t *id, Color c) {
    if (b->size >= BUCKET_SIZE) return;

    b->colors[b->size].id = id;
    b->colors[b->size++].color = c;
}

/* Print debugging information for a color entry */
void debugColorEntry(int8_t *id, Color c) { fprintf(stderr, "{\"%s\" : Color({%d, %d, %d})}", id, c.r, c.g, c.b); }

void _debugBucket(ColorBucket b) {
    fprintf(stderr, "Bucket(%ld <= %d) {\n\t", b.size, BUCKET_SIZE);
    for (size_t idx = 0; idx < b.size; idx++) {
        debugColorEntry(b.colors[idx].id, b.colors[idx].color);
        printf(" -> ");
    }
    printf("nil\n");
    printf("}\n");
}

/* Maximum number of buckets */
#define DICT_SIZE 24

/* Color dictionary */
typedef struct _color_dict {
    ColorBucket buckets[DICT_SIZE];

    size_t (*hash)(int8_t *);
} ColorDict;

/* Standard string hash */
size_t djb2(int8_t *key) {
    size_t seed = 5381;
    for (int8_t *s = key; *s; s++) seed = seed * 33 + *s;
    return seed % DICT_SIZE;
}

/* Color map declaration */
ColorDict cd = {
    .hash = djb2,
    .buckets = {
        [14] = {
            .size = 1,
            .colors = {
                [0] = {.id = "green", .color = green},
            }
        },
        [16] = {
            .size = 2,
            .colors = {
                [0] = {.id = "red", .color = red},
                [1] = {.id = "gray", .color = gray},
            }
        },
    }
};

/* Add new colors to the map */
void _putColor(ColorDict *cd, int8_t *id, Color color) {
    size_t idx = cd->hash(id);
    _insertColor(&cd->buckets[idx], id, color);
}

/* Get a color from the map */
Color _getColor(ColorDict *cd, int8_t *id) {
    size_t src_idx = cd->hash(id);
    ColorBucket src = cd->buckets[src_idx];

    for (size_t idx = 0; idx < src.size; idx++) {
        if (!strcmp(src.colors[idx].id, id)) {
            return src.colors[idx].color;
        }
    }

    return color(0, 0, 0);  // will return a color
}

/* Print map debugging information */
void debugDict(ColorDict cd) {
    for (size_t idx = 0; idx < DICT_SIZE; idx++) {
        printf("[%ld]: ", idx);
        ColorBucket bucket = cd.buckets[idx];
        if (!bucket.size) {
            printf("nil\n");
            continue;
        }
        _debugBucket(bucket);
    }
}

/**
 * @brief Register a new color for the inline interpreter.
 * @param name String identifier used by the interpreter.
 * @param clr A Color Object.
 * @returns none - Adds {name, clr} into cd.
 */
#define registerColor(name, clr) _putColor(&cd, (name), (clr))

/* Dynamic type */
typedef struct _dynamic_args {
    int32_t delay;
    bool cursor;
    int8_t *speedup;
    bool raw;
} DynamicArgs;

/* Dynamic constructor */
static inline DynamicArgs _new_dynamic_args(DynamicArgs defaults) {
    DynamicArgs da = {0};

    da.delay = defaults.delay;
    da.cursor = defaults.cursor;
    da.speedup = defaults.speedup;
    da.raw = defaults.raw;

    return da;
}

/**
 * Define a new output behaviour.
 * @param .delay   (default: 0) Set the time between printing characters.
 * @param .cursor  (default: show) Set cursor visibility.
 * @param .speedup (default: none) Keys which decrease printing time.
 * @param .raw     (default: false) Force terminal into raw mode.
 * @return new DynamicArgs object
 */
#define dynamic(...) _new_dynamic_args((DynamicArgs){__VA_ARGS__})

/* Cursor visibility option */
#define hide true
/* Cursor visibility option */
#define show false

typedef enum _wrap_option {
    force = 1,
    terminal,
    word
} WrapOption;

typedef struct _layout {
    ColRelativeMovement align;
    int32_t width;
    WrapOption wrap;
} LayoutArgs;

LayoutArgs _new_layout_args(LayoutArgs defaults) {
    LayoutArgs la = {0};

    la.align = (defaults.align) ? defaults.align : left;
    la.width = defaults.width;
    la.wrap = defaults.wrap;

    return la;
}

#define layout(...) _new_layout_args((LayoutArgs){__VA_ARGS__})

/* Printer definition */
typedef struct _printer {
    int8_t *text;
    int8_t *end;
    StyleArgs style;
    int8_t *out;
    LayoutArgs layout;
    int8_t *lpad;
    int8_t *rpad;
    int8_t *start;
    DynamicArgs dynamic;
    int32_t repeat;
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

/* Helper - move cursor into position */
static inline void _add_alignment(FILE *to, Printer p) {
    if (p.layout.align == left) return;

    TerminalDimensions td = getTerminalDimensions();

    size_t slen = strlen(p.start);
    size_t sspace = 0;
    for (size_t idx = 0; idx < slen; idx++) sspace = (isprint(p.start[idx]) != 0) ? sspace + 1 : sspace;

    size_t elen = strlen(p.end);
    size_t espace = 0;
    for (size_t idx = 0; idx < elen; idx++) espace = (isprint(p.end[idx]) != 0) ? espace + 1 : espace;

    size_t tlen = strlen(p.text);
    int32_t base_width = (!p.layout.width) ? tlen : p.layout.width;
    int32_t new_col = td.cols - ((int32_t)(base_width + sspace + espace));
    switch (p.layout.align) {
        case right:
            fprintf(to, "\r");
            move_col(right, (new_col));
            break;
        case center:
            fprintf(to, "\r");
            move_col(right, (new_col) / 2);
            break;
        default: break;
    }
}

/* Make typewriter printing thread safe - inputs do not override themselves */
typedef struct _dynamic_thread {
    #ifdef _WIN32
        HANDLE body;
    #else
        pthread_t body;
    #endif

    atomic_bool shouldListen;
    atomic_int_fast32_t lastKey;

    void *(*listen)(void *);
    void (*store)(struct _dynamic_thread *);
    int32_t (*load)(struct _dynamic_thread *);
    int32_t (*consume)(struct _dynamic_thread *);
    bool (*peek)(struct _dynamic_thread *);
} InputThread;

/* Atomic store */
static void _input_store(InputThread *t) { atomic_store(&(t->lastKey), portable_getch()); }

/* Atomic load */
static int32_t _input_load(InputThread *t) { return atomic_load(&(t->lastKey)); }

/* Atomic exchange */
static int32_t _input_consume(InputThread *t) { return atomic_exchange(&(t->lastKey), -1); }

/* Check latest input */
static bool _input_peek(InputThread *t) { return atomic_load(&(t->lastKey)) != -1; }

/* One input thread */
InputThread _input_thread = {
    .shouldListen = false,
    .lastKey = -1,
    .store = _input_store,
    .load = _input_load,
    .consume = _input_consume,
    .peek = _input_peek
};

/* Await input */
static void *_input_listen(void *) {
    while (atomic_load(&(_input_thread.shouldListen))) {
        if (!portable_kbhit()) {
            usleep(1000);  // yield
            continue;
        }
        _input_thread.store(&_input_thread);
    }
    return NULL;
}

/* Create platform-specific methods */
#ifdef _WIN32
    static inline void startInputListener(void) {
        if (atomic_load(&(_input_thread.shouldListen)) == true) return;

        atomic_store(&(_input_thread.shouldListen), true);
        _input_thread.body = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)_input_listen, NULL, 0, NULL);
    }
    static inline void stopInputListener(void) {
        if (atomic_load(&(_input_thread.shouldListen)) == false) return;

        atomic_store(&(_input_thread.shouldListen), false);
        WaitForSingleObject(_input_thread.body, INFINITE);
        CloseHandle(_input_thread.body);
    }
#else  // POSIX
    static inline void startInputListener(void) {
        if (atomic_load(&(_input_thread.shouldListen)) == true) return;

        atomic_store(&(_input_thread.shouldListen), true);
        pthread_create(&(_input_thread.body), NULL, _input_listen, NULL);
    }
    static inline void stopInputListener(void) {
        if (atomic_load(&(_input_thread.shouldListen)) == false) return;

        atomic_store(&(_input_thread.shouldListen), false);
        pthread_join((_input_thread.body), NULL);
    }
#endif  // defined InputThread ops.

/* Helper - ommit thread.self */
#define _use_thread(t, func) t.func(&t)

/** 
 *  Helper - input interpreter 
 * Could be changed in 'main' to account for different events
*/
static inline void _interpret_input(Printer p, bool *should_ff) {
    if (!p.dynamic.speedup || !_use_thread(_input_thread, peek)) return;

    int32_t key = _use_thread(_input_thread, load);

    size_t slen = strlen(p.dynamic.speedup);
    for (size_t idx = 0; idx < slen; idx++) {
        if (key == p.dynamic.speedup[idx]) {
            _use_thread(_input_thread, consume);
            *should_ff = !(*should_ff);
            return;
        }
    }
}

/* Helper - add padding */
static inline void _add_pad(FILE *to, int8_t *side, int32_t space,
    int32_t *remaining, Printer p, bool *fast_f) {
    if (!side) return;

    int32_t pad_size = strlen((char *)side);
    int32_t ms = p.dynamic.delay;

    while (*remaining < space) {
        if (*remaining + pad_size <= space) {
            for (int8_t *s = side; *s; s++) {
                _interpret_input(p, fast_f);

                if (*s == '\n') fprintf(to, endline);
                fprintf(to, "%c", *s);
                if (*s == '\n') _add_alignment(to, p);

                if (ms) {
                    fflush(to);
                    (*fast_f) ? usleep(ms / 5 * 1000) : usleep(ms * 1000);
                }
            }
            (*remaining) += pad_size;
        } else {
            int32_t fill = space - (*remaining);
            for (int32_t idx = 0; idx < fill; idx++) {
                _interpret_input(p, fast_f);

                if (side[idx] == '\n') fprintf(to, endline);
                fprintf(to, "%c", side[idx]);
                if (side[idx] == '\n') _add_alignment(to, p);

                if (ms) {
                    fflush(to);
                    (*fast_f) ? usleep(ms / 5 * 1000) : usleep(ms * 1000);
                }
            }
            (*remaining) = space;
        }
    }
}

/* Helper - correctly apply background for newlines */
static inline void _print_asc(FILE *to, int8_t *s, Printer p) {
    bool tty = isatty(fileno(to));
    size_t slen = strlen(s);
    bool check = (slen != 1);

    for (size_t idx = 0; idx < slen; idx++) {
        if (s[idx] == '\n' && tty && p.style.background.set && p.layout.align != right) fprintf(to, endline);

        fprintf(to, "%c", s[idx]);

        if (s[idx] == '\n' && check && idx != slen - 1) _add_alignment(to, p);
    }
}

/* Helper - compute a words length based on printable characters */
size_t _find_word_length(int8_t *word, Printer p) {
    size_t wlen = 0;

    if ((word == p.text || *(word - 1) == ' ') &&
        *word != ' ' && *word != '\n' && *word != '\t' && *word != '\r') {

        int8_t *iter = word;
        while (*iter && *iter != ' ' && *iter != '\n' && *iter != '\t' && *iter != '\r') {
            wlen++;
            iter++;
        }
    }

    return wlen;
}

/* Inline Interpreter Object*/
typedef struct _inline_style {
    size_t start_idx;
    StyleArgs style;
    DynamicArgs dynamic;
} InlineStyle;

/* Maximum number of custom inline blocks */
#define INLINE_RUN 64

void _interpret_style_inline(FILE *to, StyleArgs sta) {
    fprintf(to, (sta.stroke & bold) ? "\033[1m" : RESET_BOLD);
    fprintf(to, (sta.stroke & underline) ? "\033[4m" : RESET_UNDERLINE);
    fprintf(to, (sta.stroke & italic) ? "\033[3m" : RESET_ITALIC);

    if (sta.background.set)
        fprintf(to, "\x1b[48;2;%d;%d;%dm", sta.background.r, sta.background.g, sta.background.b);
    else
        fprintf(to, RESET_BACKGROUND);
    if (sta.color.set)
        fprintf(to, "\x1b[38;2;%d;%d;%dm", sta.color.r, sta.color.g, sta.color.b);
    else
        fprintf(to, RESET_COLOR);
}

/* Helper - print text */
static inline void _add_char(FILE *to, Printer p, InlineStyle *runs, size_t ridx) {
    size_t text_size = strlen(p.text);

    bool fast_f = false;
    int32_t ms = p.dynamic.delay;
    bool tty = isatty(fileno(to));

    TerminalDimensions td = getTerminalDimensions();
    WrapOption wo = p.layout.wrap;
    int32_t base_width = (!p.layout.width) ? text_size : p.layout.width;
    int32_t computed_width = (wo == terminal) ? td.cols : base_width;

    startInputListener();
    for (int32_t step = 0; step < p.repeat; step++) {
        _add_alignment(to, p);
        _print_asc(to, p.start, p);

        int32_t available_space = base_width - text_size;
        int32_t extra = 0;

        _add_pad(to, p.lpad, available_space, &extra, p, &fast_f);
        int32_t size = 0;
        size_t rcount = -1;
        for (int8_t *s = p.text; *s; s++, size++) {
            if (!p.layout.wrap && size == base_width) break;

            /* Apply style and dynamic ops. inline */
            if (runs) {
                size_t pos = (size_t)(s - p.text);
                size_t curr_run = 0;
                for (size_t jdx = 0; jdx < ridx; jdx++) {
                    if (runs[jdx].start_idx <= pos) curr_run = jdx;
                    else break;
                }
                if (curr_run != rcount) {
                    rcount = curr_run;
                    ms = runs[curr_run].dynamic.delay;
                    _interpret_style_inline(to, runs[curr_run].style);
                }
            }

            _interpret_input(p, &fast_f);

            if ((wo == word || wo == terminal)) {
                int8_t *src = (*s == ' ') ? s + 1 : s;
                size_t wlen = _find_word_length(src, p);
                int32_t rst = (*s == ' ') ? 1 : 0;

                if (size && wlen + size + rst > computed_width) {
                    _print_asc(to, "\n", p);
                    _add_alignment(to, p);
                    size = -1;

                    if (*s == ' ') continue;
                }
            }

            if (*s == '\n' && tty && p.style.background.set && p.layout.align != right) fprintf(to, endline);
            fprintf(to, "%c", *s);
            if (ms) {
                fflush(to);
                (fast_f) ? usleep(ms / 5 * 1000) : usleep(ms * 1000);
            }

            if (p.layout.wrap == force && size == computed_width - 1) {
                _print_asc(to, "\n", p);
                _add_alignment(to, p);
                size = -1;
            }
        }
        _add_pad(to, p.rpad, available_space, &extra, p, &fast_f);

        if (!extra) for (int32_t idx = 0; idx < available_space; idx++) fprintf(to, " ");

        _print_asc(to, p.end, p);
    }
    stopInputListener();
}

/* Printer constructor */
static inline Printer _makePrinter(struct _printer defaults) {
    Printer p = defaults;

    p.text = (p.text) ? p.text : (int8_t *)("");
    p.lpad = (p.lpad) ? p.lpad : NULL;
    p.rpad = (p.rpad) ? p.rpad : NULL;
    p.start = (p.start) ? p.start : (int8_t *)("");
    p.end = (p.end) ? p.end : (int8_t *)("");
    p.out = (p.out) ? p.out : (int8_t *)("stdout");
    p.repeat = (p.repeat == 0) ? 1 : p.repeat;
    p.dynamic.speedup = (p.dynamic.speedup) ? p.dynamic.speedup : NULL;
    p.layout = defaults.layout;

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

/* Inline interpreter - strip object arguments and send clear text */
/* LOOOOONG FUNCTION - cannot create helpers because of VLA's */
/* Dont want to allocate memory each time- will look into it */
void _add_char_inline(FILE *to, Printer p) {
    int8_t *text = p.text;
    size_t tlen = strlen(text);

    int8_t style_op[5] = {0};

    int8_t style_args[5][64];
    size_t args_count = 0;
    size_t args_len = 0;

    StyleArgs style = p.style;
    DynamicArgs dynamic = p.dynamic;

    bool use_ctx = false;

    int8_t buffer[2048] = {0};
    size_t bidx = 0;

    InlineStyle runs[INLINE_RUN];
    size_t ridx = 0;

    runs[ridx++] = (InlineStyle){.start_idx = 0, .style = style, .dynamic = dynamic};

    for (size_t idx = 0; idx < tlen; idx++) {
        if (text[idx] != '[') {
            buffer[bidx++] = text[idx];
            continue;
        }

        if (idx + 1 < tlen && text[idx + 1] == '[') {
            buffer[bidx++] = '[';
            continue;
        }

        size_t ctx_start = idx++;

        args_count = 0;
        args_len = 0;
        style_args[0][0] = '\0';
        use_ctx = false;

        while (idx < tlen && text[idx] != ']') {
            while (idx < tlen && text[idx] == ' ') idx++;
            if (idx >= tlen || text[idx] == ']') break;

            style_op[args_count] = text[idx++];

            while (idx < tlen && text[idx] == ' ') idx++;
            if (idx < tlen && text[idx] == ':') idx++;
            while (idx < tlen && text[idx] == ' ') idx++;

            args_len = 0;

            while (idx < tlen && text[idx] != ',' && text[idx] != ']') {
                if (text[idx] != ' ') {
                    style_args[args_count][args_len++] = text[idx];
                }
                idx++;
            }

            style_args[args_count][args_len] = '\0';

            if (style_op[args_count] == '/') {
                style = p.style;
                dynamic = p.dynamic;
                use_ctx = true;
            } else if (style_args[args_count][0] == '/') {
                switch (style_op[args_count]) {
                    case 'c': {
                        style.color = p.style.color;
                        use_ctx = true;
                        break;
                    }
                    case 'b': {
                        style.background = p.style.background;
                        use_ctx = true;
                        break;
                    }
                    case 's': {
                        style.stroke = p.style.stroke;
                        use_ctx = true;
                        break;
                    }
                    case 'd': {
                        dynamic = p.dynamic;
                        use_ctx = true;
                        break;
                    }
                    default: break;
                }
            } else {
                switch (style_op[args_count]) {
                    case 'c': {
                        Color src = _getColor(&cd, style_args[args_count]);
                        style.color = src;
                        use_ctx = true;
                        break;
                    }
                    case 'b': {
                        Color src = _getColor(&cd, style_args[args_count]);
                        style.background = src;
                        use_ctx = true;
                        break;
                    }
                    case 's': {
                        if (!strcmp(style_args[args_count], "bold"))
                            {style.stroke |= bold; use_ctx = true;}
                        if (!strcmp(style_args[args_count], "underline"))
                            {style.stroke |= underline; use_ctx = true;}
                        if (!strcmp(style_args[args_count], "italic"))
                            {style.stroke |= italic; use_ctx = true;}
                        break;
                    }
                    case 'd':
                        dynamic.delay = atoi(style_args[args_count]);
                        use_ctx = true;
                        break;
                    default: break;
                }
            }

            args_count++;
            if (idx < tlen && text[idx] == ',') idx++;
        }

        if (idx < tlen && text[idx] == ']') {
            if (!use_ctx) {
                for (size_t jdx = ctx_start; jdx <= idx; jdx++) {
                    buffer[bidx++] = text[jdx];
                }
            } else if (ridx < INLINE_RUN) {
                runs[ridx++] = (InlineStyle){.start_idx = bidx, .style = style, .dynamic = dynamic};
            }
        } else {
            for (size_t jdx = ctx_start; jdx <= idx; jdx++) {
                    buffer[bidx++] = text[jdx];
            }
        }
    }

    buffer[bidx] = '\0';

    Printer _tmp = p;
    _tmp.text = buffer;
    _apply_style(to, _tmp);
    _add_char(to, _tmp, runs, ridx);
}

/* Helper - combine all printer options */
static inline void _use_printer(Printer p) {
    FILE *to = stdout;

    if (strcmp(p.out, "stdout")) to = fopen(p.out, "a");
    if (p.dynamic.cursor) fprintf(to, CURSOR_H);

    if (strchr(p.text, '[')) {
        _add_char_inline(to, p);
    } else {
        _apply_style(to, p);
        _add_char(to, p, NULL, 0);
    }

    if (p.style.clear != bleed && isatty(fileno(to))) {
        fprintf(to, RESET_STYLE);
        fprintf(to, endline);
    }

    if (p.dynamic.cursor) fprintf(to, CURSOR_S);
    if (strcmp(p.out, "stdout")) fclose(to);
}

/**
 * Prints text to a specified output stream. Adds a newline at the end of text.
 * @param .text Printable sequence of characters.
 * @param .end (default: "") Suffix (appended after text).
 * @param .style (default: terminal specific) Specify styling options.
 * @param .out (default: stdout) Where text is printed.
 * @param .layout (default: unwrapped, aligned left) Specify layout options.
 * @param .lpad (default: NULL) Add padding to the left side of text (if able)
 * @param .rpad (default: NULL) Add padding to the right side of text (if able)
 * @param .start (default: "") Prefix (appended before text).
 * @param .dynamic (default: static) Specify terminal behaviour.
 * @param .repeat (default: 0) Repeats the text a number of times
 */
#define println(...) do { \
    Printer _p = _makePrinter((Printer){__VA_ARGS__}); \
    int8_t _ebuf[strlen(_p.end) + 2]; \
    strncpy(_ebuf, _p.end, strlen(_p.end)); \
    strcat(_ebuf, "\n\0"); \
    _p.end = _ebuf; \
    if (_p.dynamic.raw) { \
        TerminalState _ts = enable_raw(); \
        _use_printer(_p); \
        disable_raw(_ts); \
    } else _use_printer(_p); \
} while (false)

/**
 * Prints text to a specified output stream.
 * @param .text Printable sequence of characters.
 * @param .end (default: "") Suffix (appended after text).
 * @param .style (default: terminal specific) Specify styling options.
 * @param .out (default: stdout) Where text is printed.
 * @param .layout (default: unwrapped, aligned left) Specify layout options.
 * @param .lpad (default: NULL) Add padding to the left side of text (if able)
 * @param .rpad (default: NULL) Add padding to the right side of text (if able)
 * @param .start (default: "") Prefix (appended before text).
 * @param .dynamic (default: static) Specify terminal behaviour.
 * @param .repeat (default: 0) Repeats the text a number of times
 */
#define print(...) do { \
    Printer _p = _makePrinter((Printer){__VA_ARGS__}); \
    if (_p.dynamic.raw) { \
        TerminalState _ts = enable_raw(); \
        _use_printer(_p); \
        disable_raw(_ts); \
    } else _use_printer(_p); \
} while (false)

#endif  // defined PRINTER_H
