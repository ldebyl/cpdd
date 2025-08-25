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

/* Determins whether a destination file should be overwritten,
 * by firstly determining if the destination exists, whether the
 * user has specified --no-clobber, and optionally interactively
 * asking */
int should_overwrite(const char *dest_path, const options_t *opts) {
    struct stat st;
    
    if (stat(dest_path, &st) != 0) {
        return 1;
    }
    
    if (opts->no_clobber) {
        return 0;
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
    
    return 1;
}

/* Given a source a destination, propogates attributes between them */
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

/* Formats byte counts with human-readable quantities */
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size) {
    if (!human_readable) {
        snprintf(buffer, buffer_size, "%lld", (long long)bytes);
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

/* Prints statistics froma copy operation. if requested */
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size) {
    char total_bytes_str[32];
    off_t total_bytes = stats->bytes_copied + stats->bytes_hard_linked + stats->bytes_soft_linked;
    int total_files = stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;
    
    format_bytes(total_bytes, human_readable, total_bytes_str, sizeof(total_bytes_str));
    
    snprintf(buffer, buffer_size, "Files: %d copied, %d linked, %d skipped | Total: %d files (%s)", 
             stats->files_copied, stats->files_hard_linked + stats->files_soft_linked, 
             stats->files_skipped, total_files, total_bytes_str);
}

void print_statistics(const stats_t *stats, int human_readable) {
    char copied_bytes[32], linked_bytes[32], soft_linked_bytes[32];
    
    format_bytes(stats->bytes_copied, human_readable, copied_bytes, sizeof(copied_bytes));
    format_bytes(stats->bytes_hard_linked, human_readable, linked_bytes, sizeof(linked_bytes));
    format_bytes(stats->bytes_soft_linked, human_readable, soft_linked_bytes, sizeof(soft_linked_bytes));
    
    printf("\nStatistics:\n");
    printf("  Files copied:     %d (%s)\n", stats->files_copied, copied_bytes);
    printf("  Files hard linked: %d (%s)\n", stats->files_hard_linked, linked_bytes);
    printf("  Files soft linked: %d (%s)\n", stats->files_soft_linked, soft_linked_bytes);
    printf("  Files skipped:    %d\n", stats->files_skipped);
    
    off_t total_bytes = stats->bytes_copied + stats->bytes_hard_linked + stats->bytes_soft_linked;
    int total_files = stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;
    char total_bytes_str[32];
    format_bytes(total_bytes, human_readable, total_bytes_str, sizeof(total_bytes_str));
    
    printf("  Total files:      %d (%s)\n", total_files, total_bytes_str);
}

int create_directory_structure(const char *src_path, const char *dest_path) {
    struct stat st;
    char dest_dir[MAX_PATH];
    char *last_slash;
    
    if (stat(src_path, &st) != 0) {
        return -1;
    }
    
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dest_path, st.st_mode) != 0 && errno != EEXIST) {
            return -1;
        }
        return 0;
    }
    
    strncpy(dest_dir, dest_path, sizeof(dest_dir) - 1);
    dest_dir[sizeof(dest_dir) - 1] = '\0';
    
    last_slash = strrchr(dest_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        
        if (access(dest_dir, F_OK) != 0) {
            struct stat parent_st;
            char src_dir[MAX_PATH];
            
            strncpy(src_dir, src_path, sizeof(src_dir) - 1);
            src_dir[sizeof(src_dir) - 1] = '\0';
            
            last_slash = strrchr(src_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                if (stat(src_dir, &parent_st) == 0) {
                    if (mkdir(dest_dir, parent_st.st_mode) != 0 && errno != EEXIST) {
                        return -1;
                    }
                }
            }
        }
    }
    
    return 0;
}

int copy_or_link_file(const char *src, const char *dest, const char *ref, const options_t *opts, stats_t *stats) {
    struct stat src_st;
    int src_fd, dest_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    
    if (stat(src, &src_st) != 0) {
        return -1;
    }
    
    if (ref && opts->link_type != LINK_NONE) {
        struct stat ref_st;
        /* Remove destination file if it exists, since we've already decided to overwrite */
        unlink(dest);
        
        /* Get the size of the reference file - use stat() to follow symlinks */
        if (stat(ref, &ref_st) != 0) {
            /* If we can't stat the reference file, fall through to regular copy */
            if (opts->verbose) {
                printf("Warning: Could not stat reference file %s\n", ref);
            }
        } else {

            if (opts->link_type == LINK_HARD) {
                if (link(ref, dest) == 0) {
                    stats->files_hard_linked++;
                    stats->bytes_hard_linked += src_st.st_size;
                    return 0;
                } else {
                    if (opts->verbose) {
                        printf("Failed to create hard link for %s -> %s: %s\n", ref, dest, strerror(errno));
                    }
                }
            } else if (opts->link_type == LINK_SOFT) {
                if (symlink(ref, dest) == 0) {
                    stats->files_soft_linked++;
                    stats->bytes_soft_linked += src_st.st_size;
                    return 0;
                } else {
                    if (opts->verbose) {
                        printf("Failed to create soft link for %s -> %s: %s\n", ref, dest, strerror(errno));
                    }
                }
            }
        }
    }
    
    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        return -1;
    }
    
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, src_st.st_mode);
    if (dest_fd < 0) {
        close(src_fd);
        return -1;
    }
    
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            close(src_fd);
            close(dest_fd);
            unlink(dest);
            return -1;
        }
    }
    
    close(src_fd);
    close(dest_fd);
    
    if (bytes_read < 0) {
        unlink(dest);
        return -1;
    }
    
    if (opts->preserve.mode || opts->preserve.ownership || opts->preserve.timestamps) {
        if (preserve_file_attributes(src, dest, &opts->preserve) != 0) {
            if (opts->verbose) {
                printf("Warning: Failed to preserve attributes for %s\n", dest);
            }
        }
    }
    
    stats->files_copied++;
    stats->bytes_copied += src_st.st_size;
    
    return 0;
}

