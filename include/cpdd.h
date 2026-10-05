/*
 * cpdd/include/cpdd.h - Main Header File for cpdd
 *  * 
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

#ifndef CPDD_H
#define CPDD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <signal.h>

/* Path and buffer size limits */
#define MAX_PATH 16384
#define MD5_DIGEST_LENGTH 16
#define BUFFER_SIZE 8192

/* readdir d_type support. POSIX only requires d_ino and d_name in struct
 * dirent, so d_type is an extension. It exists on Linux, *BSD, macOS and
 * Solaris 11+; older Solaris and other strict POSIX systems lack it. When
 * present it lets us avoid a stat() per directory entry. The DT_* constants
 * may be hidden by strict feature-test macros even where d_type is present,
 * so define them locally using the BSD-derived values used everywhere. */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define CPDD_HAVE_D_TYPE 1
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK    10
#define DT_SOCK   12
#endif
#endif

/* Block cache configuration */
#define BLOCK_HASH_SIZE 4         /* Size of hash per block (4 or 8 bytes) */
#define BLOCK_CACHE_GROW_SIZE 16  /* Number of blocks to allocate at once */
#define MAX_CACHED_BLOCKS 512     /* Maximum blocks to cache per file */

/* Logging Macros -- note the do/while allows inclusion and exclusion of the semicolon */
extern int g_verbose;
#define VERBOSE(...) do { \
    if (g_verbose) fprintf(stderr, __VA_ARGS__); \
} while(0)

/* Linking strategy options */
typedef enum {
    LINK_NONE,    /* Regular copy */
    LINK_HARD,    /* Hard links to duplicates */
    LINK_SOFT     /* Symbolic links to duplicates */
} link_type_t;

/* File matching results */
typedef enum {
    MATCH_SUCCESS = 1,        /* Files match completely */
    MATCH_FAIL_HASHES = 0,    /* Fast rejection via cached blocks */
    MATCH_FAIL_BYTEWISE = -1, /* Failed during bytewise comparison */
    MATCH_ERROR = -2          /* File I/O or other error */
} match_result_t;

/* File attributes to preserve during copy. ctime and birth time are not
 * here because no portable (or Linux) API can set them: the kernel stamps
 * ctime itself on every inode change, including the utimensat() below. */
typedef struct {
    int mode;       /* File permissions */
    int ownership;  /* User/group ownership: 0, or one of the below */
    int atime;      /* Access time */
    int mtime;      /* Modification time (on by default) */
} preserve_t;

/* Ownership requested only implicitly (-p, -a, bare --preserve, "all")
 * falls back quietly when not permitted, as cp(1) does; naming it in
 * --preserve=ownership makes the failure a reported error. */
#define PRESERVE_IMPLICIT 1
#define PRESERVE_EXPLICIT 2

#define PRESERVE_ANY(p) ((p)->mode || (p)->ownership || (p)->atime || (p)->mtime)

/* Operation statistics */
typedef struct {
    int files_copied;        /* Files physically copied */
    int files_hard_linked;   /* Files hard linked */
    int files_soft_linked;   /* Files soft linked */
    int files_skipped;       /* Files skipped (no-clobber) */
    int files_failed;        /* Files that failed to copy */
    off_t bytes_copied;      /* Bytes physically copied */
    off_t bytes_hard_linked; /* Bytes saved via hard links */
    off_t bytes_soft_linked; /* Bytes saved via soft links */
    
    /* Block cache statistics */
    int files_compared;      /* Files that went through comparison */
    int cache_hits;          /* Fast rejections via cached blocks */
    int total_cache_depth;   /* Sum of all cache depths for averaging */
    int min_cache_depth;     /* Minimum cache depth encountered */
    int max_cache_depth;     /* Maximum cache depth encountered */
    
    /* Size collision statistics */
    int total_source_files;  /* Total source files processed */
    int files_with_size_matches; /* Source files that had at least one size match in reference */
    int unique_ref_sizes;    /* Number of unique sizes in reference files */
    int total_ref_files;     /* Total reference files scanned */

    /* Memory usage */
    size_t index_memory;     /* Total bytes used by reference index */
} stats_t;

