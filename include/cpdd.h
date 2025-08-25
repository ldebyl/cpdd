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

#define MAX_PATH 16384
#define MD5_DIGEST_LENGTH 16
#define BUFFER_SIZE 8192

typedef enum {
    LINK_NONE,
    LINK_HARD,
    LINK_SOFT
} link_type_t;

typedef struct {
    int mode;
    int ownership;
    int timestamps;
    int all;
} preserve_t;

typedef struct {
    int files_copied;
    int files_hard_linked;
    int files_soft_linked;
    int files_skipped;
    off_t bytes_copied;
    off_t bytes_hard_linked;
    off_t bytes_soft_linked;
} stats_t;

typedef struct {
    char **sources;
    int source_count;
    char *dest_dir;
    char *ref_dir;
    link_type_t link_type;
    int verbose;
    int recursive;
    int no_clobber;
    int interactive;
    int show_stats;
    int human_readable;
    preserve_t preserve;
} options_t;

typedef struct file_info {
    char *path;
    off_t size;
    unsigned char md5[MD5_DIGEST_LENGTH];
    int needs_md5;
    int has_md5;
    struct file_info *next;
} file_info_t;

int parse_args(int argc, char *argv[], options_t *opts);
int copy_directory(const options_t *opts, stats_t *stats);
int create_directory_structure(const char *src_path, const char *dest_path);
file_info_t *scan_reference_directory(const options_t *opts);
file_info_t *find_matching_file(file_info_t *ref_files, const char *src_file, const options_t *opts);
int calculate_md5(const char *filename, unsigned char *digest);
int files_identical(const char *file1, const char *file2);
int copy_or_link_file(const char *src, const char *dest, const char *ref, const options_t *opts, stats_t *stats);
int should_overwrite(const char *dest_path, const options_t *opts);
int preserve_file_attributes(const char *src, const char *dest, const preserve_t *preserve);
int parse_preserve_list(const char *preserve_list, preserve_t *preserve);
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size);
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size);
void print_statistics(const stats_t *stats, int human_readable);
void free_file_list(file_info_t *list);
void print_usage(const char *program_name);

int terminal_supports_clear_eol(void);
void print_status_update(const char *format, ...);
void fprint_status_update(FILE *stream, const char *format, ...);
void clear_status_line(void);
void fclear_status_line(FILE *stream);
void print_stats_at_bottom(const char *format, ...);

void setup_signal_handlers(void);
void register_incomplete_file(const char *path);
void unregister_incomplete_file(void);
void cleanup_incomplete_file(void);

#endif
