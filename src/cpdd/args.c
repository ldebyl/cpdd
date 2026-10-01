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

/* Parse comma-separated preserve attribute list, setting each named
 * attribute to value (1 for --preserve, 0 for --no-preserve). */
int parse_preserve_list(const char *preserve_list, preserve_t *preserve, int value) {
    char *list_copy, *token, *saveptr;

    list_copy = strdup(preserve_list);
    if (!list_copy) {
        print_error("Memory allocation failed");
        return -1;
    }

    token = strtok_r(list_copy, ",", &saveptr);
    while (token) {
        if (strcmp(token, "mode") == 0) {
            preserve->mode = value;
        } else if (strcmp(token, "ownership") == 0) {
            preserve->ownership = value ? PRESERVE_EXPLICIT : 0;
        } else if (strcmp(token, "atime") == 0) {
            preserve->atime = value;
        } else if (strcmp(token, "mtime") == 0) {
            preserve->mtime = value;
        } else if (strcmp(token, "timestamps") == 0) {
            preserve->atime = value;
            preserve->mtime = value;
        } else if (strcmp(token, "all") == 0) {
            preserve->mode = value;
            if (!value)
                preserve->ownership = 0;
            else if (!preserve->ownership)
                preserve->ownership = PRESERVE_IMPLICIT;
            preserve->atime = value;
            preserve->mtime = value;
        } else if (strcmp(token, "ctime") == 0 || strcmp(token, "btime") == 0 ||
                   strcmp(token, "crtime") == 0) {
            print_error("'%s' cannot be preserved: the kernel sets it, not the caller", token);
            fprintf(stderr, "Every copy gets a %s of the time it was written.\n", token);
            free(list_copy);
            return -1;
        } else {
            print_error("Invalid preserve attribute '%s'", token);
            fprintf(stderr, "Valid attributes: mode, ownership, atime, mtime, timestamps, all\n");
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
    printf("  -r, --reference DIR    Reference directory for content-based linking (can be used multiple times)\n");
    printf("  -L, --hard-link        Create hard links to reference files when content matches (default with -r)\n");
    printf("  -s, --symbolic-link    Create symbolic links to reference files when content matches\n");
    printf("      --hard-link-source     Hard link unmatched files back to the source instead of copying\n");
    printf("      --symbolic-link-source Symlink unmatched files back to the source instead of copying\n");
    printf("  -R, --recursive        Copy directories recursively\n");
    printf("  -a, --archive          Same as -R --no-dereference --preserve=all\n");
    printf("  --prune-empty-dirs     Don't create directories that would end up empty, including ones\n");
    printf("                           whose files were all skipped (e.g. as duplicates with -N)\n");
    printf("  -n, --no-clobber       Never overwrite existing files\n");
    printf("  -i, --interactive      Prompt before overwrite\n");
    printf("  -u, --update           Overwrite only if source is newer than destination\n");
    printf("  --dry-run              Show what would be done without actually doing it\n");
    printf("  -N, --only-new         Only copy files that don't exist in reference directories\n");
    printf("  -p                     Same as --preserve=mode,ownership,timestamps\n");
    printf("  --preserve[=ATTR_LIST] Preserve the specified attributes (no list: mode,ownership,timestamps)\n");
    printf("                           ATTR_LIST: mode, ownership, atime, mtime, timestamps (=atime,mtime), all\n");
    printf("  --no-preserve=ATTR_LIST  Don't preserve the specified attributes (e.g. --no-preserve=mtime)\n");
    printf("  --stats                Show statistics after operation\n");
    printf("  --block-size SIZE      I/O block size (default: auto-detect from filesystem)\n");
    printf("                           SIZE can be bytes or with suffix K, M, G (e.g., 64K, 1M)\n");
    printf("  -m, --match-name       Match on filename in addition to size\n");
    printf("  --no-verify            Skip content comparison (requires --match-name)\n");
    printf("  --min-size SIZE        Minimum file size for duplicate matching (default: 1)\n");
    printf("  --no-dereference       Don't follow symbolic links\n");
    printf("  --skip-symlinks        Skip symbolic links entirely\n");
    printf("  --log-processed FILE   Resume-aware log: read FILE if it exists (skip sources whose path+size+mtime\n");
    printf("                           still match), then append a record for each newly processed source.\n");
    printf("                           Line-buffered and flushed per record so it survives interrupts.\n");
    printf("  --cache-file FILE      Persist the scanned reference index to FILE and reuse it on later runs\n");
    printf("                           against the same reference directories, instead of rescanning them.\n");
    printf("                           Checkpointed periodically during a long run, not just at the end.\n");
    printf("  --cache-ttl SECONDS    Treat a directory's cached entry as stale after SECONDS (default: no\n");
    printf("                           expiry -- reused until removed or replaced). Requires --cache-file.\n");
    printf("  -h, --human-readable   Show file sizes in human readable format\n");
    printf("  -v, --verbose          Verbose output (use multiple times for more verbosity: -vv, -vvv)\n");
    printf("  --help                 Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s file1.txt file2.txt dest/           # Copy multiple files\n", program_name);
    printf("  %s -R src1/ src2/ dest/                # Copy multiple directories\n", program_name);
    printf("  %s -r ref *.txt dest/                  # Copy matching files with hard links\n", program_name);
    printf("  %s -r ref1 -r ref2 -s src/ dest/       # Multiple reference directories with symbolic links\n", program_name);
    printf("  %s -r ref -s -R src1/ src2/ dest/      # Multiple sources with symbolic links\n", program_name);
    printf("  %s -vv -r ref src/ dest/               # Copy with detailed verbosity\n", program_name);
}

int parse_args(int argc, char *argv[], options_t *opts) {
    int opt;
    int option_index = 0;
    int archive = 0;

    static struct option long_options[] = {
        {"reference",     required_argument, 0, 'r'},
        {"hard-link",     no_argument,       0, 'L'},
        {"symbolic-link", no_argument,       0, 's'},
        {"recursive",     no_argument,       0, 'R'},
        {"archive",       no_argument,       0, 'a'},
        {"no-clobber",    no_argument,       0, 'n'},
        {"interactive",   no_argument,       0, 'i'},
        {"update",        no_argument,       0, 'u'},
        {"dry-run",       no_argument,       0, 'D'},
        {"only-new",      no_argument,       0, 'N'},
        {"preserve",      optional_argument, 0, 'P'},
        {"no-preserve",   required_argument, 0, 1006},
        {"stats",         no_argument,       0, 'S'},
        {"block-size",    required_argument, 0, 'B'},
        {"match-name",    no_argument,       0, 'm'},
        {"no-verify",     no_argument,       0, 'V'},
        {"no-dereference", no_argument,      0, 'd'},
        {"skip-symlinks", no_argument,       0, 'k'},
        {"hard-link-source",     no_argument, 0, 1001},
        {"symbolic-link-source", no_argument, 0, 1002},
        {"min-size",      required_argument, 0, 'M'},
        {"log-processed", required_argument, 0, 1003},
        {"cache-file",    required_argument, 0, 1004},
        {"cache-ttl",     required_argument, 0, 1005},
        {"prune-empty-dirs", no_argument,    0, 1007},
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
    opts->source_link_type = LINK_NONE;
    opts->verbose = 0;
    opts->recursive = 0;
    opts->no_clobber = 0;
    opts->interactive = 0;
    opts->update = 0;
    opts->dry_run = 0;
    opts->only_new = 0;
    opts->show_stats = 0;
    opts->human_readable = 0;
    opts->match_name = 0;
    opts->no_verify = 0;
    opts->no_dereference = 0;
    opts->skip_symlinks = 0;
    opts->prune_empty_dirs = 0;
    opts->block_size = 0;  /* 0 = auto-detect */
    opts->min_size = 1;    /* Default: skip empty files */
    opts->preserve.mode = 0;
    opts->preserve.ownership = 0;
    opts->preserve.atime = 0;
    opts->preserve.mtime = 0;
    opts->log_file = NULL;
    opts->log_fp = NULL;
    opts->skip_set = NULL;
    opts->cache_file = NULL;
    opts->cache_ttl = 0;    /* 0/negative: no expiry */
    g_verbose = 0; // Global verbosity level for logging macros

    while ((opt = getopt_long(argc, argv, "r:LsRniuNpavhmPSH", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'r': {
                opts->ref_dir_count++;
                char **new_ref_dirs = realloc(opts->ref_dirs, opts->ref_dir_count * sizeof(char *));
                if (!new_ref_dirs) {
                    print_error("Memory allocation failed for reference directories");
                    return -1;
                }
                opts->ref_dirs = new_ref_dirs;
                opts->ref_dirs[opts->ref_dir_count - 1] = optarg;
                break;
            }
            case 'L':
                if (opts->link_type != LINK_NONE) {
                    print_error("Cannot specify both hard and symbolic links");
                    return -1;
                }
                opts->link_type = LINK_HARD;
                break;
            case 's':
                if (opts->link_type != LINK_NONE) {
                    print_error("Cannot specify both hard and symbolic links");
                    return -1;
                }
                opts->link_type = LINK_SOFT;
                break;
            case 'R':
                opts->recursive = 1;
                break;
            case 'n':
                if (opts->interactive || opts->update) {
                    print_error("Cannot specify both --no-clobber and other overwrite options");
                    return -1;
                }
                opts->no_clobber = 1;
                break;
            case 'i':
                if (opts->no_clobber || opts->update) {
                    print_error("Cannot specify both --interactive and other overwrite options");
                    return -1;
                }
                opts->interactive = 1;
                break;
            case 'u':
                if (opts->no_clobber || opts->interactive) {
                    print_error("Cannot specify both --update and other overwrite options");
                    return -1;
                }
                opts->update = 1;
                break;
            case 'D':
                opts->dry_run = 1;
                break;
            case 'N':
                opts->only_new = 1;
                break;
            case 'p':
                parse_preserve_list("all", &opts->preserve, 1);
                break;
            case 'a':
                /* As cp -a: -R --no-dereference --preserve=all. The
                 * no-dereference part is applied after parsing, so an
                 * explicit --skip-symlinks can override it. */
                archive = 1;
                opts->recursive = 1;
                parse_preserve_list("all", &opts->preserve, 1);
                break;
            case 'P':
                if (parse_preserve_list(optarg ? optarg : "all",
                                        &opts->preserve, 1) != 0) {
                    return -1;
                }
                break;
            case 1006:
                if (parse_preserve_list(optarg, &opts->preserve, 0) != 0) {
                    return -1;
                }
                break;
            case 'S':
                opts->show_stats = 1;
                break;
            case 'B': {
                opts->block_size = parse_size(optarg);
                if (opts->block_size == 0) {
                    print_error("Invalid block size '%s'", optarg);
                    return -1;
                }
                break;
            }
            case 'm':
                opts->match_name = 1;
                break;
            case 'V':
                opts->no_verify = 1;
                break;
            case 'd':
                opts->no_dereference = 1;
                break;
            case 'k':
                opts->skip_symlinks = 1;
                break;
            case 1001:
                if (opts->source_link_type != LINK_NONE) {
                    print_error("Cannot specify both --hard-link-source and --symbolic-link-source");
                    return -1;
                }
                opts->source_link_type = LINK_HARD;
                break;
            case 1002:
                if (opts->source_link_type != LINK_NONE) {
                    print_error("Cannot specify both --hard-link-source and --symbolic-link-source");
                    return -1;
                }
                opts->source_link_type = LINK_SOFT;
                break;
            case 1003:
                opts->log_file = optarg;
                break;
            case 1004:
                opts->cache_file = optarg;
                break;
            case 1005:
                opts->cache_ttl = atol(optarg);
                if (opts->cache_ttl < 0) {
                    print_error("Invalid cache TTL '%s'", optarg);
                    return -1;
                }
                break;
            case 1007:
                opts->prune_empty_dirs = 1;
                break;
            case 'M':
                opts->min_size = (off_t)atoll(optarg);
                if (opts->min_size < 0) {
                    print_error("Invalid minimum size '%s'", optarg);
                    return -1;
                }
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
        print_error("At least one SOURCE and DESTINATION required");
        print_usage(argv[0]);
        return -1;
    }

    /* Last argument is destination, everything else is sources */
    opts->source_count = argc - optind - 1;
    opts->sources = &argv[optind];
    opts->dest_dir = argv[argc - 1];

    /* Validate --only-new requirements */
    if (opts->only_new) {
        if (opts->ref_dir_count == 0) {
            print_error("--only-new requires at least one reference directory (-r)");
            return -1;
        }
    }

    /* Validate --no-verify requirements */
    if (opts->no_verify && !opts->match_name) {
        print_error("--no-verify requires --match-name");
        return -1;
    }

    /* Validate --cache-ttl requirements */
    if (opts->cache_ttl > 0 && !opts->cache_file) {
        print_error("--cache-ttl requires --cache-file");
        return -1;
    }
    if (opts->cache_file && opts->ref_dir_count == 0) {
        print_error("--cache-file requires at least one reference directory (-r)");
        return -1;
    }

    if (archive && !opts->skip_symlinks) {
        opts->no_dereference = 1;
    }

    /* Validate symlink options are mutually exclusive */
    if (opts->no_dereference && opts->skip_symlinks) {
        print_error("--no-dereference and --skip-symlinks are mutually exclusive");
        return -1;
    }

    /* Set hard links as default when reference directory is specified (unless --only-new) */
    if (opts->ref_dir_count > 0 && opts->link_type == LINK_NONE && !opts->only_new) {
        opts->link_type = LINK_HARD;
    }

    if (opts->link_type != LINK_NONE && opts->ref_dir_count == 0) {
        print_error("Link type specified but no reference directory provided");
        return -1;
    }

    /* Set global verbosity for logging macros */
    g_verbose = opts->verbose;

    return 0;
}
