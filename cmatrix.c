/*
    cmatrix.c

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok

    This file is part of cmatrix.

    cmatrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cmatrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar. If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <locale.h>

#ifndef EXCLUDE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#elif defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

/* Matrix typedef */
typedef struct cmatrix {
    int val;
    bool is_head;
} cmatrix;

/* Global variables */
int console = 0;
int xwindow = 0;
int lock = 0;
int hebrew = 0;
int reality = 0;
cmatrix **matrix = (cmatrix **) NULL;

/* -H: the 22 Hebrew letters alef..tav (U+05D0..U+05EA without final forms). Cell values use a
 * private range so they never collide with the special values (-1, 0, 1, ' ') kept in the matrix;
 * they are mapped to the letters only when drawn. */
#define HEBREW_BASE 0xE000
#define HEBREW_COUNT 22
static const char *hebrew_letters[HEBREW_COUNT] = {
    "א", "ב", "ג", "ד", "ה", "ו", "ז", "ח",
    "ט", "י", "כ", "ל", "מ", "נ", "ס", "ע",
    "פ", "צ", "ק", "ר", "ש", "ת",
};

/* Draws one cell value: a Hebrew letter as UTF-8 (addch cannot output it), anything else as before. */
static void addcell(int val) {
    if (hebrew && val >= HEBREW_BASE && val < HEBREW_BASE + HEBREW_COUNT) {
        addstr(hebrew_letters[val - HEBREW_BASE]);
    } else {
        addch(val);
    }
}
int *length = NULL;  /* Length of cols in each line */
int *spaces = NULL;  /* Spaces left to fill */
int *updates = NULL; /* What does this do again? */
volatile sig_atomic_t signal_status = 0; /* Indicates a caught signal */

int va_system(char *str, ...) {

    va_list ap;
    char buf[133];

    va_start(ap, str);
    vsnprintf(buf, sizeof(buf), str, ap);
    va_end(ap);
    return system(buf);
}

/* What we do when we're all set to exit */
void finish(void) {
    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();
    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }
    exit(0);
}

/* What we do when we're all set to exit */
void c_die(char *msg, ...) {

    va_list ap;

    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();

    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    exit(0);
}

void usage(void) {
    printf(" Usage: cmatrix -[abBcfhHlsmRVx] [-u delay] [-C color]\n");
    printf(" -a: Asynchronous scroll\n");
    printf(" -b: Bold characters on\n");
    printf(" -B: All bold characters (overrides -b)\n");
    printf(" -c: Use Japanese characters as seen in the original matrix. Requires appropriate fonts\n");
    printf(" -f: Force the linux $TERM type to be on\n");
    printf(" -H: Use the 22 Hebrew letters (alef..tav, no final forms). Requires appropriate fonts\n");
    printf(" -R: codeofreality.org look: full-height strips in Nord orange, glyphs U+E100..U+E11D (or -H)\n");
    printf(" -l: Linux mode (uses matrix console font)\n");
    printf(" -L: Lock mode (can be closed from another terminal)\n");
    printf(" -o: Use old-style scrolling\n");
    printf(" -h: Print usage and exit\n");
    printf(" -n: No bold characters (overrides -b and -B, default)\n");
    printf(" -s: \"Screensaver\" mode, exits on first keystroke\n");
    printf(" -x: X window mode, use if your xterm is using mtx.pcf\n");
    printf(" -V: Print version information and exit\n");
    printf(" -u delay (0 - 10, default 4): Screen update delay\n");
    printf(" -C [color]: Use this color for matrix (default green)\n");
    printf(" -r: rainbow mode\n");
    printf(" -m: lambda mode\n");
}

void version(void) {
    printf(" CMatrix version %s (compiled %s, %s)\n",
        VERSION, __TIME__, __DATE__);
    printf("Email: abishekvashok@gmail.com\n");
    printf("Web: https://github.com/abishekvashok/cmatrix\n");
}


/* nmalloc from nano by Big Gaute */
void *nmalloc(size_t howmuch) {
    void *r;

    if (!(r = malloc(howmuch))) {
        c_die("CMatrix: malloc: out of memory!");
    }

    return r;
}

