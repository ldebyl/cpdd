/*
   * cpdd/copy.c - Content-based copy with deduplication
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

// Path of the file currently being copied (for cleanup on signal)
static char *current_incomplete_file = NULL;

/* File operation wrappers that handle dry run */
static int file_unlink(const char *path, const options_t *opts) {
    if (opts->dry_run) {
        return 0;  /* Pretend success */
    }
    return unlink(path);
}

static int file_link(const char *oldpath, const char *newpath, const options_t *opts) {
    if (opts->dry_run) {
        return 0;  /* Pretend success */
    }
    return link(oldpath, newpath);
}

static int file_symlink(const char *target, const char *linkpath, const options_t *opts) {
    if (opts->dry_run) {
        return 0;  /* Pretend success */
    }
    return symlink(target, linkpath);
}

static int file_copy(const char *src, const char *dest, const options_t *opts, struct stat *src_st) {
    if (opts->dry_run) {
        return src_st->st_size;  /* Return bytes that would be copied */
    }
    
    /* Get optimal block size for this file */
    size_t block_size = get_optimal_block_size(src, opts);
    
    /* Show block size info in verbose mode if auto-detected */
    if (opts->verbose && opts->block_size == 0) {
        VERBOSE("Using block size %zu bytes for %s\n", block_size, src);
    }
    
    /* Actual file copy implementation */
    int src_fd, dest_fd;
    char *buffer;
    ssize_t bytes_read, bytes_written;
    
    /* Allocate buffer based on optimal block size */
    buffer = malloc(block_size);
    if (!buffer) {
        return -1;
    }
    
    // Open source file for reading
    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        free(buffer);
        return -1;
    }

    // Open destination file for writing (create/truncate)
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, src_st->st_mode);
    if (dest_fd < 0) {
        close(src_fd);
        free(buffer);
        return -1;
    }
    
    // Register the incomplete file for cleanup on signals
    register_incomplete_file(dest);
    
    // Perform the copy
    while ((bytes_read = read(src_fd, buffer, block_size)) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            close(src_fd);
            close(dest_fd);
            free(buffer);
            cleanup_incomplete_file();
            return -1;
        }
    }
    
    close(src_fd);
    close(dest_fd);
    free(buffer);
    
    // Unregister the incomplete file since copy succeeded
    unregister_incomplete_file();
    
    if (bytes_read < 0) {
        return -1;
    }
    
    return src_st->st_size;  /* Return bytes copied */
}

// Signal handler to clean up incomplete file on termination
static void signal_handler(int sig) {
    cleanup_incomplete_file();
    exit(128 + sig);
}

// Sets up signal handlers for SIGINT and SIGTERM
void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGPIPE, signal_handler);
}

// Registers the path of the file currently being copied
void register_incomplete_file(const char *path) {
    if (current_incomplete_file) {
        free(current_incomplete_file);
    }
    current_incomplete_file = strdup(path);
}

// Unregisters the current incomplete file without deleting it
void unregister_incomplete_file(void) {
    if (current_incomplete_file) {
        free(current_incomplete_file);
        current_incomplete_file = NULL;
    }
}

// Cleans up the incomplete file if it exists
void cleanup_incomplete_file(void) {
    if (current_incomplete_file) {
        unlink(current_incomplete_file);
        unregister_incomplete_file();
    }
}

