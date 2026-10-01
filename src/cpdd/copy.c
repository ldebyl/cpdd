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

/* Path of file being copied, for cleanup on signal */
static char *current_incomplete_file = NULL;

/* Unlink wrapper that respects dry-run mode */
static int file_unlink(const char *path, const options_t *opts)
{
    if (opts->dry_run)
        return 0;
    return unlink(path);
}

/* Hard link wrapper that respects dry-run mode */
static int file_link(const char *oldpath, const char *newpath, const options_t *opts)
{
    if (opts->dry_run)
        return 0;
    return link(oldpath, newpath);
}

/* Symbolic link wrapper that respects dry-run mode */
static int file_symlink(const char *target, const char *linkpath, const options_t *opts)
{
    if (opts->dry_run)
        return 0;
    return symlink(target, linkpath);
}

/* True if dest already exists and is the same underlying file as src (same
 * device and inode) -- e.g. dest is a hard link to src, perhaps created by
 * an earlier cpdd run's reference-match linking. Opening a file with
 * O_TRUNC while another descriptor is reading that same inode truncates the
 * data out from under the read, so this has to be checked before file_copy
 * ever opens dest. */
static int same_file(const struct stat *src_st, const char *dest)
{
    struct stat dest_st;
    return stat(dest, &dest_st) == 0 &&
           dest_st.st_dev == src_st->st_dev && dest_st.st_ino == src_st->st_ino;
}

/* Copy file contents from src to dest. Returns bytes copied or -1 on error */
static off_t file_copy(const char *src, const char *dest, const options_t *opts, struct stat *src_st)
{
    if (opts->dry_run)
        return src_st->st_size;

    /* Guard is repeated here, defense in depth: whatever calls file_copy(),
     * this function must never destroy data by truncating src and dest as
     * the same file. */
    if (same_file(src_st, dest)) {
        errno = 0;
        return -1;
    }

    size_t block_size = get_optimal_block_size(src, opts);

    if (opts->verbose >= 3 && opts->block_size == 0)
        print_verbose("Using block size %zu bytes for %s", block_size, src);

    int src_fd, dest_fd;
    char *buffer;
    ssize_t bytes_read, bytes_written;

    buffer = malloc(block_size);
    if (!buffer)
        return -1;

    src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        free(buffer);
        return -1;
    }

    /* Never write into dest in place. Opening an existing path with O_TRUNC
     * rewrites whatever inode is already there -- if dest is hard-linked to
     * some other file entirely (an old cpdd run's linking, or anything a
     * user set up), that file is silently corrupted too, and if dest is a
     * symlink, O_TRUNC follows it and rewrites its target instead of dest
     * itself. same_file() above only catches dest pointing at src; it can't
     * catch dest pointing at anything else. Unlinking first removes only
     * that one name -- it can never affect other names sharing the same
     * content -- so the create below always starts a brand new inode.
     * O_EXCL is a second guard against a similar entry reappearing between
     * the unlink and the open. */
    unlink(dest);
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, src_st->st_mode);
    if (dest_fd < 0) {
        close(src_fd);
        free(buffer);
        return -1;
    }

    register_incomplete_file(dest);

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
    unregister_incomplete_file();

    if (bytes_read < 0)
        return -1;

    return src_st->st_size;
}

/* Clean up incomplete file and exit on signal */
static void signal_handler(int sig)
{
    cleanup_incomplete_file();
    exit(128 + sig);
}

/* Register handlers for common termination signals */
void setup_signal_handlers(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGPIPE, signal_handler);
}

/* Track file being written for cleanup on interrupt */
void register_incomplete_file(const char *path)
{
    if (current_incomplete_file)
        free(current_incomplete_file);
    current_incomplete_file = strdup(path);
}

/* Clear incomplete file tracking without deleting */
void unregister_incomplete_file(void)
{
    if (current_incomplete_file) {
        free(current_incomplete_file);
        current_incomplete_file = NULL;
    }
}

/* Delete incomplete file if one is registered */
void cleanup_incomplete_file(void)
{
    if (current_incomplete_file) {
        unlink(current_incomplete_file);
        unregister_incomplete_file();
    }
}

