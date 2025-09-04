/*
 * args.c - Command line argument parsing for cpdd
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
#include <getopt.h>

/* Parse comma-separated preserve attribute list */
int parse_preserve_list(const char *preserve_list, preserve_t *preserve) {
    char *list_copy, *token, *saveptr;
    
    list_copy = strdup(preserve_list);
    if (!list_copy) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return -1;
    }
    
    token = strtok_r(list_copy, ",", &saveptr);
    while (token) {
        if (strcmp(token, "mode") == 0) {
            preserve->mode = 1;
        } else if (strcmp(token, "ownership") == 0) {
            preserve->ownership = 1;
        } else if (strcmp(token, "timestamps") == 0) {
            preserve->timestamps = 1;
        } else if (strcmp(token, "all") == 0) {
            preserve->all = 1;
            preserve->mode = 1;
            preserve->ownership = 1;
            preserve->timestamps = 1;
        } else {
            fprintf(stderr, "Error: Invalid preserve attribute '%s'\n", token);
            fprintf(stderr, "Valid attributes: mode, ownership, timestamps, all\n");
            free(list_copy);
            return -1;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(list_copy);
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] SOURCE... DESTINATION\n", program_name);
    printf("\nCopy files from SOURCE(s) to DESTINATION with optional reference directory linking.\n");
    printf("\nOptions:\n");
    printf("  -r, --reference DIR   Reference directory for content-based linking (can be used multiple times)\n");
    printf("  -L, --hard-link      Create hard links to reference files when content matches (default with -r)\n");
    printf("  -s, --symbolic-link  Create symbolic links to reference files when content matches\n");
    printf("  -R, --recursive      Copy directories recursively\n");
    printf("  -n, --no-clobber     Never overwrite existing files\n");
    printf("  -i, --interactive    Prompt before overwrite\n");
    printf("  -p                   Same as --preserve=mode,ownership,timestamps\n");
    printf("  --preserve[=ATTR_LIST] Preserve the specified attributes\n");
    printf("                       (default: mode,ownership,timestamps)\n");
    printf("                       Additional attributes: all\n");
    printf("  --stats              Show statistics after operation\n");
    printf("  -h, --human-readable Show file sizes in human readable format\n");
    printf("  -v, --verbose        Verbose output (use multiple times for more verbosity: -vv, -vvv)\n");
    printf("  --help               Show this help message\n");
    printf("\nVerbosity levels:\n");
    printf("  -v     Show basic operation progress (level 1)\n");
    printf("  -vv    Show detailed file operations (level 2)\n");
    printf("  -vvv   Show debug information (level 3)\n");
    printf("\nExamples:\n");
    printf("  %s file1.txt file2.txt dest/           # Copy multiple files\n", program_name);
    printf("  %s -R src1/ src2/ dest/                # Copy multiple directories\n", program_name);
    printf("  %s -r ref *.txt dest/                  # Copy matching files with hard links\n", program_name);
    printf("  %s -r ref1 -r ref2 -s src/ dest/       # Multiple reference directories with symbolic links\n", program_name);
    printf("  %s -r ref -s -R src1/ src2/ dest/      # Multiple sources with symbolic links\n", program_name);
    printf("  %s -vv -r ref src/ dest/               # Copy with detailed verbosity\n", program_name);
    printf("\nFile matching priority:\n");
    printf("  1. File size comparison\n");
    printf("  2. MD5 checksum comparison\n");
    printf("  3. Byte-by-byte content comparison\n");
}

