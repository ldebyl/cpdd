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

int terminal_supports_clear_eol(void) {
    if (terminal_capability_checked) {
        return supports_clear_eol;
    }
    
    terminal_capability_checked = 1;
    supports_clear_eol = terminal_supports_clear_eol_for_fd(STDOUT_FILENO);
    
    return supports_clear_eol;
}

void print_status_update(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (terminal_supports_clear_eol()) {
        /* Use reverse video for status line to make it stand out */
        printf("\r\033[7m");  /* \033[7m = reverse video */
        vprintf(format, args);
        printf("\033[0m");    /* \033[0m = reset attributes */
        printf("\033[K");     /* Clear to end of line */
        fflush(stdout);
    } else {
        vprintf(format, args);
        printf("\n");
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
    fclear_status_line(stdout);
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
        /* Fallback for non-terminal output */
        printf("[STATS] ");
        vprintf(format, args);
        printf("\n");
        fflush(stdout);
        va_end(args);
        return;
    }
    
    /* Reserve bottom line for stats using ANSI escapes */
    printf("\033[s");          /* Save cursor position */
    printf("\033[999;1H");     /* Move to bottom line */
    printf("\033[2K");         /* Clear entire line */
    printf("\033[7m[STATS] "); /* Reverse video + label */
    vprintf(format, args);
    printf("\033[0m");         /* Reset attributes */
    printf("\033[u");          /* Restore cursor position */
    fflush(stdout);
    
    va_end(args);
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
