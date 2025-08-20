#ifndef TESTGEN_H
#define TESTGEN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define MAX_PATH 4096
#define MAX_CONTENT_SIZE 8192
#define MIN_CONTENT_SIZE 10

typedef struct {
    char *ref_root;
    char *src_root;
    int num_files;
    int num_dirs;
    int duplicate_percent;
    int verbose;
} testgen_options_t;

typedef struct file_entry {
    char *path;
    char *content;
    size_t content_size;
    struct file_entry *next;
} file_entry_t;

int parse_testgen_args(int argc, char *argv[], testgen_options_t *opts);
int generate_test_data(const testgen_options_t *opts);
int create_reference_directory(const char *root, int num_files, int num_dirs, 
                              file_entry_t **file_list, int verbose);
int create_source_directory(const char *root, int num_files, int num_dirs,
                           file_entry_t *ref_files, int duplicate_percent, int verbose);
char *generate_random_content(size_t size);
char *generate_random_filename(const char *prefix);
int create_directory_tree(const char *root, int num_dirs);
void free_file_list(file_entry_t *list);
void print_testgen_usage(const char *program_name);

#endif