/* Copy a symbolic link itself (not its target) */
static int copy_symlink(const char *src, const char *dest, const options_t *opts,
                        const struct stat *src_st) {
    char link_target[MAX_PATH];
    ssize_t len;

    /* Read the symlink target */
    len = readlink(src, link_target, sizeof(link_target) - 1);
    if (len == -1) {
        print_error("Cannot read symlink %s: %s", src, strerror(errno));
        return -1;
    }
    link_target[len] = '\0';

    if (opts->dry_run) {
        if (opts->verbose >= 1) {
            print_verbose("%s[DRY RUN]%s %s[SYMLINK]%s '%s' -> '%s'",
                        color_dim(), color_reset(),
                        color_cyan(), color_reset(),
                        src, dest);
        }
        return 0;
    }

    /* Remove destination if it exists */
    unlink(dest);

    /* Create the new symlink */
    if (symlink(link_target, dest) != 0) {
        print_error("Cannot create symlink %s: %s", dest, strerror(errno));
        return -1;
    }

    if (PRESERVE_ANY(&opts->preserve) &&
        preserve_file_attributes(src_st, dest, &opts->preserve, 1) != 0) {
        print_warning("Failed to preserve attributes for %s", dest);
    }

    if (opts->verbose >= 1) {
        print_verbose("%s[SYMLINK]%s '%s' -> '%s'",
                    color_cyan(), color_reset(),
                    src, dest);
    }

    return 0;
}

