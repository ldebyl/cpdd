// cc -Wall -O2 statusline.c -o statusline
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <time.h>

static int rows = 24, cols = 80;
static struct termios orig_tio;

static void get_winsize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
}

static void term_show_cursor(int show) {
    fputs(show ? "\x1b[?25h" : "\x1b[?25l", stdout);
}

static void term_reset_region(void) {
    fputs("\x1b[r", stdout); // reset scroll region to full screen
}

static void term_set_region(int top, int bottom) {
    // DECSTBM: set top/bottom margins; 1-based inclusive
    fprintf(stdout, "\x1b[%d;%dr", top, bottom);
}

static void term_goto(int r, int c) {
    fprintf(stdout, "\x1b[%d;%dH", r, c); // 1-based
}

static void term_clear_line(void) {
    fputs("\x1b[2K", stdout); // clear entire line
}

static void cleanup(void) {
    term_reset_region();
    term_show_cursor(1);
    // Move cursor below the status line so the shell prompt isn’t on top of it
    term_goto(rows, 1);
    fputc('\n', stdout);
    fflush(stdout);
    // No raw mode here, but if you enable it later, restore it:
    if (isatty(STDIN_FILENO)) tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio);
}

static void on_sig(int sig) {
    if (sig == SIGWINCH) {
        get_winsize();
        term_reset_region();
        if (rows > 1) term_set_region(1, rows - 1);
    } else {
        cleanup();
        _exit(0);
    }
}

static void draw_status(const char *msg) {
    term_goto(rows, 1);
    term_clear_line();
    // Trim/pad to width
    char buf[4096];
    int len = snprintf(buf, sizeof buf, " %s", msg);
    if (len > cols) buf[cols] = '\0';
    fprintf(stdout, "%-*.*s", cols, cols, buf);
    fflush(stdout);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered stdout
    tcgetattr(STDIN_FILENO, &orig_tio);

    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "stdout is not a terminal\n");
        return 1;
    }

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGHUP,  on_sig);
    signal(SIGWINCH,on_sig);

    get_winsize();
    term_show_cursor(0);
    if (rows > 1) term_set_region(1, rows - 1);

    // Fake “work”: print logs up top, update status at bottom
    for (int i = 0; i <= 100; i++) {
        // Log line goes in scrolling region (no cursor moves here)
        time_t t = time(NULL);
        struct tm tm; localtime_r(&t, &tm);
        char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", &tm);
        printf("[%s] doing step %d/100...\n", ts, i);

        char status[256];
        snprintf(status, sizeof status, "Progress: %3d%%  |  Current step: %d  |  Press Ctrl-C to quit", i, i);
        draw_status(status);
struct timespec tss = {0, 50000000}; // 50ms
nanosleep(&tss, NULL);
    }

    draw_status("Done ✔");
    cleanup();
    return 0;
}

