/* SYNDIR - Synthetic directory generator for testing CPDD */
#ifndef SYNDIR_H
#define SYNDIR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Path and content size limits */
#define MAX_PATH 16384
#define MAX_CONTENT_SIZE 8192
#define MIN_CONTENT_SIZE 10

/* Test data generation options */
typedef struct {
    char *ref_root;        /* Reference directory path */
    char *src_root;        /* Source directory path */
    int num_files;         /* Total number of files to generate */
    int num_dirs;          /* Number of subdirectories to create */
    int duplicate_percent; /* Percentage of files that should be duplicates */
    int verbose;           /* Verbose output */
    size_t size_p50;       /* 50th percentile file size (median) */
    size_t size_p95;       /* 95th percentile file size */
    size_t size_p100;      /* 100th percentile file size (maximum) */
    double size_scale;     /* Scale factor for file sizes (default 1.0) */
    unsigned int seed;     /* Random seed for reproducible generation */
} options_t;

/* File entry for tracking generated files */
typedef struct file_entry {
    char *path;            /* Relative path to file */
    char *content;         /* File content */
    size_t content_size;   /* Size of content in bytes */
    struct file_entry *next; /* Next entry in linked list */
} file_entry_t;

/* Command line parsing and main logic */
int parse_args(int argc, char *argv[], options_t *opts);
int generate_test_data(const options_t *opts);

/* Directory and file generation */
int create_reference_directory(const char *root, int num_files, int num_dirs, 
                              file_entry_t **file_list, const options_t *opts);
int create_source_directory(const char *root, int num_files, int num_dirs,
                           file_entry_t *ref_files, const options_t *opts);
int create_directory_tree(const char *root, int num_dirs);

/* Content generation utilities */
char *generate_random_content(size_t size);
char *generate_random_filename(const char *prefix);
size_t generate_file_size(size_t p50, size_t p95, size_t p100);

/* Memory management and utilities */
void free_file_list(file_entry_t *list);
void print_usage(const char *program_name);

#endif