/* Check if dest should be overwritten based on options and timestamps */
int should_overwrite(const char *src_path, const char *dest_path, const options_t *opts)
{
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

/* Nanosecond-resolution atime/mtime from a struct stat. macOS only exposes
 * the timespec members outside strict POSIX mode, so build them there. */
static struct timespec stat_atime(const struct stat *st)
{
#if defined(__APPLE__) && defined(_POSIX_C_SOURCE) && !defined(_DARWIN_C_SOURCE)
    struct timespec ts = { st->st_atime, st->st_atimensec };
    return ts;
#elif defined(__APPLE__)
    return st->st_atimespec;
#else
    return st->st_atim;
#endif
}

static struct timespec stat_mtime(const struct stat *st)
{
#if defined(__APPLE__) && defined(_POSIX_C_SOURCE) && !defined(_DARWIN_C_SOURCE)
    struct timespec ts = { st->st_mtime, st->st_mtimensec };
    return ts;
#elif defined(__APPLE__)
    return st->st_mtimespec;
#else
    return st->st_mtim;
#endif
}

/* Apply src_st's attributes to dest, as selected by preserve.
 * Each attribute is attempted independently; a failure on one does not
 * prevent the others from being applied. nofollow applies them to dest
 * itself when it is a symlink (mode is skipped there: symlink permissions
 * aren't settable on Linux and are ignored everywhere else).
 * Returns 0 if all requested attributes were applied, -1 if any failed. */
int preserve_file_attributes(const struct stat *src_st, const char *dest,
                             const preserve_t *preserve, int nofollow)
{
    int rc = 0;
    mode_t mode = src_st->st_mode & 07777;

    /* Ownership before mode: chown() clears setuid/setgid bits, so doing it
     * second would silently strip them from the mode just applied. */
    if (preserve->ownership) {
        int (*chown_fn)(const char *, uid_t, gid_t) = nofollow ? lchown : chown;

        if (chown_fn(dest, src_st->st_uid, src_st->st_gid) != 0) {
            int err = errno;
            /* Unprivileged users can't give files away, but may still be
             * able to set the group if they belong to it. */
            int gid_ok = err == EPERM && chown_fn(dest, (uid_t)-1, src_st->st_gid) == 0;

            /* Never leave setuid/setgid bits on a file that didn't get the
             * user/group they were meant to run as. */
            mode &= ~(mode_t)S_ISUID;
            if (!gid_ok)
                mode &= ~(mode_t)S_ISGID;

            if (err != EPERM || preserve->ownership == PRESERVE_EXPLICIT) {
                print_warning("chown %s: %s", dest, strerror(err));
                rc = -1;
            }
        }
    }

    if (preserve->mode && !nofollow) {
        if (chmod(dest, mode) != 0) {
            print_warning("chmod %s: %s", dest, strerror(errno));
            rc = -1;
        }
    }

    /* Timestamps last: nothing after this may touch dest's contents. */
    if (preserve->atime || preserve->mtime) {
        struct timespec times[2];
        times[0] = preserve->atime ? stat_atime(src_st) : (struct timespec){ 0, UTIME_OMIT };
        times[1] = preserve->mtime ? stat_mtime(src_st) : (struct timespec){ 0, UTIME_OMIT };
        if (utimensat(AT_FDCWD, dest, times, nofollow ? AT_SYMLINK_NOFOLLOW : 0) != 0) {
            print_warning("utimensat %s: %s", dest, strerror(errno));
            rc = -1;
        }
    }

    return rc;
}

/* Parse size string with K/M/G suffixes, e.g. "64K" or "1M" */
size_t parse_size(const char *size_str)
{
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

/* Return optimal I/O block size for file, or user override if set */
size_t get_optimal_block_size(const char *filename, const options_t *opts)
{
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

/* Format byte count as human-readable string (e.g. "1.5M") */
void format_bytes(off_t bytes, int human_readable, char *buffer, size_t buffer_size)
{
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

/* Format one-line summary of copy statistics */
void format_stats_line(const stats_t *stats, int human_readable, char *buffer, size_t buffer_size)
{
    char total_bytes_str[32];
    off_t total_bytes = stats->bytes_copied + stats->bytes_hard_linked + stats->bytes_soft_linked;
    int total_files = stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;

    format_bytes(total_bytes, human_readable, total_bytes_str, sizeof(total_bytes_str));

    snprintf(buffer, buffer_size, "Files: %d copied, %d linked, %d skipped | Total: %d files (%s)",
             stats->files_copied, stats->files_hard_linked + stats->files_soft_linked,
             stats->files_skipped, total_files, total_bytes_str);
}

/* Print detailed final statistics to stderr */
void print_statistics(const stats_t *stats, int human_readable)
{
    char copied_bytes[32], linked_bytes[32], soft_linked_bytes[32];

    format_bytes(stats->bytes_copied, human_readable, copied_bytes, sizeof(copied_bytes));
    format_bytes(stats->bytes_hard_linked, human_readable, linked_bytes, sizeof(linked_bytes));
    format_bytes(stats->bytes_soft_linked, human_readable, soft_linked_bytes, sizeof(soft_linked_bytes));

    fprintf(stderr, "\n%sStatistics:%s\n", color_cyan(), color_reset());
    fprintf(stderr, "  Files copied:      %s%d%s (%s)\n", color_green(), stats->files_copied, color_reset(), copied_bytes);
    fprintf(stderr, "  Files hard linked: %s%d%s (%s)\n", color_blue(), stats->files_hard_linked, color_reset(), linked_bytes);
    fprintf(stderr, "  Files soft linked: %s%d%s (%s)\n", color_blue(), stats->files_soft_linked, color_reset(), soft_linked_bytes);
    fprintf(stderr, "  Files skipped:     %s%d%s\n", color_yellow(), stats->files_skipped, color_reset());
    if (stats->files_failed > 0) {
        fprintf(stderr, "  Files failed:      %s%d%s\n", color_red(), stats->files_failed, color_reset());
    }

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
        fprintf(stderr, "  Cache depth:      avg: %.1f, min: %d, max: %d blocks\n",
               avg_cache_depth, stats->min_cache_depth, stats->max_cache_depth);
    }

    /* Display size collision statistics */
    if (stats->total_ref_files > 0 && stats->total_source_files > 0) {
        fprintf(stderr, "\n%sSize Collision Statistics:%s\n", color_cyan(), color_reset());
        fprintf(stderr, "  Reference files:  %d\n",
               stats->total_ref_files);
        fprintf(stderr, "  Source files:     %d\n",
               stats->total_source_files);

        double ref_unique_pct = (double)stats->unique_ref_sizes / stats->total_ref_files * 100.0;
        double size_match_pct = (double)stats->files_with_size_matches / stats->total_source_files * 100.0;

        fprintf(stderr, "  Unique reference file sizes: %.1f%%\n", ref_unique_pct);
        fprintf(stderr, "  Size matches:                %.1f%%\n", size_match_pct);
    }

    if (stats->index_memory > 0) {
        char mem_str[32];
        format_bytes((off_t)stats->index_memory, human_readable, mem_str, sizeof(mem_str));
        fprintf(stderr, "\n%sIndex Memory:%s            %s\n", color_cyan(), color_reset(), mem_str);
    }
}

/* Create directory with given mode, respecting dry-run */
static int create_directory(const char *path, mode_t mode, const options_t *opts)
{
    if (opts->dry_run) {
        return 0; /* Pretend success */
    }

    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* Create parent directories recursively (mkdir -p style) */
static int create_parent_directories(const char *path, mode_t default_mode, const options_t *opts)
{
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

/* Ensure dest directory structure exists, preserving src permissions.
 * known_src_st lets a caller that already has src's stat() pass it in
 * instead of paying for a second one here. */
int create_directory_structure(const char *src_path, const char *dest_path,
                                const options_t *opts, const struct stat *known_src_st)
{
    struct stat local_st;
    const struct stat *src_st;

    src_st = known_src_st;
    if (!src_st) {
        if (stat(src_path, &local_st) != 0) {
            return -1;
        }
        src_st = &local_st;
    }

    if (S_ISDIR(src_st->st_mode)) {
        /* Source is directory - create destination directory */
        if (create_directory(dest_path, src_st->st_mode, opts) != 0) {
            return -1;
        }
        /* Attributes are applied by copy_directory_recursive() once the
         * directory is populated -- adding entries would reset its mtime. */
    } else {
        /* Source is file - create parent directory for destination file */
        if (create_parent_directories(dest_path, 0755, opts) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Copy file, or create link to matching reference file if found */
int copy_or_link_file(const char *src, const char *dest, ref_files_t *ref_files,
                       const options_t *opts, stats_t *stats, int dest_dir_ready)
{
    struct stat src_st;
    file_info_t *matching_file = NULL;

    /* Check if we should skip symlinks entirely */
    if (opts->skip_symlinks) {
        struct stat lstat_st;
        if (lstat(src, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
            if (opts->verbose >= 1) {
                print_verbose("%s[SKIP]%s '%s'",
                            color_yellow(), color_reset(), src);
            }
            stats->files_skipped++;
            processed_log_write(opts->log_fp, src, lstat_st.st_size, lstat_st.st_mtime, "SKIP-SYMLINK");
            return 0;
        }
    }

    /* Use lstat if not dereferencing, stat otherwise */
    int stat_result;
    if (opts->no_dereference) {
        stat_result = lstat(src, &src_st);

        if (stat_result != 0) {
            print_error("Cannot lstat %s: %s", src, strerror(errno));
            return -1;
        }
    } else {
        stat_result = stat(src, &src_st);

        if (stat_result != 0) {
            /* Check if it's a broken symlink */
            struct stat lstat_st;
            if (lstat(src, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                print_warning("Skipping broken symlink: %s", src);
                stats->files_skipped++;
                return 0;
            }
            print_error("Cannot stat %s: %s", src, strerror(errno));
            return -1;
        }
    }

    /* Placed before reference matching so we bypass the comparison path. */
    if (opts->skip_set &&
        skip_set_contains(opts->skip_set, src, src_st.st_size, src_st.st_mtime)) {
        if (opts->verbose >= 1) {
            print_verbose("%s[SKIP-LOG]%s '%s'",
                        color_yellow(), color_reset(), src);
        }
        stats->files_skipped++;
        return 0;
    }

    /* If it's a symlink and we're not dereferencing, copy the symlink itself */
    if (opts->no_dereference && S_ISLNK(src_st.st_mode)) {
        /* Check if we should overwrite */
        if (!should_overwrite(src, dest, opts)) {
            if (opts->verbose >= 1) {
                print_verbose("%s[SKIP]%s '%s'",
                            color_yellow(), color_reset(), src);
            }
            stats->files_skipped++;
            return 0;
        }

        /* Ensure destination directory exists */
        if (!dest_dir_ready && create_directory_structure(src, dest, opts, NULL) != 0) {
            return -1;
        }

        /* Copy the symlink itself, skip all duplicate detection */
        return copy_symlink(src, dest, opts, &src_st);
    }

    /* At this point, we either have a regular file or a dereferenced symlink */
    if (!S_ISREG(src_st.st_mode)) {
        if (opts->verbose >= 1) {
            print_verbose("%s[SKIP]%s '%s'",
                        color_yellow(), color_reset(), src);
        }
        stats->files_skipped++;
        return 0;
    }

    /* Check if we should overwrite */
    if (!should_overwrite(src, dest, opts)) {
        if (opts->verbose >= 1) {
            print_verbose("%s[SKIP]%s '%s'",
                        color_yellow(), color_reset(), src);
        }
        stats->files_skipped++;
        processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "SKIP-EXIST");
        return 0;
    }

    /* Count source files processed for statistics */
    stats->total_source_files++;

    /* Find matching reference file if available. src_st.st_size is passed
     * in rather than letting this stat() src itself again -- the caller
     * (here) already has it, computed above. */
    matching_file = find_matching_file(ref_files, src, src_st.st_size, opts, stats);
    if (opts->verbose >= 3 && matching_file) {
        print_verbose("Found matching reference file for %s: %s", src, matching_file->path);
    }

    /* If --only-new is set and file exists in reference, skip it */
    if (opts->only_new && matching_file) {
        if (opts->verbose >= 1) {
            print_verbose("%s[SKIP]%s '%s' (matches '%s')",
                        color_yellow(), color_reset(), src, matching_file->path);
        }
        stats->files_skipped++;
        processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "SKIP-DUP");
        return 0;
    }

    /* Ensure destination directory exists. Skipped when the caller already
     * guarantees it (dest_dir_ready): the redundant stat() of src this
     * would otherwise repeat, and the near-always-succeeds stat() of the
     * parent directory inside it, cost one wasted syscall pair per file --
     * significant when copying many small files. When it does run, src_st
     * is passed in already computed above, so at most the parent-directory
     * check is paid for, never a second stat() of src itself. */
    if (!dest_dir_ready &&
        create_directory_structure(src, dest, opts, &src_st) != 0) {
        print_error("Cannot create directory structure for %s: %s", dest, strerror(errno));
        stats->files_failed++;
        return -1;
    }

    /* Try to create a link to reference file if we found a match */
    if (matching_file && opts->link_type != LINK_NONE) {
        struct stat ref_st;

        /* Get the size of the reference file */
        if (stat(matching_file->path, &ref_st) != 0) {
            if (opts->verbose >= 3) {
                print_warning("Could not stat reference file %s", matching_file->path);
            }
        } else {
            /* Remove destination file if it exists */
            file_unlink(dest, opts);

            if (opts->link_type == LINK_HARD) {
                if (file_link(matching_file->path, dest, opts) == 0) {
                    stats->files_hard_linked++;
                    stats->bytes_hard_linked += src_st.st_size;

                    if (opts->verbose >= 1) {
                        if (opts->dry_run) {
                            print_verbose("%s[DRY RUN]%s %s[LINK]%s '%s' -> '%s' -> '%s'",
                                        color_dim(), color_reset(),
                                        color_blue(), color_reset(),
                                        src, dest, matching_file->path);
                        } else {
                            print_verbose("%s[LINK]%s '%s' -> '%s' -> '%s'",
                                        color_blue(), color_reset(),
                                        src, dest, matching_file->path);
                        }
                    }
                    processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "LINK-HARD");
                    return 0;
                } else {
                    print_error("Failed to create hard link for %s -> %s: %s", matching_file->path, dest, strerror(errno));
                }
            } else if (opts->link_type == LINK_SOFT) {
                if (file_symlink(matching_file->path, dest, opts) == 0) {
                    stats->files_soft_linked++;
                    stats->bytes_soft_linked += src_st.st_size;

                    if (opts->verbose >= 1) {
                        if (opts->dry_run) {
                            print_verbose("%s[DRY RUN]%s %s[LINK]%s '%s' -> '%s' -> '%s'",
                                        color_dim(), color_reset(),
                                        color_blue(), color_reset(),
                                        src, dest, matching_file->path);
                        } else {
                            print_verbose("%s[LINK]%s '%s' -> '%s' -> '%s'",
                                        color_blue(), color_reset(),
                                        src, dest, matching_file->path);
                        }
                    }
                    processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "LINK-SOFT");
                    return 0;
                } else {
                    if (opts->verbose >= 3) {
                        print_verbose("Failed to create soft link for %s -> %s: %s", matching_file->path, dest, strerror(errno));
                    }
                }
            }
        }
    }

    /* If no reference match, optionally link the destination back to the source
     * instead of copying. Useful for merging directory trees on the same
     * filesystem without duplicating data. */
    if (opts->source_link_type != LINK_NONE) {
        file_unlink(dest, opts);

        if (opts->source_link_type == LINK_HARD) {
            if (file_link(src, dest, opts) == 0) {
                stats->files_hard_linked++;
                stats->bytes_hard_linked += src_st.st_size;
                if (opts->verbose >= 1) {
                    print_verbose("%s%s[LINK-SRC]%s '%s' -> '%s'",
                                opts->dry_run ? color_dim() : "",
                                opts->dry_run ? "[DRY RUN] " : "",
                                color_blue(), src, dest);
                    (void)color_reset();
                }
                processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "LINK-SRC-HARD");
                return 0;
            }
            print_warning("Hard link to source failed for %s -> %s: %s; falling back to copy",
                          src, dest, strerror(errno));
            /* fall through to regular copy */
        } else if (opts->source_link_type == LINK_SOFT) {
            if (file_symlink(src, dest, opts) == 0) {
                stats->files_soft_linked++;
                stats->bytes_soft_linked += src_st.st_size;
                if (opts->verbose >= 1) {
                    print_verbose("%s%s[LINK-SRC]%s '%s' -> '%s'",
                                opts->dry_run ? color_dim() : "",
                                opts->dry_run ? "[DRY RUN] " : "",
                                color_blue(), src, dest);
                }
                processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "LINK-SRC-SOFT");
                return 0;
            }
            print_warning("Symlink to source failed for %s -> %s: %s; falling back to copy",
                          src, dest, strerror(errno));
            /* fall through to regular copy */
        }
    }

    /* dest may already be the same file as src -- typically a hard link left
     * by an earlier cpdd run's reference-match linking. Copying onto it
     * would truncate the data both names point to before a single byte is
     * read back, destroying src as well as dest. Recognise it and skip
     * instead: the content is already identical, nothing needs to change. */
    if (same_file(&src_st, dest)) {
        if (opts->verbose >= 1) {
            print_verbose("%s[SKIP]%s '%s' (already the same file as '%s')",
                        color_yellow(), color_reset(), dest, src);
        }
        stats->files_skipped++;
        processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "SKIP-SAMEFILE");
        return 0;
    }

    /* Fall back to regular copy */
    off_t bytes_copied = file_copy(src, dest, opts, &src_st);
    if (bytes_copied < 0) {
        print_error("Failed to copy %s -> %s: %s", src, dest, strerror(errno));
        stats->files_failed++;
        return -1;
    }

    stats->files_copied++;
    stats->bytes_copied += bytes_copied;

    /* Preserve attributes if requested. Never on a dry run: dest was not
     * written, and if it already exists it isn't ours to modify. */
    if (!opts->dry_run && PRESERVE_ANY(&opts->preserve)) {
        if (preserve_file_attributes(&src_st, dest, &opts->preserve, 0) != 0) {
            print_warning("Failed to preserve attributes for %s", dest);
        }
    }

    if (opts->verbose >= 1) {
        if (opts->dry_run) {
            print_verbose("%s[DRY RUN]%s %s[COPY]%s '%s' -> '%s'",
                        color_dim(), color_reset(),
                        color_green(), color_reset(),
                        src, dest);
        } else {
            print_verbose("%s[COPY]%s '%s' -> '%s'",
                        color_green(), color_reset(),
                        src, dest);
        }
    }

    processed_log_write(opts->log_fp, src, src_st.st_size, src_st.st_mtime, "COPY");
    return 0;
}

/* qsort/bsearch comparator for an array of C strings */
static int compare_strs(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Free a sorted name list built by build_dest_name_set */
static void free_name_set(char **names, int count)
{
    if (!names) return;
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

/* Build a sorted array of entry names from dest_path. Returns NULL on
 * failure or empty directory. Sets *out_count to the number of entries.
 * Used to batch a single readdir on the destination instead of stat-ing
 * each file individually -- a big win over high-latency filesystems
 * such as SMB when --no-clobber is set. */
static char **build_dest_name_set(const char *dest_path, int *out_count)
{
    DIR *d = opendir(dest_path);
    if (!d) {
        *out_count = 0;
        return NULL;
    }

    int capacity = 64;
    int count = 0;
    char **names = malloc(capacity * sizeof(char *));
    if (!names) {
        closedir(d);
        *out_count = 0;
        return NULL;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (count == capacity) {
            capacity *= 2;
            char **grown = realloc(names, capacity * sizeof(char *));
            if (!grown) {
                free_name_set(names, count);
                closedir(d);
                *out_count = 0;
                return NULL;
            }
            names = grown;
        }
        names[count] = strdup(e->d_name);
        if (!names[count]) {
            free_name_set(names, count);
            closedir(d);
            *out_count = 0;
            return NULL;
        }
        count++;
    }
    closedir(d);

    qsort(names, count, sizeof(char *), compare_strs);
    *out_count = count;
    return names;
}

/* If src_name exists in the cached destination listing, emit a [SKIP] log,
 * bump files_skipped, and return 1 so the caller can `continue`. Returns 0
 * otherwise (or when no cache is available). Caller is responsible for not
 * invoking this on entries that should recurse (i.e. directories). */
static int try_skip_existing(char **dest_names, int dest_name_count,
                             const char *src_full, const char *src_name,
                             const options_t *opts, stats_t *stats)
{
    if (!dest_names) {
        return 0;
    }
    if (bsearch(&src_name, dest_names, dest_name_count,
                sizeof(char *), compare_strs) == NULL) {
        return 0;
    }
    if (opts->verbose >= 1) {
        print_verbose("%s[SKIP]%s '%s'",
                    color_yellow(), color_reset(), src_full);
    }
    stats->files_skipped++;
    /* Intentionally not logged: the fast path skipped this entry without
     * statting it, and statting now just to log would defeat the optimization.
     * A subsequent run will re-detect the destination collision the same way. */
    return 1;
}

/* Recursively copy directory contents, linking duplicates when possible */
/* Entries actually written to the destination (or that would be, on a dry
 * run) -- as opposed to skipped. */
static int placed_count(const stats_t *stats)
{
    return stats->files_copied + stats->files_hard_linked + stats->files_soft_linked;
}

/* *placed (optional) is set to whether anything ended up in dest_path,
 * directly or in a subdirectory, for --prune-empty-dirs. */
static int copy_directory_recursive(const char *src_path, const char *dest_path,
                                    ref_files_t *ref_files, const options_t *opts, stats_t *stats,
                                    int *placed)
{
    DIR *src_dir;
    struct stat dest_st;
    int created = 0;
    int kept = 0;
    struct dirent *entry;
    struct stat st;
    char src_full[MAX_PATH];
    char dest_full[MAX_PATH];
    char **dest_names = NULL;
    int dest_name_count = 0;

    src_dir = opendir(src_path);
    if (!src_dir) {
        print_error("Cannot open source directory %s: %s", src_path, strerror(errno));
        return -1;
    }

    if (placed)
        *placed = 1;  /* Conservative until the walk below says otherwise */

    /* Only a directory this run creates is ever pruned; one that already
     * existed is left alone however empty it is. */
    if (opts->prune_empty_dirs)
        created = lstat(dest_path, &dest_st) != 0;

    if (create_directory_structure(src_path, dest_path, opts, NULL) != 0) {
        print_error("Cannot create destination directory %s: %s", dest_path, strerror(errno));
        closedir(src_dir);
        return -1;
    }

    /* For --no-clobber, list the destination directory once and use it as
     * an in-memory existence set so we can short-circuit per-file stat()s. */
    if (opts->no_clobber) {
        dest_names = build_dest_name_set(dest_path, &dest_name_count);
    }

    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(src_full, sizeof(src_full), "%s/%s", src_path, entry->d_name);
        snprintf(dest_full, sizeof(dest_full), "%s/%s", dest_path, entry->d_name);

#ifdef CPDD_HAVE_D_TYPE
        /* Fast skip path for --no-clobber: when readdir gives us the entry
         * type directly we can skip without statting the source at all.
         * Only safe for non-directory entries -- a colliding directory
         * still has to recurse normally so missing children get copied. */
        if (dest_names && entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
            if (try_skip_existing(dest_names, dest_name_count, src_full,
                                   entry->d_name, opts, stats)) {
                continue;
            }
        }
#endif

        /* Check if we should skip symlinks entirely */
        if (opts->skip_symlinks) {
            struct stat lstat_st;
            if (lstat(src_full, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                if (opts->verbose >= 1) {
                    print_verbose("%s[SKIP]%s '%s'",
                                color_yellow(), color_reset(), src_full);
                }
                continue;
            }
        }

        /* Use lstat if not dereferencing, stat otherwise */
        int stat_result;
        if (opts->no_dereference) {
            stat_result = lstat(src_full, &st);

            if (stat_result != 0) {
                print_warning("Cannot lstat %s: %s", src_full, strerror(errno));
                continue;
            }
        } else {
            stat_result = stat(src_full, &st);

            if (stat_result != 0) {
                /* Check if it's a broken symlink */
                struct stat lstat_st;
                if (lstat(src_full, &lstat_st) == 0 && S_ISLNK(lstat_st.st_mode)) {
                    print_warning("Skipping broken symlink: %s", src_full);
                    continue;
                }
                print_warning("Cannot stat %s: %s", src_full, strerror(errno));
                continue;
            }
        }

        /* Handle symlinks */
        if (opts->no_dereference && S_ISLNK(st.st_mode)) {
            if (try_skip_existing(dest_names, dest_name_count, src_full, entry->d_name, opts, stats)) {
                continue;
            }
            /* Copied symlinks aren't counted in placed_count(), so tell
             * placement apart from a skip by the skip counter instead. */
            int skipped_before = stats->files_skipped;
            /* dest_dir_ready=1: dest_path (the directory this loop is
             * iterating) was already created above, before this loop
             * started, so its existence never needs reverifying per entry. */
            if (copy_or_link_file(src_full, dest_full, ref_files, opts, stats, 1) != 0) {
                continue;
            }
            if (stats->files_skipped == skipped_before)
                kept = 1;

            if (opts->show_stats && opts->verbose == 0) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));
                print_status_update("%s", stats_buffer);
            }
            ref_cache_checkpoint(ref_files, opts);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (opts->recursive) {
                int child_placed;
                int rc = copy_directory_recursive(src_full, dest_full, ref_files, opts, stats,
                                                  &child_placed);
                if (child_placed)
                    kept = 1;
                if (rc != 0) {
                    stats->files_failed++;
                    continue;
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            if (try_skip_existing(dest_names, dest_name_count, src_full, entry->d_name, opts, stats)) {
                continue;
            }
            int placed_before = placed_count(stats);
            /* dest_dir_ready=1: same reasoning as the symlink branch above. */
            if (copy_or_link_file(src_full, dest_full, ref_files, opts, stats, 1) != 0) {
                continue;
            }
            if (placed_count(stats) != placed_before)
                kept = 1;

            if (opts->show_stats && opts->verbose == 0) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));
                print_status_update("%s", stats_buffer);
            }
            ref_cache_checkpoint(ref_files, opts);
        }
    }

    closedir(src_dir);
    free_name_set(dest_names, dest_name_count);

    /* The destination root itself is never pruned, as with rsync: the user
     * named it, so it should exist afterwards. rmdir() only ever removes an
     * empty directory, so nothing placed here can be lost to a miscount. */
    if (created && !kept && strcmp(dest_path, opts->dest_dir) != 0) {
        if (opts->dry_run || rmdir(dest_path) == 0) {
            if (placed)
                *placed = 0;
            if (opts->verbose >= 1)
                print_verbose("%s%s[PRUNE]%s '%s'",
                              opts->dry_run ? color_dim() : "",
                              opts->dry_run ? "[DRY RUN] " : "",
                              color_reset(), dest_path);
            return 0;
        }
    }

    /* Only now that every child is in place: each entry created above bumped
     * the directory's mtime, so applying it any earlier would be undone. */
    if (!opts->dry_run && PRESERVE_ANY(&opts->preserve)) {
        struct stat dir_st;
        if (stat(src_path, &dir_st) != 0 ||
            preserve_file_attributes(&dir_st, dest_path, &opts->preserve, 0) != 0) {
            if (opts->verbose)
                print_warning("Failed to preserve attributes for directory %s", dest_path);
        }
    }
    return 0;
}

/* Main entry point: copy all sources to destination with deduplication */
int copy_directory(const options_t *opts, stats_t *stats)
{
    struct stat dest_st;
    ref_files_t *ref_files = NULL;
    int overall_result = 0;
    int dest_is_dir = 0;

    /* Check destination */
    if (stat(opts->dest_dir, &dest_st) == 0) {
        if (S_ISDIR(dest_st.st_mode)) {
            dest_is_dir = 1;
        } else if (S_ISREG(dest_st.st_mode) && opts->source_count > 1) {
            print_error("Cannot copy multiple sources to a regular file");
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
        if (opts->verbose >= 3) {
            print_verbose("Scanning %d reference directories...", opts->ref_dir_count);
        }
        ref_files = scan_reference_directory(opts, stats);
        if (!ref_files) {
            print_warning("No files found in reference directories");
        } else if (opts->verbose >= 3) {
            print_verbose("Found %d reference files across all directories", ref_files->count);
        }
    }

    /* Process each source */
    for (int i = 0; i < opts->source_count; i++) {
        struct stat src_st;
        char dest_path[MAX_PATH];
        const char *src_path = opts->sources[i];

        if (stat(src_path, &src_st) != 0) {
            print_error("Cannot access source %s: %s", src_path, strerror(errno));
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
            if (copy_directory_recursive(src_path, dest_path, ref_files, opts, stats, NULL) != 0) {
                overall_result = -1;
            }
        } else {
            /* dest_dir_ready=0: this is a top-level source named directly on
             * the command line, so unlike the recursive-walk call sites,
             * opts->dest_dir's existence has not already been established. */
            if (copy_or_link_file(src_path, dest_path, ref_files, opts, stats, 0) != 0) {
                overall_result = -1;
                continue;
            }

            if (opts->show_stats && opts->verbose == 0) {
                char stats_buffer[256];
                format_stats_line(stats, opts->human_readable, stats_buffer, sizeof(stats_buffer));
                print_status_update("%s", stats_buffer);
            }
            ref_cache_checkpoint(ref_files, opts);
        }
    }

    if (ref_files) {
        /* Final write regardless of the periodic checkpoint's own timing, so
         * the last file's worth of progress is never lost to it simply not
         * having fired again before the run ended. */
        if (opts->cache_file) {
            ref_cache_write(opts->cache_file, ref_files);
        }

        /* Calculate index memory usage before freeing */
        stats->index_memory = sizeof(ref_files_t)
                            + (size_t)ref_files->count * sizeof(file_info_t *);
        for (int i = 0; i < ref_files->count; i++) {
            file_info_t *f = ref_files->files[i];
            stats->index_memory += sizeof(file_info_t);
            stats->index_memory += strlen(f->path) + 1;
            stats->index_memory += (size_t)f->block_hashes.allocated_blocks * BLOCK_HASH_SIZE;
        }
        free_sorted_file_info(ref_files);
    }

    return overall_result;
}