/* Initialize the global variables */
void var_init() {
    int i, j;

    if (matrix != NULL) {
        free(matrix[0]);
        free(matrix);
    }

    matrix = nmalloc(sizeof(cmatrix *) * (LINES + 1));
    matrix[0] = nmalloc(sizeof(cmatrix) * (LINES + 1) * COLS);
    for (i = 1; i <= LINES; i++) {
        matrix[i] = matrix[i - 1] + COLS;
    }

    if (length != NULL) {
        free(length);
    }
    length = nmalloc(COLS * sizeof(int));

    if (spaces != NULL) {
        free(spaces);
    }
    spaces = nmalloc(COLS* sizeof(int));

    if (updates != NULL) {
        free(updates);
    }
    updates = nmalloc(COLS * sizeof(int));

    /* Make the matrix */
    for (i = 0; i <= LINES; i++) {
        for (j = 0; j <= COLS - 1; j += 2) {
            matrix[i][j].val = -1;
        }
    }

    for (j = 0; j <= COLS - 1; j += 2) {
        /* Set up spaces[] array of how many spaces to skip */
        spaces[j] = (int) rand() % LINES + 1;

        /* And length of the stream */
        length[j] = (int) rand() % (LINES - 3) + 3;

        /* Sentinel value for creation of new objects */
        matrix[1][j].val = ' ';

        /* And set updates[] array for update speed. */
        updates[j] = (int) rand() % 3 + 1;
    }

}

void sighandler(int s) {
    signal_status = s;
}

void resize_screen(void) {
    char *tty;
    int fd = 0;
    int result = 0;
    struct winsize win;

    tty = ttyname(0);
    if (!tty) {
        return;
    }
    fd = open(tty, O_RDWR);
    if (fd == -1) {
        return;
    }
    result = ioctl(fd, TIOCGWINSZ, &win);
    if (result == -1) {
        return;
    }

    COLS = win.ws_col;
    LINES = win.ws_row;

    if(LINES <10){
        LINES = 10;
    }
    if(COLS <10){
        COLS = 10;
    }

#ifdef HAVE_RESIZETERM
    resizeterm(LINES, COLS);
#ifdef HAVE_WRESIZE
    if (wresize(stdscr, LINES, COLS) == ERR) {
        c_die("Cannot resize window!");
    }
#endif /* HAVE_WRESIZE */
#endif /* HAVE_RESIZETERM */

    var_init();
    /* Do these because width may have changed... */
    clear();
    refresh();
}

/* -R "reality" mode: the falling-glyph background of codeofreality.org, ported to the terminal.
 * Every 2nd column holds one strip of LINES+10 glyphs that spans the whole screen height; its
 * bottom glyph is the head. Strips fall at their own speed, glyphs flicker, and the color runs in
 * Nord Aurora orange (nord12) from near-black at the top to a bold head (256-color terminals).
 *
 * Output glyphs (not to be confused with the internal HEBREW_BASE cell values): by default the 30
 * Private Use codepoints U+E100..U+E11D, one per char of "abcdefghijklmnopqrstuvwxyz1234". No font is
 * shipped; map that range to a font of your choice, e.g. kitty: symbol_map U+E100-U+E11D <font>.
 * With -H the strips use the Hebrew letters instead. */
#define REALITY_PUA_BASE 0xE100
#define REALITY_PUA_COUNT 30
#define REALITY_TAIL_EXTRA 10        /* strip length = LINES + 10, as on the site */
/* Site: 0.5..2.0 px/frame at 60 fps in 16 px cells, times its idle (no scroll) factor 0.3. */
#define REALITY_SPEED_MIN 0.5625
#define REALITY_SPEED_SPAN 1.6875
#define REALITY_MUTATE_PER_S 1.2     /* 2 % per 60 fps frame */

enum { RP_TAIL = 10, RP_DARK, RP_RED, RP_DEEP, RP_ORANGE, RP_HEAD };