/* Copy a symbolic link itself (not its target) */
static int copy_symlink(const char *src, const char *dest, const options_t *opts) {
    char link_target[MAX_PATH];
    ssize_t len;

    /* Read the symlink target */
    len = readlink(src, link_target, sizeof(link_target) - 1);
    if (len == -1) {
        fprintf(stderr, "Error: Cannot read symlink %s: %s\n", src, strerror(errno));
        return -1;
    }
    link_target[len] = '\0';

    if (opts->dry_run) {
        if (opts->verbose) {
            print_verbose("[DRY RUN] %s[SYMLINK]%s %s %s->%s %s %s(target: %s)%s",
                        color_cyan(), color_reset(),
                        src,
                        color_dim(), color_reset(),
                        dest,
                        color_dim(), link_target, color_reset());
        }
        return 0;
    }

    /* Remove destination if it exists */
    unlink(dest);

    /* Create the new symlink */
    if (symlink(link_target, dest) != 0) {
        fprintf(stderr, "Error: Cannot create symlink %s: %s\n", dest, strerror(errno));
        return -1;
    }

    if (opts->verbose) {
        print_verbose("%s[SYMLINK]%s %s %s->%s %s %s(target: %s)%s",
                    color_cyan(), color_reset(),
                    src,
                    color_dim(), color_reset(),
                    dest,
                    color_dim(), link_target, color_reset());
    }

    return 0;
}

/* Given source and destination files, determine if destination should be overwritten
 * by checking if destination exists, user preferences for --no-clobber, --update,
 * and optionally asking interactively */
int should_overwrite(const char *src_path, const char *dest_path, const options_t *opts) {
    struct stat dest_st, src_st;
    
    if (stat(dest_path, &dest_st) != 0) {
        return 1; /* Destination doesn't exist, safe to copy */
    }
    
    if (opts->no_clobber) {
        return 0;
    }
    
    if (opts->update) {
        if (stat(src_path, &src_st) != 0) {
            return 0; /* Can't stat source, don't overwrite */
        }
        /* Only overwrite if source is newer than destination */
        return (src_st.st_mtime > dest_st.st_mtime);
    }
    
    if (opts->interactive) {
        char response;
        printf("overwrite '%s'? ", dest_path);
        fflush(stdout);
        
        if (scanf(" %c", &response) == 1) {
            return (response == 'y' || response == 'Y');
        }
        return 0;
    }
    
    return 1; /* Default: overwrite */
}

/* Given a source and destination, propagates attributes between them */
int preserve_file_attributes(const char *src, const char *dest, const preserve_t *preserve) {
    struct stat src_st;
    struct utimbuf times;
    
    if (stat(src, &src_st) != 0) {
        return -1;
    }
    
    if (preserve->mode) {
        if (chmod(dest, src_st.st_mode) != 0) {
            return -1;
        }
    }
    
    if (preserve->ownership) {
        if (chown(dest, src_st.st_uid, src_st.st_gid) != 0) {
            return -1;
        }
    }
    
    if (preserve->timestamps) {
        times.actime = src_st.st_atime;
        times.modtime = src_st.st_mtime;
        if (utime(dest, &times) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/* Parse size string with K/M/G suffixes (like dd bs= parameter) */
size_t parse_size(const char *size_str) {
    char *endptr;
    unsigned long long value = strtoull(size_str, &endptr, 10);
    
    if (value == 0 || endptr == size_str) {
        return 0;  /* Invalid number */
    }
    
    /* Handle suffix */
    if (*endptr != '\0') {
        switch (*endptr) {
            case 'k':
            case 'K':
                value *= 1024;
                break;
            case 'm':
            case 'M':
                value *= 1024 * 1024;
                break;
            case 'g':
            case 'G':
                value *= 1024 * 1024 * 1024;
                break;
            default:
                return 0;  /* Invalid suffix */
        }
    }
    
    /* Sanity check - block size should be reasonable */
    if (value < 512 || value > 16 * 1024 * 1024) {  /* 512B to 16MB */
        return 0;
    }
    
    return (size_t)value;
}

/* Get optimal block size for file I/O */
size_t get_optimal_block_size(const char *filename, const options_t *opts) {
    if (opts->block_size > 0) {
        return opts->block_size;  /* User override */
    }
    
    struct stat st;
    if (stat(filename, &st) == 0 && st.st_blksize > 0) {
        /* Use filesystem's preferred I/O block size */
        return (size_t)st.st_blksize;
    }
    
    return BUFFER_SIZE;  /* Fallback to default */
}

/* Formats byte counts with human-readable quantities */
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size) {
    if (!human_readable) {
        snprintf(buffer, buffer_size, "%lld bytes", (long long)bytes);
        return;
    }
    
    const char *units[] = {"B", "K", "M", "G", "T", "P"};
    int unit = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit < 5) {
        size /= 1024.0;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(buffer, buffer_size, "%lld%s", (long long)bytes, units[unit]);
    } else if (size >= 100.0) {
        snprintf(buffer, buffer_size, "%.0f%s", size, units[unit]);
    } else if (size >= 10.0) {
        snprintf(buffer, buffer_size, "%.1f%s", size, units[unit]);
    } else {
        snprintf(buffer, buffer_size, "%.2f%s", size, units[unit]);
    }
}

/* String formats statistics from a copy operation. */
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size) {
    char total_bytes_str[32];
    off_t total_bytes = stats->bytes_copied + stats->bytes_hard_linked + stats->bytes_soft_linked;
    int total_files = stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;
    
    format_bytes(total_bytes, human_readable, total_bytes_str, sizeof(total_bytes_str));
    
    snprintf(buffer, buffer_size, "Files: %d copied, %d linked, %d skipped | Total: %d files (%s)", 
             stats->files_copied, stats->files_hard_linked + stats->files_soft_linked, 
             stats->files_skipped, total_files, total_bytes_str);
}


