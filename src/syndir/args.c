 /*
   * cpdd/testgen/args.c - Test data generator argument parsing
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

#include "syndir.h"
#include <getopt.h>

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] REF_ROOT SRC_ROOT\n", program_name);
    printf("\nGenerate test directories with configurable file duplication.\n");
    printf("\nArguments:\n");
    printf("  REF_ROOT              Root directory for reference files\n");
    printf("  SRC_ROOT              Root directory for source files\n");
    printf("\nOptions:\n");
    printf("  -f, --files COUNT     Number of files to generate (default: 100)\n");
    printf("  -d, --dirs COUNT      Number of directories to create (default: 10)\n");
    printf("  -p, --percent PCT     Percentage of source files that duplicate reference (0-100, default: 30)\n");
    printf("      --size-p50 SIZE   50th percentile file size in bytes (default: 4096)\n");
    printf("      --size-p95 SIZE   95th percentile file size in bytes (default: 65536)\n");
    printf("      --size-max SIZE   Maximum file size in bytes (default: 1048576)\n");
    printf("      --size-scale FACTOR Scale all file sizes by this factor (default: 1.0)\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s /tmp/ref /tmp/src                    # Default: 100 files, 10 dirs, 30%% duplicates\n", program_name);
    printf("  %s -f 200 -d 20 -p 50 /tmp/ref /tmp/src # 200 files, 20 dirs, 50%% duplicates\n", program_name);
    printf("  %s -v -f 50 -p 80 /tmp/ref /tmp/src     # 50 files, 80%% duplicates, verbose\n", program_name);
    printf("\nDescription:\n");
    printf("  Creates a reference directory with random files, then creates a source\n");
    printf("  directory where a specified percentage of files have identical content\n");
    printf("  to reference files (but different names/locations).\n");
}

int parse_args(int argc, char *argv[], options_t *opts) {
    int opt;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"files",    required_argument, 0, 'f'},
        {"dirs",     required_argument, 0, 'd'},
        {"percent",  required_argument, 0, 'p'},
        {"size-p50", required_argument, 0, '5'},
        {"size-p95", required_argument, 0, '9'},
        {"size-max", required_argument, 0, 'm'},
        {"size-scale", required_argument, 0, 's'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    opts->ref_root = NULL;
    opts->src_root = NULL;
    opts->num_files = 100;
    opts->num_dirs = 10;
    opts->duplicate_percent = 30;
    opts->verbose = 0;
    opts->size_p50 = 4096;      /* Default: 4KB median */
    opts->size_p95 = 65536;     /* Default: 64KB 95th percentile */
    opts->size_p100 = 1048576;  /* Default: 1MB maximum */
    opts->size_scale = 1.0;     /* Default: no scaling */
    
    while ((opt = getopt_long(argc, argv, "f:d:p:5:9:m:s:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'f':
                opts->num_files = atoi(optarg);
                if (opts->num_files <= 0) {
                    fprintf(stderr, "Error: File count must be positive\n");
                    return -1;
                }
                break;
            case 'd':
                opts->num_dirs = atoi(optarg);
                if (opts->num_dirs < 0) {
                    fprintf(stderr, "Error: Directory count must be non-negative\n");
                    return -1;
                }
                break;
            case 'p':
                opts->duplicate_percent = atoi(optarg);
                if (opts->duplicate_percent < 0 || opts->duplicate_percent > 100) {
                    fprintf(stderr, "Error: Duplicate percentage must be 0-100\n");
                    return -1;
                }
                break;
            case '5':
                opts->size_p50 = (size_t)atol(optarg);
                if (opts->size_p50 == 0) {
                    fprintf(stderr, "Error: 50th percentile size must be positive\n");
                    return -1;
                }
                break;
            case '9':
                opts->size_p95 = (size_t)atol(optarg);
                if (opts->size_p95 == 0) {
                    fprintf(stderr, "Error: 95th percentile size must be positive\n");
                    return -1;
                }
                break;
            case 'm':
                opts->size_p100 = (size_t)atol(optarg);
                if (opts->size_p100 == 0) {
                    fprintf(stderr, "Error: Maximum size must be positive\n");
                    return -1;
                }
                break;
            case 's':
                opts->size_scale = atof(optarg);
                if (opts->size_scale <= 0.0) {
                    fprintf(stderr, "Error: Size scale factor must be positive\n");
                    return -1;
                }
                break;
            case 'v':
                opts->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 1;
            case '?':
                return -1;
            default:
                return -1;
        }
    }
    
    if (optind + 2 != argc) {
        fprintf(stderr, "Error: REF_ROOT and SRC_ROOT arguments required\n");
        print_usage(argv[0]);
        return -1;
    }
    
    /* Validate size percentiles are in ascending order */
    if (opts->size_p50 > opts->size_p95 || opts->size_p95 > opts->size_p100) {
        fprintf(stderr, "Error: Size percentiles must be in ascending order (p50 <= p95 <= p100)\n");
        return -1;
    }
    
    opts->ref_root = argv[optind];
    opts->src_root = argv[optind + 1];
    
    return 0;
}