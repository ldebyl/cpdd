/*
 * processed_log.c - Record processed source files and skip them on re-run.
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

#include <inttypes.h>

typedef struct {
    char *path;
    off_t size;
    time_t mtime;
} skip_entry_t;

struct skip_set {
    skip_entry_t *entries;
    size_t count;
};

/* Escape \, TAB and NL so a path can sit safely in a TSV field. */
static void write_escaped(FILE *fp, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '\\': fputs("\\\\", fp); break;
        case '\t': fputs("\\t", fp);  break;
        case '\n': fputs("\\n", fp);  break;
        default:   fputc(*s, fp);
        }
    }
}

/* In-place unescape of the reverse encoding. */
static void unescape_inplace(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
            case 't':  *w++ = '\t'; break;
            case 'n':  *w++ = '\n'; break;
            case '\\': *w++ = '\\'; break;
            default:   *w++ = *r;
            }
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

int processed_log_open(const char *path, FILE **out_fp)
{
    FILE *fp = fopen(path, "a");
    if (!fp) {
        print_error("Cannot open log file %s: %s", path, strerror(errno));
        *out_fp = NULL;
        return -1;
    }
    /* Line-buffered so each record flushes on its trailing \n -- an
     * interrupted run leaves complete records on disk without per-write fflush. */
    setvbuf(fp, NULL, _IOLBF, 0);
    *out_fp = fp;
    return 0;
}

void processed_log_close(FILE *fp)
{
    if (fp) fclose(fp);
}

void processed_log_write(FILE *fp, const char *src_path,
                          off_t size, time_t mtime, const char *decision)
{
    if (!fp) return;
    write_escaped(fp, src_path);
    fprintf(fp, "\t%" PRIdMAX "\t%" PRIdMAX "\t%s\n",
            (intmax_t)size, (intmax_t)mtime, decision);
}

/* String comparator for sorted skip_entry_t array. */
static int compare_skip_entry(const void *a, const void *b)
{
    const skip_entry_t *ea = a;
    const skip_entry_t *eb = b;
    return strcmp(ea->path, eb->path);
}

/* Returns NULL silently if the file doesn't exist (fresh run) or on any
 * other error after warning. The caller proceeds without skips either way. */
skip_set_t *skip_set_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno != ENOENT) {
            print_warning("Cannot read log %s: %s; continuing without skip-set",
                          path, strerror(errno));
        }
        return NULL;
    }

    size_t capacity = 256;
    size_t count = 0;
    skip_entry_t *entries = malloc(capacity * sizeof(skip_entry_t));
    if (!entries) {
        fclose(fp);
        print_error("Memory allocation failed loading skip-log");
        return NULL;
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t n;

    while ((n = getline(&line, &line_cap, fp)) != -1) {
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        /* Split on TAB. Fields: path, size, mtime, decision. */
        char *p = line;
        char *tab1 = strchr(p, '\t');
        if (!tab1) continue;
        *tab1 = '\0';
        char *path_field = p;

        char *size_field = tab1 + 1;
        char *tab2 = strchr(size_field, '\t');
        if (!tab2) continue;
        *tab2 = '\0';

        char *mtime_field = tab2 + 1;
        char *tab3 = strchr(mtime_field, '\t');
        if (tab3) *tab3 = '\0';

        unescape_inplace(path_field);

        if (count == capacity) {
            capacity *= 2;
            skip_entry_t *grown = realloc(entries, capacity * sizeof(skip_entry_t));
            if (!grown) {
                print_error("Memory allocation failed loading skip-log");
                free(line);
                for (size_t i = 0; i < count; i++) free(entries[i].path);
                free(entries);
                fclose(fp);
                return NULL;
            }
            entries = grown;
        }

        char *dup = strdup(path_field);
        if (!dup) continue;
        entries[count].path  = dup;
        entries[count].size  = (off_t)strtoll(size_field, NULL, 10);
        entries[count].mtime = (time_t)strtoll(mtime_field, NULL, 10);
        count++;
    }

    free(line);
    fclose(fp);

    qsort(entries, count, sizeof(skip_entry_t), compare_skip_entry);

    skip_set_t *set = malloc(sizeof(skip_set_t));
    if (!set) {
        for (size_t i = 0; i < count; i++) free(entries[i].path);
        free(entries);
        return NULL;
    }
    set->entries = entries;
    set->count = count;
    return set;
}

int skip_set_contains(const skip_set_t *set, const char *path,
                       off_t size, time_t mtime)
{
    if (!set || set->count == 0) return 0;
    skip_entry_t key = { (char *)path, 0, 0 };
    const skip_entry_t *hit = bsearch(&key, set->entries, set->count,
                                       sizeof(skip_entry_t), compare_skip_entry);
    if (!hit) return 0;
    return hit->size == size && hit->mtime == mtime;
}

void skip_set_free(skip_set_t *set)
{
    if (!set) return;
    for (size_t i = 0; i < set->count; i++) free(set->entries[i].path);
    free(set->entries);
    free(set);
}