/* Prints final statistics */
void print_statistics(const stats_t *stats, int human_readable) {
    char copied_bytes[32], linked_bytes[32], soft_linked_bytes[32];
    
    format_bytes(stats->bytes_copied, human_readable, copied_bytes, sizeof(copied_bytes));
    format_bytes(stats->bytes_hard_linked, human_readable, linked_bytes, sizeof(linked_bytes));
    format_bytes(stats->bytes_soft_linked, human_readable, soft_linked_bytes, sizeof(soft_linked_bytes));
    
    fprintf(stderr, "\n%sStatistics:%s\n", color_cyan(), color_reset());
    fprintf(stderr, "  Files copied:      %s%d%s (%s)\n", color_green(), stats->files_copied, color_reset(), copied_bytes);
    fprintf(stderr, "  Files hard linked: %s%d%s (%s)\n", color_blue(), stats->files_hard_linked, color_reset(), linked_bytes);
    fprintf(stderr, "  Files soft linked: %s%d%s (%s)\n", color_blue(), stats->files_soft_linked, color_reset(), soft_linked_bytes);
    fprintf(stderr, "  Files skipped:     %s%d%s\n", color_yellow(), stats->files_skipped, color_reset());

    off_t total_bytes = stats->bytes_copied + stats->bytes_hard_linked + stats->bytes_soft_linked;
    int total_files = stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;
    char total_bytes_str[32];
    format_bytes(total_bytes, human_readable, total_bytes_str, sizeof(total_bytes_str));

    fprintf(stderr, "  Total files:       %s%d%s (%s)\n", color_cyan(), total_files, color_reset(), total_bytes_str);

    /* Display cache statistics if any comparisons were made */
    if (stats->files_compared > 0) {
        fprintf(stderr, "\n%sCache Statistics:%s\n", color_cyan(), color_reset());
        fprintf(stderr, "  Files compared:   %d\n", stats->files_compared);
        fprintf(stderr, "  Fast rejections:  %d (%.1f%%)\n", stats->cache_hits,
               (double)stats->cache_hits / stats->files_compared * 100.0);

        double avg_cache_depth = (double)stats->total_cache_depth / stats->files_compared;
        fprintf(stderr, "  Cache depth - avg: %.1f, min: %d, max: %d blocks\n",
               avg_cache_depth, stats->min_cache_depth, stats->max_cache_depth);
    }

    /* Display size collision statistics */
    if (stats->total_ref_files > 0 && stats->total_source_files > 0) {
        fprintf(stderr, "\n%sSize Collision Statistics:%s\n", color_cyan(), color_reset());
        fprintf(stderr, "  Reference files:  %d (with %d unique sizes)\n",
               stats->total_ref_files, stats->unique_ref_sizes);
        fprintf(stderr, "  Source files:     %d (%d had size matches)\n",
               stats->total_source_files, stats->files_with_size_matches);

        double ref_unique_pct = (double)stats->unique_ref_sizes / stats->total_ref_files * 100.0;
        double size_match_pct = (double)stats->files_with_size_matches / stats->total_source_files * 100.0;

        fprintf(stderr, "  Unique ref sizes: %.1f%% (higher = fewer opportunities for matching)\n", ref_unique_pct);
        fprintf(stderr, "  Size matches:     %.1f%% (files that proceeded to content comparison)\n", size_match_pct);
    }
}

