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

#include "testgen.h"
#include <getopt.h>

void print_testgen_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] REF_ROOT SRC_ROOT\n", program_name);
    printf("\nGenerate test directories with configurable file duplication.\n");
    printf("\nArguments:\n");
    printf("  REF_ROOT              Root directory for reference files\n");
    printf("  SRC_ROOT              Root directory for source files\n");
    printf("\nOptions:\n");
    printf("  -f, --files COUNT     Number of files to generate (default: 100)\n");
    printf("  -d, --dirs COUNT      Number of directories to create (default: 10)\n");
    printf("  -p, --percent PCT     Percentage of source files that duplicate reference (0-100, default: 30)\n");
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

int parse_testgen_args(int argc, char *argv[], testgen_options_t *opts) {
    int opt;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"files",    required_argument, 0, 'f'},
        {"dirs",     required_argument, 0, 'd'},
        {"percent",  required_argument, 0, 'p'},
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
    
    while ((opt = getopt_long(argc, argv, "f:d:p:vh", long_options, &option_index)) != -1) {
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
            case 'v':
                opts->verbose = 1;
                break;
            case 'h':
                print_testgen_usage(argv[0]);
                return 1;
            case '?':
                return -1;
            default:
                return -1;
        }
    }
    
    if (optind + 2 != argc) {
        fprintf(stderr, "Error: REF_ROOT and SRC_ROOT arguments required\n");
        print_testgen_usage(argv[0]);
        return -1;
    }
    
    opts->ref_root = argv[optind];
    opts->src_root = argv[optind + 1];
    
    return 0;
}