/*
   * cpdd/matching.c - Content-based copy with deduplication
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
#include "md5.h"

/* Initialize a new block cache to empty state */
void init_block_hashes(block_hashes_t *cache) {
    cache->block_md5s = NULL;
    cache->cached_blocks = 0;
    cache->allocated_blocks = 0;
}

/* Grow block cache by BLOCK_CACHE_GROW_SIZE blocks */
int grow_block_hashes(block_hashes_t *hashes) {
    if (hashes->allocated_blocks >= MAX_CACHED_BLOCKS) {
        return 0; /* Already at maximum size */
    }
    
    int new_size = hashes->allocated_blocks + BLOCK_CACHE_GROW_SIZE;
    if (new_size > MAX_CACHED_BLOCKS) {
        new_size = MAX_CACHED_BLOCKS;
    }
    
    unsigned char (*new_blocks)[BLOCK_HASH_SIZE] = realloc(hashes->block_md5s, 
                                                          new_size * BLOCK_HASH_SIZE);
    if (!new_blocks) {
        return -1; /* Memory allocation failed */
    }
    
    hashes->block_md5s = new_blocks;
    hashes->allocated_blocks = new_size;
    return 1; /* Success */
}

/* Free block cache memory */
void free_block_hashes(block_hashes_t *hashes) {
    if (hashes->block_md5s) {
        free(hashes->block_md5s);
        hashes->block_md5s = NULL;
    }
    hashes->cached_blocks = 0;
    hashes->allocated_blocks = 0;
}

/* Update block cache with new block hash if needed */
static int update_hash_chain(file_info_t *file, int block_num, 
                             const unsigned char *buffer, size_t bytes) {
    /* Only cache if we need to (beyond current cache) */
    if (bytes == 0 || block_num < file->block_hashes.cached_blocks) {
        return 1; /* Nothing to do */
    }
    
    /* Check if we're already at maximum blocks */
    if (block_num >= MAX_CACHED_BLOCKS) {
        return 0; /* Can't cache beyond maximum */
    }
    
    /* Grow cache if needed */
    if (block_num >= file->block_hashes.allocated_blocks) {
        if (grow_block_hashes(&file->block_hashes) <= 0) {
            return 0; /* Can't grow cache */
        }
    }
    
    /* Calculate and store block hash */
    unsigned char block_hash[BLOCK_HASH_SIZE];
    calculate_block_hash(buffer, bytes, block_hash, BLOCK_HASH_SIZE);
    memcpy(file->block_hashes.block_md5s[block_num], block_hash, BLOCK_HASH_SIZE);
    file->block_hashes.cached_blocks = block_num + 1;
    
    return 1; /* Success */
}


/* Block-based file comparison with dynamic cache growth.
 * 1. First check cached blocks from reference file for fast rejection
 * 2. If cached blocks match, do full bytewise comparison from beginning
 * 3. Build block cache for both files during comparison
 * 4. Stop caching at first mismatch or when cache is full
 */
match_result_t files_match(file_info_t *ref_file, file_info_t *src_file) {
    /* Files should always have the same size when this function is called */
    if (ref_file->size != src_file->size) {
        fprintf(stderr, "Internal error: files_match called with different sized files\n");
        return MATCH_ERROR;
    }
    
    
    /* Phase 1: Fast rejection using cached blocks (pure memory comparison) */
    int common_cached_blocks = (ref_file->block_hashes.cached_blocks < src_file->block_hashes.cached_blocks) ?
                               ref_file->block_hashes.cached_blocks : src_file->block_hashes.cached_blocks;

    if (common_cached_blocks > 0) {
        /* Compare all common cached blocks in one memcmp since they're contiguous */
        if (memcmp(ref_file->block_hashes.block_md5s, src_file->block_hashes.block_md5s,
                   common_cached_blocks * BLOCK_HASH_SIZE) != 0) {
            return MATCH_FAIL_HASHES; /* Fast rejection - cached blocks differ */
        }
    }
    
    /* Phase 2: Full bytewise comparison with synchronized cache building */
    FILE *src_fp = fopen(src_file->path, "rb");
    FILE *ref_fp = fopen(ref_file->path, "rb");
    if (!src_fp || !ref_fp) {
        if (src_fp) fclose(src_fp);
        if (ref_fp) fclose(ref_fp);
        return MATCH_ERROR;
    }
    
    unsigned char ref_buffer[BUFFER_SIZE], src_buffer[BUFFER_SIZE];
    size_t ref_bytes, src_bytes;
    int block_num = 0;
    int files_match = 1;
    
    /* Read and compare blocks */
    do {
        ref_bytes = fread(ref_buffer, 1, BUFFER_SIZE, ref_fp);
        src_bytes = fread(src_buffer, 1, BUFFER_SIZE, src_fp);
        
        /* Update caches first (since we've already read the data) */
        update_hash_chain(ref_file, block_num, ref_buffer, ref_bytes);
        update_hash_chain(src_file, block_num, src_buffer, src_bytes);
        
        /* Bytewise comparison after cache building */
        if (ref_bytes != src_bytes || memcmp(ref_buffer, src_buffer, ref_bytes) != 0) {
            files_match = 0;
            break; /* Exit early on mismatch */
        }
        
        block_num++;
        
    } while (ref_bytes > 0);
    
    fclose(ref_fp);
    fclose(src_fp);
    
    return files_match ? MATCH_SUCCESS : MATCH_FAIL_BYTEWISE;
}

