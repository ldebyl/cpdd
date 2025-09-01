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

int calculate_md5(const char *filename, unsigned char *digest) {
    FILE *file;
    MD5_CTX ctx;
    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    file = fopen(filename, "rb");
    if (!file) {
        return -1;
    }
    
    MD5_Init(&ctx);
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        MD5_Update(&ctx, buffer, bytes_read);
    }
    
    MD5_Final(digest, &ctx);
    fclose(file);
    
    return 0;
}

int files_identical(const char *file1, const char *file2) {
    FILE *f1, *f2;
    unsigned char buffer1[BUFFER_SIZE], buffer2[BUFFER_SIZE];
    size_t bytes1, bytes2;
    int result = 1;
    
    f1 = fopen(file1, "rb");
    f2 = fopen(file2, "rb");
    
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }
    
    do {
        bytes1 = fread(buffer1, 1, BUFFER_SIZE, f1);
        bytes2 = fread(buffer2, 1, BUFFER_SIZE, f2);
        
        if (bytes1 != bytes2 || memcmp(buffer1, buffer2, bytes1) != 0) {
            result = 0;
            break;
        }
    } while (bytes1 > 0);
    
    fclose(f1);
    fclose(f2);
    
    return result;
}

/*
 * Helper function to recursively collect file paths and sizes portably across operating systems.
 */
static file_info_t *collect_file_info(const char *ref_dir, const options_t *opts, int *count) {
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    file_info_t *head = NULL;
    char full_path[MAX_PATH];
    dir = opendir(ref_dir);
    if (!dir) {
        return NULL;
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
            file_info_t *subdir_files = collect_file_info(full_path, opts, count);
            if (subdir_files) {
                file_info_t *subdir_tail = subdir_files;
                while (subdir_tail->next) {
                    subdir_tail = subdir_tail->next;
                }
                subdir_tail->next = head;
                head = subdir_files;
            }
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
            /* MD5 will be calculated lazily during comparison */
            memset(new_file->md5, 0, MD5_DIGEST_LENGTH);
            new_file->needs_md5 = 0; /* Will be set later */
            new_file->has_md5 = 0;   /* No MD5 calculated yet */
            
            new_file->next = head;
            head = new_file;
            (*count)++;
        }
    }
    
    closedir(dir);
    if (opts->verbose == 1) {
        print_status_update("\rScanned %d reference files in %s", *count, ref_dir);
        fflush(stdout);
    }
    return head;
}

/*
 * Sorted array structure for file info objects
 */
typedef struct {
    file_info_t **files;
    int count;
    int capacity;
} sorted_file_info_t;

static sorted_file_info_t *sorted_file_info_init(int initial_capacity) {
    sorted_file_info_t *list = malloc(sizeof(sorted_file_info_t));
    if (!list) return NULL;
    list->files = malloc(sizeof(file_info_t *) * initial_capacity);
    if (!list->files) {
        free(list);
        return NULL;
    }
    list->count = 0;
    list->capacity = initial_capacity;
    return list;
}

static int compare_file_info_size(const void *a, const void *b) {
    file_info_t *file_a = *(file_info_t **)a;
    file_info_t *file_b = *(file_info_t **)b;
    return (file_a->size > file_b->size) - (file_a->size < file_b->size);
}

static int sorted_file_info_contains_size(sorted_file_info_t *list, off_t size) {
    if (list->count == 0) return 0;
    
    /* Binary search for size */
    int left = 0, right = list->count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (list->files[mid]->size == size) {
            return 1;
        } else if (list->files[mid]->size < size) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return 0;
}

static void sorted_file_info_add(sorted_file_info_t *list, file_info_t *file) {
    /* Resize if needed */
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        file_info_t **new_files = realloc(list->files, sizeof(file_info_t *) * list->capacity);
        if (!new_files) {
            return; /* Failed to resize - keep original array */
        }
        list->files = new_files;
    }
    
    /* Add and re-sort */
    list->files[list->count++] = file;
    qsort(list->files, list->count, sizeof(file_info_t *), compare_file_info_size);
}

static void sorted_file_info_free(sorted_file_info_t *list) {
    if (list) {
        free(list->files);
        free(list);
    }
}