typedef struct {
    double y;                        /* row of the strip's top glyph; negative = above the screen */
    double speed;                    /* rows per second */
    unsigned char *glyph;            /* LINES + REALITY_TAIL_EXTRA glyph indices */
} reality_col;

static reality_col *reality_cols = NULL;
static int reality_ncols = 0;
static int reality_len = 0;
static char reality_pua[REALITY_PUA_COUNT][4];

static double reality_rand01(void) {
    return (double) rand() / ((double) RAND_MAX + 1.0);
}

static int reality_glyph_count(void) {
    return hebrew ? HEBREW_COUNT : REALITY_PUA_COUNT;
}

static int reality_floor(double v) {
    int i = (int) v;
    return (v < 0 && (double) i != v) ? i - 1 : i;
}

static void reality_fill(reality_col *c) {
    int n = reality_glyph_count();
    for (int k = 0; k < reality_len; k++) {
        c->glyph[k] = (unsigned char) (rand() % n);
    }
}

static void reality_free(void) {
    for (int j = 0; j < reality_ncols; j++) {
        free(reality_cols[j].glyph);
    }
    free(reality_cols);
    reality_cols = NULL;
    reality_ncols = 0;
}

static void reality_init(void) {
    reality_free();
    reality_ncols = (COLS + 1) / 2;
    reality_len = LINES + REALITY_TAIL_EXTRA;
    reality_cols = nmalloc(sizeof(reality_col) * reality_ncols);
    for (int j = 0; j < reality_ncols; j++) {
        reality_col *c = &reality_cols[j];
        c->glyph = nmalloc((size_t) reality_len);
        c->y = -reality_rand01() * LINES;
        c->speed = REALITY_SPEED_MIN + reality_rand01() * REALITY_SPEED_SPAN;
        reality_fill(c);
    }
}