/* Ensures that the directory structure for dest_path exists,
 * creating directories as needed, using the permissions of
 * the source path or its parent directory */
/* Create directory with proper permissions, handling dry run */
static int create_directory(const char *path, mode_t mode, const options_t *opts) {
    if (opts->dry_run) {
        return 0; /* Pretend success */
    }
    
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* Create parent directories recursively (mkdir -p style) */
static int create_parent_directories(const char *path, mode_t default_mode, const options_t *opts) {
    char dir_path[MAX_PATH];
    char *slash;
    struct stat st;
    
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    /* Find the last slash to get parent directory */
    slash = strrchr(dir_path, '/');
    if (!slash) {
        return 0; /* No parent directory needed */
    }
    
    *slash = '\0'; /* Truncate to parent directory */
    
    /* Check if parent already exists */
    if (stat(dir_path, &st) == 0) {
        return 0; /* Parent exists */
    }
    
    /* Recursively create parent's parent */
    if (create_parent_directories(dir_path, default_mode, opts) != 0) {
        return -1;
    }
    
    /* Create this directory */
    if (create_directory(dir_path, default_mode, opts) != 0) {
        return -1;
    }
    
    return 0;
}

/* Create directory structure for destination, preserving source permissions */
int create_directory_structure(const char *src_path, const char *dest_path, const options_t *opts) {
    struct stat src_st;
    
    if (stat(src_path, &src_st) != 0) {
        return -1;
    }
    
    if (S_ISDIR(src_st.st_mode)) {
        /* Source is directory - create destination directory */
        if (create_directory(dest_path, src_st.st_mode, opts) != 0) {
            return -1;
        }
        
        /* Preserve attributes if requested */
        if (opts->preserve.mode || opts->preserve.ownership || opts->preserve.timestamps) {
            if (!opts->dry_run) {
                preserve_file_attributes(src_path, dest_path, &opts->preserve);
            }
        }
    } else {
        /* Source is file - create parent directory for destination file */
        if (create_parent_directories(dest_path, 0755, opts) != 0) {
            return -1;
        }
    }
    
    return 0;
}

// Copies a file from src to dest, optionally creating a hard or soft link
int copy_or_link_file(const char *src, const char *dest, ref_files_t *ref_files, const options_t *opts, stats_t *stats) {
    struct stat src_st;
    file_info_t *matching_file = NULL;

    /* Check if we should skip symlinks entirely */
    if (opts->skip_symlinks) {
        struct stat lstat_st;
        if (lstat(src, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
            if (opts->verbose >= 2) {
                print_verbose("%s[SKIP]%s %s %s(symlink)%s",
                            color_yellow(), color_reset(),
                            src,
                            color_dim(), color_reset());
            }
            stats->files_skipped++;
            return 0;
        }
    }

    /* Use lstat if not dereferencing, stat otherwise */
    int stat_result;
    if (opts->no_dereference) {
        stat_result = lstat(src, &src_st);

        if (stat_result != 0) {
            fprintf(stderr, "Error: Cannot lstat %s: %s\n", src, strerror(errno));
            return -1;
        }
    } else {
        stat_result = stat(src, &src_st);

        if (stat_result != 0) {
            /* Check if it's a broken symlink */
            struct stat lstat_st;
            if (lstat(src, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                fprintf(stderr, "Warning: Skipping broken symlink: %s\n", src);
                stats->files_skipped++;
                return 0;
            }
            fprintf(stderr, "Error: Cannot stat %s: %s\n", src, strerror(errno));
            return -1;
        }
    }

    /* If it's a symlink and we're not dereferencing, copy the symlink itself */
    if (opts->no_dereference && S_ISLNK(src_st.st_mode)) {
        /* Check if we should overwrite */
        if (!should_overwrite(src, dest, opts)) {
            if (opts->verbose) {
                print_verbose("%s[SKIP]%s %s %s(exists)%s",
                            color_yellow(), color_reset(),
                            dest,
                            color_dim(), color_reset());
            }
            stats->files_skipped++;
            return 0;
        }

        /* Ensure destination directory exists */
        if (create_directory_structure(src, dest, opts) != 0) {
            return -1;
        }

        /* Copy the symlink itself, skip all duplicate detection */
        return copy_symlink(src, dest, opts);
    }

    /* At this point, we either have a regular file or a dereferenced symlink */
    if (!S_ISREG(src_st.st_mode)) {
        if (opts->verbose >= 2) {
            print_verbose("%s[SKIP]%s %s %s(non-regular file)%s",
                        color_yellow(), color_reset(),
                        src,
                        color_dim(), color_reset());
        }
        stats->files_skipped++;
        return 0;
    }

    /* Check if we should overwrite */
    if (!should_overwrite(src, dest, opts)) {
        if (opts->verbose) {
            print_verbose("%s[SKIP]%s %s %s(exists)%s",
                        color_yellow(), color_reset(),
                        dest,
                        color_dim(), color_reset());
        }
        stats->files_skipped++;
        return 0;
    }

    /* Find matching reference file if available */
    matching_file = find_matching_file(ref_files, src, opts, stats);
    if (matching_file) {
        VERBOSE("Found matching reference file for %s: %s\n", src, matching_file->path);
    }

    /* If --only-new is set and file exists in reference, skip it */
    if (opts->only_new && matching_file) {
        if (opts->verbose) {
            print_verbose("%s[SKIP]%s %s %s(in reference: %s)%s",
                        color_yellow(), color_reset(),
                        src,
                        color_dim(), matching_file->path, color_reset());
        }
        stats->files_skipped++;
        return 0;
    }

    /* Ensure destination directory exists */
    if (create_directory_structure(src, dest, opts) != 0) {
        return -1;
    }

    /* Try to create a link to reference file if we found a match */
    if (matching_file && opts->link_type != LINK_NONE) {
        struct stat ref_st;
        
        /* Get the size of the reference file */
        if (stat(matching_file->path, &ref_st) != 0) {
            VERBOSE("Warning: Could not stat reference file %s\n", matching_file->path);
        } else {
            /* Remove destination file if it exists */
            file_unlink(dest, opts);
            
            if (opts->link_type == LINK_HARD) {
                if (file_link(matching_file->path, dest, opts) == 0) {
                    stats->files_hard_linked++;
                    stats->bytes_hard_linked += src_st.st_size;

                    if (opts->verbose) {
                        const char *dry_run_prefix = opts->dry_run ? "[DRY RUN] " : "";
                        print_verbose("%s%s[HARD LINK]%s %s %s<-%s %s %s(source: %s)%s",
                                    dry_run_prefix,
                                    color_blue(), color_reset(),
                                    dest,
                                    color_dim(), color_reset(),
                                    matching_file->path,
                                    color_dim(), src, color_reset());
                    }
                    return 0;
                } else {
                    fprintf(stderr, "Failed to create hard link for %s -> %s: %s\n", matching_file->path, dest, strerror(errno));
                }
            } else if (opts->link_type == LINK_SOFT) {
                if (file_symlink(matching_file->path, dest, opts) == 0) {
                    stats->files_soft_linked++;
                    stats->bytes_soft_linked += src_st.st_size;

                    if (opts->verbose) {
                        const char *dry_run_prefix = opts->dry_run ? "[DRY RUN] " : "";
                        print_verbose("%s%s[SOFT LINK]%s %s %s<-%s %s %s(source: %s)%s",
                                    dry_run_prefix,
                                    color_cyan(), color_reset(),
                                    dest,
                                    color_dim(), color_reset(),
                                    matching_file->path,
                                    color_dim(), src, color_reset());
                    }
                    return 0;
                } else {
                    VERBOSE("Failed to create soft link for %s -> %s: %s\n", matching_file->path, dest, strerror(errno));
                }
            }
        }
    }
    
    /* Fall back to regular copy */
    int bytes_copied = file_copy(src, dest, opts, &src_st);
    if (bytes_copied < 0) {
        return -1;
    }
    
    stats->files_copied++;
    stats->bytes_copied += bytes_copied;
    
    /* Preserve attributes if requested */
    if (opts->preserve.mode || opts->preserve.ownership || opts->preserve.timestamps) {
        if (preserve_file_attributes(src, dest, &opts->preserve) != 0) {
                fprintf(stderr, "Warning: Failed to preserve attributes for %s\n", dest);
        }
    }
    
    if (opts->verbose) {
        const char *dry_run_prefix = opts->dry_run ? "[DRY RUN] " : "";
        print_verbose("%s%s[COPY]%s %s %s->%s %s",
                    dry_run_prefix,
                    color_green(), color_reset(),
                    src,
                    color_dim(), color_reset(),
                    dest);
    }

    return 0;
}

static int copy_directory_recursive(const char *src_path, const char *dest_path, 
                                   ref_files_t *ref_files, const options_t *opts, stats_t *stats) {
    DIR *src_dir;
    struct dirent *entry;
    struct stat st;
    char src_full[MAX_PATH];
    char dest_full[MAX_PATH];
    
    src_dir = opendir(src_path);
    if (!src_dir) {
        fprintf(stderr, "Error: Cannot open source directory %s: %s\n", 
                src_path, strerror(errno));
        return -1;
    }
    
    if (create_directory_structure(src_path, dest_path, opts) != 0) {
        fprintf(stderr, "Error: Cannot create destination directory %s: %s\n", 
                dest_path, strerror(errno));
        closedir(src_dir);
        return -1;
    }
    
    if (opts->preserve.mode || opts->preserve.ownership || opts->preserve.timestamps) {
        if (preserve_file_attributes(src_path, dest_path, &opts->preserve) != 0 && opts->verbose) {
            fprintf(stderr, "Warning: Failed to preserve attributes for directory %s\n", dest_path);
        }
    }
    
    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(src_full, sizeof(src_full), "%s/%s", src_path, entry->d_name);
        snprintf(dest_full, sizeof(dest_full), "%s/%s", dest_path, entry->d_name);

        /* Check if we should skip symlinks entirely */
        if (opts->skip_symlinks) {
            struct stat lstat_st;
            if (lstat(src_full, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                if (opts->verbose >= 2) {
                    print_verbose("%s[SKIP]%s %s %s(symlink)%s",
                                color_yellow(), color_reset(),
                                src_full,
                                color_dim(), color_reset());
                }
                continue;
            }
        }

        /* Use lstat if not dereferencing, stat otherwise */
        int stat_result;
        if (opts->no_dereference) {
            stat_result = lstat(src_full, &st);

            if (stat_result != 0) {
                fprintf(stderr, "Warning: Cannot lstat %s: %s\n", src_full, strerror(errno));
                continue;
            }
        } else {
            stat_result = stat(src_full, &st);

            if (stat_result != 0) {
                /* Check if it's a broken symlink */
                struct stat lstat_st;
                if (lstat(src_full, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                    fprintf(stderr, "Warning: Skipping broken symlink: %s\n", src_full);
                    continue;
                }
                fprintf(stderr, "Warning: Cannot stat %s: %s\n", src_full, strerror(errno));
                continue;
            }
        }

        /* Handle symlinks */
        if (opts->no_dereference && S_ISLNK(st.st_mode)) {
            if (copy_or_link_file(src_full, dest_full, ref_files, opts, stats) != 0) {
                continue;
            }

            if (opts->show_stats) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));

                if (opts->verbose == 0) {
                    print_status_update("%s", stats_buffer);
                } else {
                    print_stats_at_bottom("%s", stats_buffer);
                }
            }
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (opts->recursive) {
                if (copy_directory_recursive(src_full, dest_full, ref_files, opts, stats) != 0) {
                    closedir(src_dir);
                    return -1;
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copy_or_link_file(src_full, dest_full, ref_files, opts, stats) != 0) {
                continue;
            }
            
            if (opts->show_stats) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));
                
                if (opts->verbose == 0) {
                    print_status_update("%s", stats_buffer);
                } else {
                    print_stats_at_bottom("%s", stats_buffer);
                }
            }
        }
    }
    
    closedir(src_dir);
    return 0;
}

