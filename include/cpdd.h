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

#define MAX_PATH 4096
#define MD5_DIGEST_LENGTH 16
#define BUFFER_SIZE 8192

typedef enum {
    LINK_NONE,
    LINK_HARD,
    LINK_SOFT
} link_type_t;

typedef struct {
    char **sources;
    int source_count;
    char *dest_dir;
    char *ref_dir;
    link_type_t link_type;
    int verbose;
    int recursive;
} options_t;

typedef struct file_info {
    char *path;
    off_t size;
    unsigned char md5[MD5_DIGEST_LENGTH];
    struct file_info *next;
} file_info_t;

int parse_args(int argc, char *argv[], options_t *opts);
int copy_directory(const options_t *opts);
int create_directory_structure(const char *src_path, const char *dest_path);
file_info_t *scan_reference_directory(const char *ref_dir);
file_info_t *find_matching_file(file_info_t *ref_files, const char *src_file);
int calculate_md5(const char *filename, unsigned char *digest);
int files_identical(const char *file1, const char *file2);
int copy_or_link_file(const char *src, const char *dest, const char *ref, link_type_t link_type);
void free_file_list(file_info_t *list);
void print_usage(const char *program_name);

#endif