/*
 * Helper function to recursively collect file paths and sizes portably across operating systems.
 */
static void collect_file_info(const char *ref_dir, const options_t *opts, int *count, file_info_t **head) {
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char full_path[MAX_PATH];

    dir = opendir(ref_dir);
    if (!dir) {
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", ref_dir, entry->d_name);
        
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            collect_file_info(full_path, opts, count, head);
        } else if (S_ISREG(st.st_mode)) {
            file_info_t *new_file = malloc(sizeof(file_info_t));
            if (!new_file) {
                continue;
            }
            // Display the file that is being added
            if (opts->verbose == 3) {
                // Cast off_t to long long to avoid cross-platform format specifier issues
                printf("Adding reference file: %s (size: %lld bytes)\n", full_path, (long long)st.st_size);
            }

            new_file->path = strdup(full_path);
            new_file->size = st.st_size;

            /* Set basename as pointer into path */
            new_file->basename = strrchr(new_file->path, '/');
            new_file->basename = new_file->basename ? new_file->basename + 1 : new_file->path;

            /* Initialize block cache */
            init_block_hashes(&new_file->block_hashes);
            
            new_file->next = *head;
            *head = new_file;
            (*count)++;
        }
    }
    
    closedir(dir);
    if (opts->verbose == 1) {
        print_status_update("\rScanned %d reference files in", *count, ref_dir);
        fflush(stdout);
    }
}

static int compare_file_info_size(const void *a, const void *b) {
    file_info_t *file_a = *(file_info_t **)a;
    file_info_t *file_b = *(file_info_t **)b;
    return (file_a->size > file_b->size) - (file_a->size < file_b->size);
}

static int find_first_size_match(ref_files_t *ref_files, off_t target_size) {
    int left = 0, right = ref_files->count - 1;
    int first_match = -1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (ref_files->files[mid]->size == target_size) {
            first_match = mid;
            right = mid - 1;  /* Continue searching left for first occurrence */
        } else if (ref_files->files[mid]->size < target_size) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return first_match;  /* -1 if not found */
}


/*
 * Recursively scan reference directory and build sorted array of file metadata.
 * Uses lazy MD5 calculation optimization: first pass collects file sizes and marks
 * files that need MD5 (those with duplicate sizes), but doesn't calculate MD5 yet.
 * MD5 is calculated lazily during the first comparison attempt.
 * Returns sorted_file_info_t structure with array of file_info_t pointers, or NULL on error.
 */
ref_files_t *scan_reference_directory(const options_t *opts, stats_t *stats) {
    file_info_t *head = NULL;
    file_info_t *current;
    ref_files_t *sorted_files;
    
    /* First pass: collect all files from all reference directories */
    int total_files = 0;
    
    for (int i = 0; i < opts->ref_dir_count; i++) {
        collect_file_info(opts->ref_dirs[i], opts, &total_files, &head);
    }
    
    if (!head) {
        return NULL;
    }
    
    /* Allocate sorted array */
    sorted_files = malloc(sizeof(ref_files_t));
    if (!sorted_files) {
        free_file_list(head);
        return NULL;
    }
    
    sorted_files->files = malloc(sizeof(file_info_t *) * total_files);
    if (!sorted_files->files) {
        free(sorted_files);
        free_file_list(head);
        return NULL;
    }
    
    sorted_files->count = total_files;
    sorted_files->capacity = total_files;
    
    /* Transfer files from linked list to array */
    current = head;
    for (int i = 0; i < total_files && current; i++) {
        file_info_t *next = current->next;
        current->next = NULL;
        sorted_files->files[i] = current;
        current = next;
    }

    /* Sort once after all files are added */
    qsort(sorted_files->files, sorted_files->count, sizeof(file_info_t *), compare_file_info_size);

    /* Calculate unique size statistics */
    if (stats && sorted_files->count > 0) {
        stats->total_ref_files = sorted_files->count;
        stats->unique_ref_sizes = 1; /* First file always has a unique size relative to nothing */
        
        for (int i = 1; i < sorted_files->count; i++) {
            if (sorted_files->files[i]->size != sorted_files->files[i-1]->size) {
                stats->unique_ref_sizes++;
            }
        }
    }
    
    return sorted_files;
}