int copy_directory(const options_t *opts, stats_t *stats) {
    struct stat dest_st;
    ref_files_t *ref_files = NULL;
    int overall_result = 0;
    int dest_is_dir = 0;
    
    /* Check destination */
    if (stat(opts->dest_dir, &dest_st) == 0) {
        if (S_ISDIR(dest_st.st_mode)) {
            dest_is_dir = 1;
        } else if (S_ISREG(dest_st.st_mode) && opts->source_count > 1) {
            fprintf(stderr, "Error: Cannot copy multiple sources to a regular file\n");
            return -1;
        }
    } else {
        /* Destination doesn't exist - if multiple sources, assume it should be a directory */
        if (opts->source_count > 1) {
            dest_is_dir = 1;
        }
    }
    
    /* Scan reference directories once */
    if (opts->ref_dir_count > 0) {
        VERBOSE("Scanning %d reference directories...\n", opts->ref_dir_count);
        ref_files = scan_reference_directory(opts, stats);
        if (!ref_files) {
            fprintf(stderr, "Warning: No files found in reference directories\n");
        } else {
            // Get the number of reference files from the sorted structure
            int ref_file_count = ref_files->count;
            VERBOSE("Found %d reference files across all directories\n", ref_file_count);
        }
    }
    
    /* Process each source */
    for (int i = 0; i < opts->source_count; i++) {
        struct stat src_st;
        char dest_path[MAX_PATH];
        const char *src_path = opts->sources[i];
        
        if (stat(src_path, &src_st) != 0) {
            fprintf(stderr, "Error: Cannot access source %s: %s\n", 
                    src_path, strerror(errno));
            overall_result = -1;
            continue;
        }
        
        /* Determine destination path */
        if (dest_is_dir || opts->source_count > 1) {
            /* Extract basename from source */
            const char *basename = strrchr(src_path, '/');
            basename = basename ? basename + 1 : src_path;
            snprintf(dest_path, sizeof(dest_path), "%s/%s", opts->dest_dir, basename);
        } else {
            strncpy(dest_path, opts->dest_dir, sizeof(dest_path) - 1);
            dest_path[sizeof(dest_path) - 1] = '\0';
        }
        
        /* Copy source to destination */
        if (S_ISDIR(src_st.st_mode)) {
            if (copy_directory_recursive(src_path, dest_path, ref_files, opts, stats) != 0) {
                overall_result = -1;
            }
        } else {
            if (copy_or_link_file(src_path, dest_path, ref_files, opts, stats) != 0) {
                overall_result = -1;
                continue;
            }
            
            if (opts->show_stats) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));
                
                if (opts->verbose == 0) {
                    print_status_update("%s", stats_buffer);
                } else {
                    print_stats_at_bottom("%s", stats_buffer);
                }
            }
        }
    }
    
    if (ref_files) {
        free_sorted_file_info(ref_files);
    }
    
    return overall_result;
}