static int copy_directory_recursive(const char *src_path, const char *dest_path, 
                                   file_info_t *ref_files, const options_t *opts, stats_t *stats) {
    DIR *src_dir;
    struct dirent *entry;
    struct stat st;
    char src_full[MAX_PATH];
    char dest_full[MAX_PATH];
    file_info_t *matching_file;
    
    src_dir = opendir(src_path);
    if (!src_dir) {
        fprintf(stderr, "Error: Cannot open source directory %s: %s\n", 
                src_path, strerror(errno));
        return -1;
    }
    
    if (create_directory_structure(src_path, dest_path) != 0) {
        fprintf(stderr, "Error: Cannot create destination directory %s: %s\n", 
                dest_path, strerror(errno));
        closedir(src_dir);
        return -1;
    }
    
    if (opts->preserve.mode || opts->preserve.ownership || opts->preserve.timestamps) {
        if (preserve_file_attributes(src_path, dest_path, &opts->preserve) != 0 && opts->verbose) {
            printf("Warning: Failed to preserve attributes for directory %s\n", dest_path);
        }
    }
    
    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(src_full, sizeof(src_full), "%s/%s", src_path, entry->d_name);
        snprintf(dest_full, sizeof(dest_full), "%s/%s", dest_path, entry->d_name);
        
        if (stat(src_full, &st) != 0) {
            fprintf(stderr, "Warning: Cannot stat %s: %s\n", src_full, strerror(errno));
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
            matching_file = NULL;
            
            if (!should_overwrite(dest_full, opts)) {
                if (opts->verbose) {
                    printf("skipping '%s' (not overwriting)\n", dest_full);
                }
                stats->files_skipped++;
                continue;
            }
            
            if (ref_files) {
                matching_file = find_matching_file(ref_files, src_full, opts);
            }
            
            if (create_directory_structure(src_full, dest_full) != 0) {
                fprintf(stderr, "Warning: Cannot create directory structure for %s\n", dest_full);
                continue;
            }
            
            if (copy_or_link_file(src_full, dest_full, 
                                 matching_file ? matching_file->path : NULL, 
                                 opts, stats) != 0) {
                fprintf(stderr, "Warning: Cannot copy %s to %s: %s\n", 
                        src_full, dest_full, strerror(errno));
                continue;
            }
            
            if (opts->verbose) {
                if (matching_file) {
                    printf("%s -> %s (%s to %s)\n", src_full, dest_full,
                           opts->link_type == LINK_HARD ? "hard link" : "soft link",
                           matching_file->path);
                } else {
                    printf("%s -> %s (copied)\n", src_full, dest_full);
                }
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
    file_info_t *ref_files = NULL;
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
    
    /* Scan reference directory once */
    if (opts->ref_dir) {
        if (opts->verbose) {
            printf("Scanning reference directory %s...\n", opts->ref_dir);
        }
        ref_files = scan_reference_directory(opts);
        if (!ref_files) {
            if (opts->verbose) {
                printf("Warning: No files found in reference directory\n");
            }
        } else {
            // Count the number of reference files
            int ref_file_count = 0;
            file_info_t *current = ref_files;
            while (current) {
                ref_file_count++;
                current = current->next;
            }

            if (opts->verbose) {
                printf("Found %d reference files in %s\n", ref_file_count, opts->ref_dir);
            }
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
            file_info_t *matching_file = NULL;
            
            if (!should_overwrite(dest_path, opts)) {
                if (opts->verbose) {
                    printf("skipping '%s' (not overwriting)\n", dest_path);
                }
                stats->files_skipped++;
                continue;
            }
            
            if (ref_files) {
                matching_file = find_matching_file(ref_files, src_path, opts);
                if (opts->verbose && matching_file) {
                    printf("Found matching reference file for %s: %s\n", src_path, matching_file->path);
                }
            }
            
            if (create_directory_structure(src_path, dest_path) != 0) {
                fprintf(stderr, "Error: Cannot create directory structure for %s\n", dest_path);
                overall_result = -1;
                continue;
            }
            
            if (copy_or_link_file(src_path, dest_path,
                                 matching_file ? matching_file->path : NULL,
                                 opts, stats) != 0) {
                overall_result = -1;
                continue;
            }
            
            if (opts->verbose) {
                if (matching_file) {
                    printf("%s -> %s (%s to %s)\n", src_path, dest_path,
                           opts->link_type == LINK_HARD ? "hard link" : "soft link",
                           matching_file->path);
                } else {
                    printf("%s -> %s (copied)\n", src_path, dest_path);
                }
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
        free_file_list(ref_files);
    }
    
    return overall_result;
}
