/* CPDD - Copy with deduplication main header */
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

/* Linking strategy options */
typedef enum {
    LINK_NONE,    /* Regular copy */
    LINK_HARD,    /* Hard links to duplicates */
    LINK_SOFT     /* Symbolic links to duplicates */
} link_type_t;

/* File attributes to preserve during copy */
typedef struct {
    int mode;       /* File permissions */
    int ownership;  /* User/group ownership */
    int timestamps; /* Access/modification times */
    int all;        /* Preserve all attributes */
} preserve_t;

/* Operation statistics */
typedef struct {
    int files_copied;        /* Files physically copied */
    int files_hard_linked;   /* Files hard linked */
    int files_soft_linked;   /* Files soft linked */
    int files_skipped;       /* Files skipped (no-clobber) */
    off_t bytes_copied;      /* Bytes physically copied */
    off_t bytes_hard_linked; /* Bytes saved via hard links */
    off_t bytes_soft_linked; /* Bytes saved via soft links */
} stats_t;

/* Command line options */
typedef struct {
    char **sources;         /* Source directories/files */
    int source_count;       /* Number of sources */
    char *dest_dir;         /* Destination directory */
    char *ref_dir;          /* Reference directory for deduplication */
    link_type_t link_type;  /* Linking strategy */
    int verbose;            /* Verbose output */
    int recursive;          /* Recursive directory traversal */
    int no_clobber;         /* Don't overwrite existing files */
    int interactive;        /* Prompt before overwriting */
    int show_stats;         /* Display operation statistics */
    int human_readable;     /* Human-readable byte counts */
    preserve_t preserve;    /* Attributes to preserve */
} options_t;

/* Reference file information for deduplication */
typedef struct file_info {
    char *path;                         /* Full path to file */
    off_t size;                         /* File size in bytes */
    unsigned char md5[MD5_DIGEST_LENGTH]; /* MD5 checksum */
    int needs_md5;                      /* Whether MD5 calculation is needed */
    int has_md5;                        /* Whether MD5 has been calculated */
    struct file_info *next;             /* Next file in linked list */
} file_info_t;

/* Command line parsing */
int parse_args(int argc, char *argv[], options_t *opts);

/* Main copy operations */
int copy_directory(const options_t *opts, stats_t *stats);
int create_directory_structure(const char *src_path, const char *dest_path);

/* Sorted file info structure */
typedef struct {
    file_info_t **files;
    int count;
    int capacity;
} sorted_file_info_t;

/* File matching and deduplication */
sorted_file_info_t *scan_reference_directory(const options_t *opts);
file_info_t *find_matching_file(sorted_file_info_t *ref_files, const char *src_file, const options_t *opts);
int calculate_md5(const char *filename, unsigned char *digest);
int files_identical(const char *file1, const char *file2);
int files_match(file_info_t *ref_file, file_info_t *src_file);

/* File operations */
int copy_or_link_file(const char *src, const char *dest, const char *ref, const options_t *opts, stats_t *stats);
int should_overwrite(const char *dest_path, const options_t *opts);
int preserve_file_attributes(const char *src, const char *dest, const preserve_t *preserve);
int parse_preserve_list(const char *preserve_list, preserve_t *preserve);

/* Statistics and output formatting */
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size);
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size);
void print_statistics(const stats_t *stats, int human_readable);
void free_file_list(file_info_t *list);
void free_sorted_file_info(sorted_file_info_t *sorted_files);
void print_usage(const char *program_name);

/* Terminal output and status display */
int terminal_supports_clear_eol(void);
void print_status_update(const char *format, ...);
void fprint_status_update(FILE *stream, const char *format, ...);
void clear_status_line(void);
void fclear_status_line(FILE *stream);
void print_stats_at_bottom(const char *format, ...);

/* Signal handling and cleanup */
void setup_signal_handlers(void);
void register_incomplete_file(const char *path);
void unregister_incomplete_file(void);
void cleanup_incomplete_file(void);

#endif