/*
 * Recursively scan reference directory and build linked list of file metadata.
 * Uses lazy MD5 calculation optimization: first pass collects file sizes and marks
 * files that need MD5 (those with duplicate sizes), but doesn't calculate MD5 yet.
 * MD5 is calculated lazily during the first comparison attempt.
 * Returns linked list of file_info_t structures, or NULL on error.
 */
file_info_t *scan_reference_directory(const options_t *opts) {
    file_info_t *head;
    file_info_t *current;
    sorted_file_info_t *seen_files;
    sorted_file_info_t *duplicate_files;
    
    /* First pass: collect all files with sizes only.
     * Note that although ref_dir is included in opts, collect_file_info is called
     * recursively with different ref_dirs, hence it is passed as a separate parameter.
     * This allows opts to be kept constant.
     */
    int total_files = 0;
    head = collect_file_info(opts->ref_dir, opts, &total_files);
    if (!head) {
        return NULL;
    }
    
    /* Initialize sorted arrays for tracking file info objects */
    seen_files = sorted_file_info_init(total_files);
    duplicate_files = sorted_file_info_init(total_files / 4); /* estimate fewer duplicates */
    if (!seen_files || !duplicate_files) {
        sorted_file_info_free(seen_files);
        sorted_file_info_free(duplicate_files);
        free_file_list(head);
        return NULL;
    }
    
    /* Build lists of seen and duplicate file info objects */
    current = head;
    while (current) {
        if (sorted_file_info_contains_size(seen_files, current->size)) {
            /* Already seen this size - it's a duplicate */
            sorted_file_info_add(duplicate_files, current);
        } else {
            /* First time seeing this size */
            sorted_file_info_add(seen_files, current);
        }
        current = current->next;
    }
    
    /* Second pass: mark files that need MD5 using binary search */
    current = head;
    while (current) {
        current->needs_md5 = sorted_file_info_contains_size(duplicate_files, current->size);
        current = current->next;
    }
    
    /* Cleanup sorted arrays */
    sorted_file_info_free(seen_files);
    sorted_file_info_free(duplicate_files);
    
    return head;
}

file_info_t *find_matching_file(file_info_t *ref_files, const char *src_file, const options_t *opts) {
    struct stat st;
    unsigned char src_md5[MD5_DIGEST_LENGTH];
    int src_md5_calculated = 0;
    file_info_t *current = ref_files;

    if (stat(src_file, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat source file %s\n", src_file);
        return NULL;
    }

    while (current) {
        if (current->size == st.st_size) {
            /* If reference file doesn't need MD5 (unique size), skip MD5 and go to byte comparison */
            if (!current->needs_md5) {
                if (files_identical(current->path, src_file)) {
                    if (opts->verbose) {
                        printf("Match found: %s matches %s\n", src_file, current->path);
                    }
                    return current;
                }
            } else {
                /* Reference file needs MD5 - calculate it lazily if not already done */
                if (!current->has_md5) {
                    if (calculate_md5(current->path, current->md5) != 0) {
                        fprintf(stderr, "Error: Failed to calculate MD5 for reference file %s\n", current->path);
                        /* Mark as having MD5 to avoid repeated failures */
                        current->has_md5 = 1;
                        current = current->next;
                        continue;
                    }
                    current->has_md5 = 1;
                }
                
                /* Calculate source MD5 if not already done */
                if (!src_md5_calculated) {
                    if (calculate_md5(src_file, src_md5) != 0) {
                        fprintf(stderr, "Error: Failed to calculate MD5 for source file %s\n", src_file);
                        return NULL;
                    }
                    src_md5_calculated = 1;
                }
                
                /* Compare MD5 hashes first */
                if (memcmp(current->md5, src_md5, MD5_DIGEST_LENGTH) == 0) {
                    /* MD5 matches, do full byte comparison to be sure */
                    if (files_identical(current->path, src_file)) {
                        if (opts->verbose) {
                            printf("Match found: %s matches %s\n", src_file, current->path);
                        }
                        return current;
                    } else {
                        if (opts->verbose) {
                            printf("MD5 matched but content differs: %s and %s\n", src_file, current->path);
                        }
                    }
                }
            }
        }
        current = current->next;
    }

    return NULL;
}

void free_file_list(file_info_t *list) {
    file_info_t *current = list;
    file_info_t *next;
    
    while (current) {
        next = current->next;
        free(current->path);
        free(current);
        current = next;
    }
}