/* Updates the global hash cache stattstics */
static void update_hash_stats(stats_t *stats, file_info_t *file) {
    if (file->block_hashes.cached_blocks > 0) {
        stats->total_cache_depth += file->block_hashes.cached_blocks;
        if (file->block_hashes.cached_blocks < stats->min_cache_depth || stats->min_cache_depth == 0) {
            stats->min_cache_depth = file->block_hashes.cached_blocks;
        }
        if (file->block_hashes.cached_blocks > stats->max_cache_depth) {
            stats->max_cache_depth = file->block_hashes.cached_blocks;
        }
    }
}

/* Finds a file in the ser of reference files that matches the source file.
   This is the main matching algorithm. */
file_info_t *find_matching_file(ref_files_t *ref_files, const char *src_file, const options_t *opts, stats_t *stats) {
    struct stat st;

    if (ref_files == NULL || ref_files->count == 0) {
        return NULL; /* No reference files available */
    }

    if (stat(src_file, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat source file %s\n", src_file);
        return NULL;
    }

    /* Create file_info_t structure for source file */
    file_info_t src_info;
    src_info.path = (char *)src_file;
    src_info.size = st.st_size;
    src_info.next = NULL;

    /* Set basename as pointer into path */
    src_info.basename = strrchr(src_info.path, '/');
    src_info.basename = src_info.basename ? src_info.basename + 1 : src_info.path;

    init_block_hashes(&src_info.block_hashes);

    file_info_t *match = NULL;

    stats->total_source_files++; /* TODO: This shouldn't be here!*/ 
    
    /* Find first file with matching size */
    int first_match = find_first_size_match(ref_files, st.st_size);
    if (first_match == -1) {
        return NULL;  /* No files with matching size found */
    }

    /* Check all files with the same size starting from first_match */
    for (int i = first_match; i < ref_files->count && ref_files->files[i]->size == st.st_size; i++) {
        file_info_t *current = ref_files->files[i];

        /* If name matching is enabled, check if basenames match */
        if (opts->match_name && strcmp(src_info.basename, current->basename) != 0) {
            continue; /* Names don't match, skip this file */
        }

        /* At this point we have a size match (and name match if enabled) */
        /* Decide whether to verify contents */
        if (opts->no_verify) {
            /* Accept match based on size and name only, no content verification */
            if (opts->verbose) {
                printf("Match found (size+name, no verification): %s matches %s\n", src_file, current->path);
            }
            match = current;
            break;
        }

        /* Perform content verification */
        stats->files_compared++;

        switch (files_match(current, &src_info)) {
            case MATCH_SUCCESS:
                /* Match found */
                if (opts->verbose) {
                    printf("Match found: %s matches %s\n", src_file, current->path);
                }
                match = current;
                break;
            case MATCH_FAIL_HASHES:
                /* Fast rejection via cached blocks - Files don't match */
                stats->cache_hits++;
                break;
            case MATCH_FAIL_BYTEWISE:
                /* Failed during bytewise comparison - Files don't match */
                break;
            case MATCH_ERROR:
                fprintf(stderr, "Error: File comparison failed between %s and %s\n",
                        src_file, current->path);
                break;
        }

    }

    update_hash_stats(stats, &src_info);
    return match;
}

void free_file_list(file_info_t *list) {
    file_info_t *current = list;
    file_info_t *next;
    
    while (current) {
        next = current->next;
        free(current->path);
        free_block_hashes(&current->block_hashes);
        free(current);
        current = next;
    }
}

void free_sorted_file_info(ref_files_t *sorted_files) {
    if (!sorted_files) return;
    
    /* Free all file_info_t objects */
    for (int i = 0; i < sorted_files->count; i++) {
        if (sorted_files->files[i]) {
            free(sorted_files->files[i]->path);
            free_block_hashes(&sorted_files->files[i]->block_hashes);
            free(sorted_files->files[i]);
        }
    }
    
    /* Free the array of pointers and the structure itself */
    free(sorted_files->files);
    free(sorted_files);
}