int parse_args(int argc, char *argv[], options_t *opts) {
    int opt;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"reference",     required_argument, 0, 'r'},
        {"hard-link",     no_argument,       0, 'L'},
        {"symbolic-link", no_argument,       0, 's'},
        {"recursive",     no_argument,       0, 'R'},
        {"no-clobber",    no_argument,       0, 'n'},
        {"interactive",   no_argument,       0, 'i'},
        {"preserve",      optional_argument, 0, 'P'},
        {"stats",         no_argument,       0, 'S'},
        {"human-readable", no_argument,      0, 'h'},
        {"verbose",       no_argument,       0, 'v'},
        {"help",          no_argument,       0, 'H'},
        {0, 0, 0, 0}
    };
    
    opts->sources = NULL;
    opts->source_count = 0;
    opts->dest_dir = NULL;
    opts->ref_dirs = NULL;
    opts->ref_dir_count = 0;
    opts->link_type = LINK_NONE;
    opts->verbose = 0;
    opts->recursive = 0;
    opts->no_clobber = 0;
    opts->interactive = 0;
    opts->show_stats = 0;
    opts->human_readable = 0;
    opts->preserve.mode = 0;
    opts->preserve.ownership = 0;
    opts->preserve.timestamps = 0;
    opts->preserve.all = 0;
    
    while ((opt = getopt_long(argc, argv, "r:LsRnipvhSH", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'r': {
                opts->ref_dir_count++;
                char **new_ref_dirs = realloc(opts->ref_dirs, opts->ref_dir_count * sizeof(char *));
                if (!new_ref_dirs) {
                    fprintf(stderr, "Error: Memory allocation failed for reference directories\n");
                    return -1;
                }
                opts->ref_dirs = new_ref_dirs;
                opts->ref_dirs[opts->ref_dir_count - 1] = optarg;
                break;
            }
            case 'L':
                if (opts->link_type != LINK_NONE) {
                    fprintf(stderr, "Error: Cannot specify both hard and symbolic links\n");
                    return -1;
                }
                opts->link_type = LINK_HARD;
                break;
            case 's':
                if (opts->link_type != LINK_NONE) {
                    fprintf(stderr, "Error: Cannot specify both hard and symbolic links\n");
                    return -1;
                }
                opts->link_type = LINK_SOFT;
                break;
            case 'R':
                opts->recursive = 1;
                break;
            case 'n':
                if (opts->interactive) {
                    fprintf(stderr, "Error: Cannot specify both --no-clobber and --interactive\n");
                    return -1;
                }
                opts->no_clobber = 1;
                break;
            case 'i':
                if (opts->no_clobber) {
                    fprintf(stderr, "Error: Cannot specify both --no-clobber and --interactive\n");
                    return -1;
                }
                opts->interactive = 1;
                break;
            case 'p':
                opts->preserve.mode = 1;
                opts->preserve.ownership = 1;
                opts->preserve.timestamps = 1;
                break;
            case 'P':
                if (optarg) {
                    if (parse_preserve_list(optarg, &opts->preserve) != 0) {
                        return -1;
                    }
                } else {
                    opts->preserve.mode = 1;
                    opts->preserve.ownership = 1;
                    opts->preserve.timestamps = 1;
                }
                break;
            case 'S':
                opts->show_stats = 1;
                break;
            case 'h':
                opts->human_readable = 1;
                break;
            case 'v':
                opts->verbose++;
                break;
            case 'H':
                print_usage(argv[0]);
                return 0;
            case '?':
                return -1;
            default:
                return -1;
        }
    }
    
    if (optind + 1 >= argc) {
        fprintf(stderr, "Error: At least one SOURCE and DESTINATION required\n");
        print_usage(argv[0]);
        return -1;
    }
    
    /* Last argument is destination, everything else is sources */
    opts->source_count = argc - optind - 1;
    opts->sources = &argv[optind];
    opts->dest_dir = argv[argc - 1];
    
    /* Set hard links as default when reference directory is specified */
    if (opts->ref_dir_count > 0 && opts->link_type == LINK_NONE) {
        opts->link_type = LINK_HARD;
    }
    
    if (opts->link_type != LINK_NONE && opts->ref_dir_count == 0) {
        fprintf(stderr, "Error: Link type specified but no reference directory provided\n");
        return -1;
    }
    
    return 0;
}