static void reality_colors(void) {
    for (int k = 0; k < REALITY_PUA_COUNT; k++) {
        unsigned cp = REALITY_PUA_BASE + (unsigned) k;  /* 3-byte UTF-8 */
        reality_pua[k][0] = (char) (0xE0 | (cp >> 12));
        reality_pua[k][1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        reality_pua[k][2] = (char) (0x80 | (cp & 0x3F));
        reality_pua[k][3] = '\0';
    }
    if (!has_colors()) {
        return;
    }
    short bg = COLOR_BLACK;
#ifdef HAVE_USE_DEFAULT_COLORS
    if (use_default_colors() != ERR) {
        bg = -1;
    }
#endif
    if (COLORS >= 256) {
        /* Nord Aurora orange (nord12 #d08770) at 30/45/65/85/100 % brightness, nearest xterm-256. */
        init_pair(RP_TAIL, 236, bg);
        init_pair(RP_DARK, 238, bg);
        init_pair(RP_RED, 95, bg);
        init_pair(RP_DEEP, 131, bg);
        init_pair(RP_ORANGE, 173, bg);
        init_pair(RP_HEAD, 173, bg);
    } else {
        init_pair(RP_TAIL, COLOR_RED, bg);
        init_pair(RP_DARK, COLOR_RED, bg);
        init_pair(RP_RED, COLOR_RED, bg);
        init_pair(RP_DEEP, COLOR_RED, bg);
        init_pair(RP_ORANGE, COLOR_YELLOW, bg);
        init_pair(RP_HEAD, COLOR_YELLOW, bg);
    }
}

/* Color pair and attributes for glyph i of a strip (0 = top, reality_len - 1 = head). */
static attr_t reality_attr(int i) {
    double l = (double) i / (double) reality_len;
    if (i == reality_len - 1) {
        return COLOR_PAIR(RP_HEAD) | A_BOLD;
    }
    if (l > 0.9) {
        return COLOR_PAIR(RP_ORANGE) | A_BOLD;
    }
    if (l > 0.75) {
        return COLOR_PAIR(RP_DEEP);
    }
    if (l > 0.6) {
        return COLOR_PAIR(RP_RED);
    }
    if (l > 0.3) {
        return COLOR_PAIR(RP_DARK);
    }
    return COLOR_PAIR(RP_TAIL);
}

static void reality_draw_glyph(int g) {
    addstr(hebrew ? hebrew_letters[g] : reality_pua[g]);
}

static double reality_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

/* Main loop for -R; never returns (exits through finish()). */
static void reality_loop(int screensaver, int *update) {
    int pause = 0;
    double last = reality_now();

    reality_colors();
    reality_init();
    clear();

    while (1) {
        if (signal_status == SIGINT || signal_status == SIGQUIT || signal_status == SIGTSTP) {
            if (lock != 1) {
                finish();
            }
            signal_status = 0;
        }
        if (signal_status == SIGWINCH) {
            resize_screen();
            reality_init();
            signal_status = 0;
        }

        int keypress = wgetch(stdscr);
        if (keypress != ERR) {
            if (screensaver == 1) {
#ifdef USE_TIOCSTI
                do {
                    char ch = (char) keypress;
                    ioctl(STDIN_FILENO, TIOCSTI, &ch);
                } while ((keypress = wgetch(stdscr)) != ERR);
#endif
                finish();
            }
            if (keypress == 'q' && lock != 1) {
                finish();
            } else if (keypress == 'L') {
                lock = 1;
            } else if (keypress == 'p' || keypress == 'P') {
                pause = !pause;
            } else if (keypress >= '0' && keypress <= '9') {
                *update = keypress - '0';
            }
        }

        double now = reality_now();
        double dt = now - last;
        last = now;
        if (dt > 0.25) {
            dt = 0.25;  /* after a stall, do not jump */
        }
        double mutate = REALITY_MUTATE_PER_S * dt;
        int n = reality_glyph_count();

        for (int c = 0; c < reality_ncols; c++) {
            reality_col *col = &reality_cols[c];
            if (!pause) {
                col->y += col->speed * dt;
                if (col->y > LINES) {
                    /* Whole strip has left the screen: restart above it with fresh glyphs. */
                    col->y = -reality_rand01() * LINES * 0.5;
                    reality_fill(col);
                }
            }
            int top = reality_floor(col->y);
            int x = c * 2;
            for (int row = 0; row < LINES; row++) {
                int i = row - top;
                move(row, x);
                if (i < 0 || i >= reality_len) {
                    addch(' ');
                } else {
                    if (!pause && reality_rand01() < mutate) {
                        col->glyph[i] = (unsigned char) (rand() % n);
                    }
                    attr_t a = reality_attr(i);
                    attron(a);
                    reality_draw_glyph(col->glyph[i]);
                    attroff(a);
                }
                if (x + 1 < COLS) {
                    /* Explicit blank so wide-rendered glyphs (kitty PUA) get their second cell. */
                    move(row, x + 1);
                    addch(' ');
                }
            }
        }
        refresh();
        napms(*update * 10);
    }
}

int main(int argc, char *argv[]) {
    int i, y, z, optchr, keypress;
    int j = 0;
    int count = 0;
    int screensaver = 0;
    int asynch = 0;
    int bold = 0;
    int force = 0;
    int firstcoldone = 0;
    int oldstyle = 0;
    int random = 0;
    int update = 4;
    int highnum = 0;
    int mcolor = COLOR_GREEN;
    int rainbow = 0;
    int lambda = 0;
    int randnum = 0;
    int randmin = 0;
    int pause = 0;
    int classic = 0;

    srand((unsigned) time(NULL));
    setlocale(LC_ALL, "");

    /* Many thanks to morph- (morph@jmss.com) for this getopt patch */
    opterr = 0;
    while ((optchr = getopt(argc, argv, "abBcfhHlLnrRosmxVu:C:")) != EOF) {
        switch (optchr) {
        case 's':
            screensaver = 1;
            break;
        case 'a':
            asynch = 1;
            break;
        case 'b':
            if (bold != 2) {
                bold = 1;
            }
            break;
        case 'B':
            bold = 2;
            break;
        case 'C':
            if (!strcasecmp(optarg, "green")) {
                mcolor = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                mcolor = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                mcolor = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                mcolor = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                mcolor = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                mcolor = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                mcolor = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                mcolor = COLOR_BLACK;
            } else {
                c_die(" Invalid color selection\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta " "and black.\n");
            }
            break;
        case 'c':
            classic = 1;
            break;
        case 'H':
            hebrew = 1;
            break;
        case 'R':
            reality = 1;
            break;
        case 'f':
            force = 1;
            break;
        case 'l':
            console = 1;
            break;
        case 'L':
            lock = 1;
            break;
        case 'n':
            bold = -1;
            break;
        case 'h':
        case '?':
            usage();
            exit(0);
        case 'o':
            oldstyle = 1;
            break;
        case 'u':
            update = atoi(optarg);
            break;
        case 'x':
            xwindow = 1;
            break;
        case 'V':
            version();
            exit(0);
        case 'r':
             rainbow = 1;
             break;
        case 'm':
             lambda = 1;
             break;
        }
    }

    if (force && strcmp("linux", getenv("TERM"))) {
        /* setenv is much more safe to use than putenv */
        setenv("TERM", "linux", 1);
    }
    initscr();
    savetty();
    nonl();
    cbreak();
    noecho();
    timeout(0);
    leaveok(stdscr, TRUE);
    curs_set(0);
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
    signal(SIGWINCH, sighandler);
    signal(SIGTSTP, sighandler);

if (console) {
#ifdef HAVE_CONSOLECHARS
        if (va_system("consolechars -f matrix") != 0) {
            c_die
                (" There was an error running consolechars. Please make sure the\n"
                 " consolechars program is in your $PATH.  Try running \"consolechars -f matrix\" by hand.\n");
        }
#elif defined(HAVE_SETFONT)
        if (va_system("setfont matrix") != 0) {
            c_die
                (" There was an error running setfont. Please make sure the\n"
                 " setfont program is in your $PATH.  Try running \"setfont matrix\" by hand.\n");
        }
#else
        c_die(" Unable to use both \"setfont\" and \"consolechars\".\n");
#endif
}
    if (has_colors()) {
        start_color();
        /* Add in colors, if available */
#ifdef HAVE_USE_DEFAULT_COLORS
        if (use_default_colors() != ERR) {
            init_pair(COLOR_BLACK, -1, -1);
            init_pair(COLOR_GREEN, COLOR_GREEN, -1);
            init_pair(COLOR_WHITE, COLOR_WHITE, -1);
            init_pair(COLOR_RED, COLOR_RED, -1);
            init_pair(COLOR_CYAN, COLOR_CYAN, -1);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
            init_pair(COLOR_BLUE, COLOR_BLUE, -1);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);
        } else {
#else
        { /* Hack to deal the after effects of else in HAVE_USE_DEFAULT_COLOURS*/
#endif
            init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
            init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
            init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
            init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
            init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        }
    }

    /* Set up values for random number generation */
    if (hebrew) {
        /* rand() % randnum + randmin covers exactly the HEBREW_COUNT private values. */
        randmin = HEBREW_BASE;
        highnum = HEBREW_BASE + HEBREW_COUNT;
    } else if(classic) {
        /* Japanese character unicode range [they are seen in the original cmatrix] */
        randmin = 12288;
        highnum = 12351;
    } else if (console || xwindow) {
        randmin = 166;
        highnum = 217;
    } else {
        randmin = 33;
        highnum = 123;
    }
    randnum = highnum - randmin;

    var_init();

    if (reality) {
        reality_loop(screensaver, &update);
    }

    while (1) {
        /* Check for signals */
        if (signal_status == SIGINT || signal_status == SIGQUIT) {
            if(lock != 1)
                finish();
            /* exits */
        }
        if (signal_status == SIGWINCH) {
            resize_screen();
            signal_status = 0;
        }

        if(signal_status == SIGTSTP){
            if(lock != 1)
                    finish();
        }

        count++;
        if (count > 4) {
            count = 1;
        }

        if ((keypress = wgetch(stdscr)) != ERR) {
            if (screensaver == 1) {
#ifdef USE_TIOCSTI
                char *str = malloc(0);
                size_t str_len = 0;
                do {
                    str = realloc(str, str_len + 1);
                    str[str_len++] = keypress;
                } while ((keypress = wgetch(stdscr)) != ERR);
                size_t i;
                for (i = 0; i < str_len; i++)
                    ioctl(STDIN_FILENO, TIOCSTI, (char*)(str + i));
                free(str);
#endif
                finish();
            } else {
                switch (keypress) {
                case 'q':
                    if(lock != 1)
                        finish();
                    break;
                case 'a':
                    asynch = 1 - asynch;
                    break;
                case 'b':
                    bold = 1;
                    break;
                case 'B':
                    bold = 2;
                    break;
                case 'L':
                    lock = 1;
                    break;
                case 'n':
                    bold = 0;
                    break;
                case '0': /* Fall through */
                case '1': /* Fall through */
                case '2': /* Fall through */
                case '3': /* Fall through */
                case '4': /* Fall through */
                case '5': /* Fall through */
                case '6': /* Fall through */
                case '7': /* Fall through */
                case '8': /* Fall through */
                case '9':
                    update = keypress - 48;
                    break;
                case '!':
                    mcolor = COLOR_RED;
                    rainbow = 0;
                    break;
                case '@':
                    mcolor = COLOR_GREEN;
                    rainbow = 0;
                    break;
                case '#':
                    mcolor = COLOR_YELLOW;
                    rainbow = 0;
                    break;
                case '$':
                    mcolor = COLOR_BLUE;
                    rainbow = 0;
                    break;
                case '%':
                    mcolor = COLOR_MAGENTA;
                    rainbow = 0;
                    break;
                case 'r':
                     rainbow = 1;
                     break;
                case 'm':
                     lambda = !lambda;
                     break;
                case '^':
                    mcolor = COLOR_CYAN;
                    rainbow = 0;
                    break;
                case '&':
                    mcolor = COLOR_WHITE;
                    rainbow = 0;
                    break;
                case 'p':
                case 'P':
                    pause = (pause == 0)?1:0;
                    break;

                }
            }
        }
        for (j = 0; j <= COLS - 1; j += 2) {
            if ((count > updates[j] || asynch == 0) && pause == 0) {

                /* I dont like old-style scrolling, yuck */
                if (oldstyle) {
                    for (i = LINES - 1; i >= 1; i--) {
                        matrix[i][j].val = matrix[i - 1][j].val;
                    }
                    random = (int) rand() % (randnum + 8) + randmin;

                    if (matrix[1][j].val == 0) {
                        matrix[0][j].val = 1;
                    } else if (matrix[1][j].val == ' '
                             || matrix[1][j].val == -1) {
                        if (spaces[j] > 0) {
                            matrix[0][j].val = ' ';
                            spaces[j]--;
                        } else {

                            /* Random number to determine whether head of next collumn
                               of chars has a white 'head' on it. */

                            if (((int) rand() % 3) == 1) {
                                matrix[0][j].val = 0;
                            } else {
                                matrix[0][j].val = (int) rand() % randnum + randmin;
                            }
                            spaces[j] = (int) rand() % LINES + 1;
                        }
                    } else if (random > highnum && matrix[1][j].val != 1) {
                        matrix[0][j].val = ' ';
                    } else {
                        matrix[0][j].val = (int) rand() % randnum + randmin;
                    }

                } else { /* New style scrolling (default) */
                    if (matrix[0][j].val == -1 && matrix[1][j].val == ' '
                        && spaces[j] > 0) {
                        matrix[0][j].val = -1;
                        spaces[j]--;
                    } else if (matrix[0][j].val == -1
                        && matrix[1][j].val == ' ') {
                        length[j] = (int) rand() % (LINES - 3) + 3;
                        matrix[0][j].val = (int) rand() % randnum + randmin;

                        spaces[j] = (int) rand() % LINES + 1;
                    }
                    i = 0;
                    y = 0;
                    firstcoldone = 0;
                    while (i <= LINES) {

                        /* Skip over spaces */
                        while (i <= LINES && (matrix[i][j].val == ' ' ||
                               matrix[i][j].val == -1)) {
                            i++;
                        }

                        if (i > LINES) {
                            break;
                        }

                        /* Go to the head of this collumn */
                        z = i;
                        y = 0;
                        while (i <= LINES && (matrix[i][j].val != ' ' &&
                               matrix[i][j].val != -1)) {
                            matrix[i][j].is_head = false;
                            i++;
                            y++;
                        }

                        if (i > LINES) {
                            matrix[z][j].val = ' ';
                            continue;
                        }

                        matrix[i][j].val = (int) rand() % randnum + randmin;
                        matrix[i][j].is_head = true;

                        /* If we're at the top of the collumn and it's reached its
                           full length (about to start moving down), we do this
                           to get it moving.  This is also how we keep segments not
                           already growing from growing accidentally =>
                         */
                        if (y > length[j] || firstcoldone) {
                            matrix[z][j].val = ' ';
                            matrix[0][j].val = -1;
                        }
                        firstcoldone = 1;
                        i++;
                    }
                }
            }
            /* A simple hack */
            if (!oldstyle) {
                y = 1;
                z = LINES;
            } else {
                y = 0;
                z = LINES - 1;
            }
            for (i = y; i <= z; i++) {
                move(i - y, j);

                if (matrix[i][j].is_head && !rainbow) {
                    if (console || xwindow) {
                        attron(A_ALTCHARSET);
                    }
                    attron(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attron(A_BOLD);
                    }
                    if (matrix[i][j].val == 0) {
                        if (console || xwindow) {
                            addch(183);
                        } else {
                            addch('&');
                        }
                    } else {
                        addcell(matrix[i][j].val);
                    }

                    attroff(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attroff(A_BOLD);
                    }
                    if (console || xwindow) {
                        attroff(A_ALTCHARSET);
                    }
                } else {
                    if(rainbow) {
                        int randomColor = rand() % 6;

                        switch(randomColor){
                            case 0:
                                mcolor = COLOR_GREEN;
                                break;
                            case 1:
                                mcolor = COLOR_BLUE;
                                break;
                            case 2:
                                mcolor = COLOR_BLACK;
                                break;
                            case 3:
                                mcolor = COLOR_YELLOW;
                                break;
                            case 4:
                                mcolor = COLOR_CYAN;
                                break;
                            case 5:
                                mcolor = COLOR_MAGENTA;
                                break;
                       }
                    }
                    attron(COLOR_PAIR(mcolor));
                    if (matrix[i][j].val == 1) {
                        if (bold) {
                            attron(A_BOLD);
                        }
                        addch('|');
                        if (bold) {
                            attroff(A_BOLD);
                        }
                    } else {
                        if (console || xwindow) {
                            attron(A_ALTCHARSET);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attron(A_BOLD);
                        }
                        if (matrix[i][j].val == -1) {
                            addch(' ');
                        } else if (lambda && matrix[i][j].val != ' ') {
                            addstr("λ");
                        } else {
                            addcell(matrix[i][j].val);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attroff(A_BOLD);
                        }
                        if (console || xwindow) {
                            attroff(A_ALTCHARSET);
                        }
                    }
                    attroff(COLOR_PAIR(mcolor));
                }
            }
        }

        //Check if computer is locked
        if(lock == 1){

            //Add our message to the screen
            char *msg = "Computer locked.";
            int msg_x = LINES/2;
            int msg_y = COLS/2 - strlen(msg)/2;
            int i = 0;

            //Add space before message
            move(msg_x-1, msg_y-2);
            for(i = 0; i < strlen(msg)+4; i++)
                addch(' ');

            //Write message
            move(msg_x, msg_y-2);
            addch(' ');
            addch(' ');
            addstr(msg);
            addch(' ');
            addch(' ');

            //Add space after message
            move(msg_x+1, msg_y-2);
            for(i = 0; i < strlen(msg)+4; i++)
                addch(' ');
        }

        napms(update * 10);
    }
    finish();
}