/* Command line options */
typedef struct {
    char **sources;         /* Source directories/files */
    int source_count;       /* Number of sources */
    char *dest_dir;         /* Destination directory */
    char **ref_dirs;        /* Reference directories for deduplication */
    int ref_dir_count;      /* Number of reference directories */
    link_type_t link_type;  /* Linking strategy for reference matches */
    link_type_t source_link_type; /* Linking strategy for unmatched files (link to source instead of copying) */
    int verbose;            /* Verbose output */
    int recursive;          /* Recursive directory traversal */
    int no_clobber;         /* Don't overwrite existing files */
    int interactive;        /* Prompt before overwriting */
    int update;             /* Overwrite only if source is newer */
    int dry_run;            /* Show what would be done without doing it */
    int only_new;           /* Only copy files that don't exist in reference directories */
    int show_stats;         /* Display operation statistics */
    int human_readable;     /* Human-readable byte counts */
    int match_name;         /* Match on filename in addition to size */
    int match_mtime;        /* Match on modification time in addition to size */
    int no_verify;          /* Skip content comparison (requires match_name or match_mtime) */
    int no_dereference;     /* Don't follow symlinks (copy them as-is) */
    int skip_symlinks;      /* Skip symlinks entirely */
    int prune_empty_dirs;   /* Don't leave behind directories nothing was placed in */
    size_t block_size;      /* I/O block size (0 = auto-detect) */
    off_t min_size;         /* Minimum file size for matching (0 = no minimum) */
    preserve_t preserve;    /* Attributes to preserve */
    char *log_file;         /* Processed-files log path (NULL = disabled) */
    FILE *log_fp;           /* Append handle for log_file */
    struct skip_set *skip_set; /* Loaded from log_file if it existed */
    char *cache_file;       /* Reference-scan cache path (NULL = disabled) */
    long cache_ttl;         /* Per-directory cache freshness window in seconds (<=0 = no expiry) */
} options_t;

/* Block-based MD5 cache for incremental comparison */
typedef struct {
    unsigned char (*block_md5s)[BLOCK_HASH_SIZE];  /* Dynamically allocated block hashes */
    int cached_blocks;                             /* Number of blocks currently cached */
    int allocated_blocks;                          /* Number of blocks allocated */
} block_hashes_t;

/* Reference file information for deduplication */
typedef struct file_info {
    char *path;                         /* Full path to file */
    char *basename;                     /* Basename of file (pointer into path) */
    off_t size;                         /* File size in bytes */
    time_t mtime;                       /* Modification time, whole seconds */
    block_hashes_t block_hashes;        /* Block-based MD5 hashes */
    struct file_info *next;             /* Next file in linked list */
    int ref_dir_index;                  /* Index into ref_files_t's ref_dir_paths this
                                          * file came from, when the reference cache is
                                          * in use; -1 otherwise. */
} file_info_t;

/* Command line parsing */
int parse_args(int argc, char *argv[], options_t *opts);

/* Main copy operations */
int copy_directory(const options_t *opts, stats_t *stats);
/* known_src_st: pass the caller's already-computed stat() of src_path to
 * avoid a redundant one here, or NULL to have it stat'd internally. */
int create_directory_structure(const char *src_path, const char *dest_path,
                                const options_t *opts, const struct stat *known_src_st);

/* Sorted file info structure */
typedef struct {
    file_info_t **files;
    int count;
    int capacity;

    /* Reference-cache bookkeeping (see ref_cache.c): which top-level -r
     * directory each entry in `files` came from, recorded so the cache can
     * be regrouped and saved by directory again. NULL/0 when opts->cache_file
     * is not set. ref_dir_paths[i] is realpath()'d, used as the cache's
     * identity key for that directory; ref_dir_scanned_at[i] is when that
     * directory's entries were captured (from cache load, or "now" for a
     * fresh scan), used to evaluate --cache-ttl per directory rather than
     * against the cache file's own mtime, which would drift once entries
     * from different scan times are merged together. */
    char **ref_dir_paths;
    time_t *ref_dir_scanned_at;
    int ref_dir_count;
} ref_files_t;

/* File matching and deduplication */
ref_files_t *scan_reference_directory(const options_t *opts, stats_t *stats);
/* src_size: the caller's already-known size of src_file (from a stat() it
 * already has), so this never needs to stat() src_file itself. */
file_info_t *find_matching_file(ref_files_t *ref_files, const char *src_file,
                                 off_t src_size, time_t src_mtime, const options_t *opts, stats_t *stats);
match_result_t files_match(file_info_t *ref_file, file_info_t *src_file);

