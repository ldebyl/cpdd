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
// ... same includes ...

static void draw_status(const char *msg) {
    term_goto(rows, 1);      // go to bottom line (outside scroll region)
    term_clear_line();
    char buf[4096];
    int len = snprintf(buf, sizeof buf, " %s", msg);
    if (len > cols) buf[cols] = '\0';
    fprintf(stdout, "%-*.*s", cols, cols, buf);
    // IMPORTANT: move back into the scrolling region so normal prints go there
    term_goto(rows - 1, 1);
    fflush(stdout);
}

static void sleep_ms(long ms){
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    tcgetattr(STDIN_FILENO, &orig_tio);

    if (!isatty(STDOUT_FILENO)) { fprintf(stderr,"stdout is not a terminal\n"); return 1; }

    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    signal(SIGHUP, on_sig); signal(SIGWINCH, on_sig);

    get_winsize();
    term_show_cursor(0);
    if (rows > 1) term_set_region(1, rows - 1);

    // start with cursor inside the scroll region
    term_goto(rows - 1, 1);

    for (int i = 0; i <= 100; i++) {
        // log lines now scroll within the region
        printf("doing step %d/100...\n", i);

        char status[128];
        snprintf(status, sizeof status, "Progress: %3d%%  | Press Ctrl-C to quit", i);
        draw_status(status);   // redraw status, then cursor returns to rows-1

        sleep_ms(50);
    }

    draw_status("Done ✔");
    cleanup();
    return 0;
}

