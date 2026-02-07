/*
 * common/terminal.c - Self-contained terminal capability detection
 * 
 * Copyright (c) 2025 Lee de Byl <lee@32kb.net>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "cpdd.h"
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>

static int terminal_capability_checked = 0;
static int supports_clear_eol = 0;
static int supports_color = 0;
static int stats_line_active = 0;

static int terminal_supports_clear_eol_for_fd(int fd) {
    if (!isatty(fd)) {
        return 0;
    }

    const char *term = getenv("TERM");

    if (strstr(term, "xterm")  ||
        strstr(term, "screen") ||
        strstr(term, "tmux")   ||
        strstr(term, "vt100")  ||
        strstr(term, "vt102")  ||
        strstr(term, "vt220")  ||
        strstr(term, "vt320")  ||
        strstr(term, "linux")  ||
        strstr(term, "color")  ||
        strstr(term, "ansi"))
    {
        return 1;
    } else {
        return 0;
    }
}

static int terminal_supports_color_for_fd(int fd) {
    /* Respect NO_COLOR environment variable */
    if (getenv("NO_COLOR") != NULL) {
        return 0;
    }

    if (!isatty(fd)) {
        return 0;
    }

    const char *term = getenv("TERM");
    if (!term) {
        return 0;
    }

    /* Check for common color-capable terminals */
    if (strstr(term, "xterm")  ||
        strstr(term, "screen") ||
        strstr(term, "tmux")   ||
        strstr(term, "linux")  ||
        strstr(term, "color")  ||
        strstr(term, "ansi")   ||
        strstr(term, "256"))
    {
        return 1;
    }

    return 0;
}

int terminal_supports_clear_eol(void) {
    if (terminal_capability_checked) {
        return supports_clear_eol;
    }

    terminal_capability_checked = 1;
    supports_clear_eol = terminal_supports_clear_eol_for_fd(STDERR_FILENO);
    supports_color = terminal_supports_color_for_fd(STDERR_FILENO);

    return supports_clear_eol;
}

int terminal_supports_color(void) {
    if (!terminal_capability_checked) {
        terminal_supports_clear_eol(); /* Initialize */
    }
    return supports_color;
}

void print_status_update(const char *format, ...) {
    va_list args;
    va_start(args, format);

    if (terminal_supports_clear_eol()) {
        /* Use reverse video for status line to make it stand out */
        fprintf(stderr, "\r\033[7m");  /* \033[7m = reverse video */
        vfprintf(stderr, format, args);
        fprintf(stderr, "\033[0m");    /* \033[0m = reset attributes */
        fprintf(stderr, "\033[K");     /* Clear to end of line */
        fflush(stderr);
    } else {
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }

    va_end(args);
}

void fprint_status_update(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    int fd = fileno(stream);
    if (terminal_supports_clear_eol_for_fd(fd)) {
        fprintf(stream, "\r");
        vfprintf(stream, format, args);
        fprintf(stream, "\033[K");
        fflush(stream);
    } else {
        vfprintf(stream, format, args);
        fprintf(stream, "\n");
    }
    
    va_end(args);
}

void clear_status_line(void) {
    fclear_status_line(stderr);
}

void fclear_status_line(FILE *stream) {
    int fd = fileno(stream);
    if (terminal_supports_clear_eol_for_fd(fd)) {
        fprintf(stream, "\r\033[0m\033[K");  /* Reset attributes + clear line */
        fflush(stream);
    }
}

void print_stats_at_bottom(const char *format, ...) {
    va_list args;
    va_start(args, format);

    if (!terminal_supports_clear_eol()) {
        /* Fallback for non-terminal output - write to stderr */
        fprintf(stderr, "[STATS] ");
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        fflush(stderr);
        va_end(args);
        return;
    }

    /* Update stats on current line on stderr */
    fprintf(stderr, "\r\033[7m[STATS] "); /* Reverse video + label */
    vfprintf(stderr, format, args);
    fprintf(stderr, "\033[0m");           /* Reset attributes */
    fprintf(stderr, "\033[K");            /* Clear to end of line */
    fflush(stderr);
    stats_line_active = 1;

    va_end(args);
}

/* Print verbose message, clearing stats line if needed */
void print_verbose(const char *format, ...) {
    va_list args;

    /* Clear stats line if active */
    if (stats_line_active && terminal_supports_clear_eol()) {
        fprintf(stderr, "\r\033[K");  /* Clear line */
        stats_line_active = 0;
    }

    /* Print verbose message to stderr */
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Clear stats line at end of operation */
void finalize_stats_line(void) {
    if (stats_line_active && terminal_supports_clear_eol()) {
        fprintf(stderr, "\n");  /* Move to new line, preserving stats */
        fflush(stderr);
        stats_line_active = 0;
    }
}

/* Truncate a path to fit within max_width, showing first and last parts */
void truncate_path(const char *path, char *buffer, size_t buffer_size, int max_width) {
    int path_len = strlen(path);

    if (path_len <= max_width) {
        /* Path fits, just copy it */
        strncpy(buffer, path, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    /* Calculate how much to show from start and end */
    int prefix_len = (max_width - 3) / 2;  /* -3 for "..." */
    int suffix_len = max_width - prefix_len - 3;

    if (prefix_len < 1 || suffix_len < 1) {
        /* Too short to truncate meaningfully */
        strncpy(buffer, path, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    snprintf(buffer, buffer_size, "%.*s...%s",
             prefix_len, path,
             path + path_len - suffix_len);
}

/* ANSI color codes */
const char *color_reset(void) {
    return terminal_supports_color() ? "\033[0m" : "";
}

const char *color_green(void) {
    return terminal_supports_color() ? "\033[32m" : "";
}

const char *color_blue(void) {
    return terminal_supports_color() ? "\033[34m" : "";
}

const char *color_yellow(void) {
    return terminal_supports_color() ? "\033[33m" : "";
}

const char *color_cyan(void) {
    return terminal_supports_color() ? "\033[36m" : "";
}

const char *color_dim(void) {
    return terminal_supports_color() ? "\033[2m" : "";
}