/* Block hash management */
void init_block_hashes(block_hashes_t *hashes);
int grow_block_hashes(block_hashes_t *hashes);
void free_block_hashes(block_hashes_t *hashes);

/* File operations */
/* dest_dir_ready: pass true when the caller already guarantees dest's
 * parent directory exists (e.g. it was just created before iterating the
 * files inside it), so the regular-file copy path can skip re-verifying it
 * -- a stat() otherwise repeated, and almost always wasted, on every file. */
int copy_or_link_file(const char *src, const char *dest, ref_files_t *ref_files,
                       const options_t *opts, stats_t *stats, int dest_dir_ready);
int should_overwrite(const char *src_path, const char *dest_path, const options_t *opts);
int preserve_file_attributes(const struct stat *src_st, const char *dest,
                             const preserve_t *preserve, int nofollow);
int parse_preserve_list(const char *preserve_list, preserve_t *preserve, int value);

/* Block size utilities */
size_t parse_size(const char *size_str);
size_t get_optimal_block_size(const char *filename, const options_t *opts);


/* Statistics and output formatting */
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size);
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size);
void print_statistics(const stats_t *stats, int human_readable);
void free_file_list(file_info_t *list);
void free_sorted_file_info(ref_files_t *sorted_files);
void print_usage(const char *program_name);

/* Terminal output and status display */
int terminal_supports_clear_eol(void);
int terminal_supports_color(void);
void print_status_update(const char *format, ...);
void fprint_status_update(FILE *stream, const char *format, ...);
void clear_status_line(void);
void fclear_status_line(FILE *stream);
void print_stats_at_bottom(const char *format, ...);
void print_verbose(const char *format, ...);
void print_error(const char *format, ...);
void print_warning(const char *format, ...);
void finalize_stats_line(void);
void truncate_path(const char *path, char *buffer, size_t buffer_size, int max_width);

/* ANSI color codes (empty strings if colors not supported) */
const char *color_reset(void);
const char *color_green(void);
const char *color_blue(void);
const char *color_yellow(void);
const char *color_cyan(void);
const char *color_dim(void);
const char *color_red(void);
const char *color_bold(void);

/* Signal handling and cleanup */
void register_incomplete_file(const char *path);
void unregister_incomplete_file(void);
void cleanup_incomplete_file(void);

/* Processed-log format: TAB-separated <escaped-path>\t<size>\t<mtime>\t<decision>\n
 * per record. Path is escaped (\, TAB, NL -> \\, \t, \n) to keep the TSV intact. */
typedef struct skip_set skip_set_t;

int processed_log_open(const char *path, FILE **out_fp);
void processed_log_close(FILE *fp);
void processed_log_write(FILE *fp, const char *src_path,
                          off_t size, time_t mtime, const char *decision);

skip_set_t *skip_set_load(const char *path);
int skip_set_contains(const skip_set_t *set, const char *path,
                       off_t size, time_t mtime);
void skip_set_free(skip_set_t *set);

/* Reference-scan cache: persists a scanned ref_files_t (paths, sizes, and
 * whatever block hashes have been built up) to a binary file, so a re-run
 * against the same reference directories can skip rescanning them. Purely a
 * performance cache -- it is fully regenerable from the reference
 * directories at any time, so a missing, truncated, or version-mismatched
 * file is never an error, only a fresh scan. See src/cpdd/ref_cache.c. */

/* Read path into a ref_files_t containing whatever it holds (every
 * directory it recorded, not filtered to opts->ref_dirs -- the caller does
 * that). Returns NULL if the file doesn't exist, is corrupt, or is an
 * incompatible version; never treated as fatal by callers. */
ref_files_t *ref_cache_read(const char *path);

/* Write ref_files's cache-relevant contents (path, size, ref_dir_* fields,
 * block hashes) to path, replacing it directly. Returns 0 on success, -1 on
 * error (a warning is printed; never fatal to the copy in progress, since
 * losing a cache write only costs a future rescan). */
int ref_cache_write(const char *path, const ref_files_t *ref_files);

/* Call after each file is processed. Throttled internally (both by a file
 * count sample and by a minimum interval) so it is cheap to call
 * unconditionally; writes the cache at most every few seconds of real
 * progress, so a long run that gets interrupted still leaves a useful
 * cache behind. A no-op when opts->cache_file is NULL. */
void ref_cache_checkpoint(const ref_files_t *ref_files, const options_t *opts);

#endif
