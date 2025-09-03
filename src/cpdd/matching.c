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

/* Determines if two files are bytewise identical. */
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

/* Efficiently determines if two files are identical.
 * 1. Checks if sizes match (should always be true when called)
 * 2. If both have MD5, compare hashes first, then byte compare if they match
 * 3. If neither needs MD5 (reference file size is unique), just do byte compare
 * 4. If at least one needs MD5, read both files in chunks, updating MD5 as needed,
 *    and comparing bytes until a mismatch is found or EOF is reached.
 * 5. Finalize MD5 for files that need it for futuer comparisons.
 */
int files_match(file_info_t *ref_file, file_info_t *src_file) {
    /* Files should always have the same size when this function is called */
    if (ref_file->size != src_file->size) {
        fprintf(stderr, "Internal error: files_match called with different sized files\n");
        return 0;
    }
    
    /* If both files have MD5, compare hashes first */
    if (ref_file->has_md5 && src_file->has_md5) {
        if (memcmp(ref_file->md5, src_file->md5, MD5_DIGEST_LENGTH) != 0) {
            return 0;
        }
        /* MD5 matches, do byte comparison to be certain */
        return files_identical(ref_file->path, src_file->path);
    }
    
    /* If neither file needs MD5 (both unique sizes), just do byte comparison */
    if (!ref_file->needs_md5 && !src_file->needs_md5) {
        return files_identical(ref_file->path, src_file->path);
    }
    
    /* At least one file needs MD5 calculation - do it while comparing bytes */
    FILE *ref_fp = fopen(ref_file->path, "rb");
    FILE *src_fp = fopen(src_file->path, "rb");
    if (!ref_fp || !src_fp) {
        if (ref_fp) fclose(ref_fp);
        if (src_fp) fclose(src_fp);
        return 0;
    }
    
    MD5_CTX ref_ctx, src_ctx;
    int calc_ref_md5 = ref_file->needs_md5 && !ref_file->has_md5;
    int calc_src_md5 = src_file->needs_md5 && !src_file->has_md5;
    
    if (calc_ref_md5) MD5_Init(&ref_ctx);
    if (calc_src_md5) MD5_Init(&src_ctx);
    
    unsigned char ref_buffer[BUFFER_SIZE], src_buffer[BUFFER_SIZE];
    size_t ref_bytes, src_bytes;
    int files_match = 1;
    
    do {
        ref_bytes = fread(ref_buffer, 1, BUFFER_SIZE, ref_fp);
        src_bytes = fread(src_buffer, 1, BUFFER_SIZE, src_fp);
        
        /* Update MD5 for files that need it */
        if (calc_ref_md5 && ref_bytes > 0) {
            MD5_Update(&ref_ctx, ref_buffer, ref_bytes);
        }
        if (calc_src_md5 && src_bytes > 0) {
            MD5_Update(&src_ctx, src_buffer, src_bytes);
        }
        
        /* Compare bytes, until a mismatch has been found (then just generate MD5) */
        if (files_match) {
            if (ref_bytes != src_bytes || memcmp(ref_buffer, src_buffer, ref_bytes) != 0) {
                files_match = 0;
                // Don't break here - continue to read to end for MD5 calculation
            }
        }
    } while (ref_bytes > 0);
    
    /* Finalize MD5 for files that needed it */
    if (calc_ref_md5) {
        MD5_Final(ref_file->md5, &ref_ctx);
        ref_file->has_md5 = 1;
    }
    if (calc_src_md5) {
        MD5_Final(src_file->md5, &src_ctx);
        src_file->has_md5 = 1;
    }
    
    fclose(ref_fp);
    fclose(src_fp);
    
    return files_match;
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

            /* MD5 will be calculated lazily during comparison */
            memset(new_file->md5, 0, MD5_DIGEST_LENGTH);
            new_file->needs_md5 = 0; /* Will be set later */
            new_file->has_md5 = 0;   /* No MD5 calculated yet */
            
            new_file->next = *head;
            *head = new_file;
            (*count)++;
        }
    }
    
    closedir(dir);
    if (opts->verbose == 1) {
        print_status_update("\rScanned %d reference files in %s", *count, ref_dir);
        fflush(stdout);
    }
}

/*
 * Sorted array functions for file info objects
 */

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
    
    /* Just add - don't sort yet */
    list->files[list->count++] = file;
}


/*
 * Recursively scan reference directory and build sorted array of file metadata.
 * Uses lazy MD5 calculation optimization: first pass collects file sizes and marks
 * files that need MD5 (those with duplicate sizes), but doesn't calculate MD5 yet.
 * MD5 is calculated lazily during the first comparison attempt.
 * Returns sorted_file_info_t structure with array of file_info_t pointers, or NULL on error.
 */
sorted_file_info_t *scan_reference_directory(const options_t *opts) {
    file_info_t *head = NULL;
    file_info_t *current;
    sorted_file_info_t *sorted_files;
    
    /* First pass: collect all files from all reference directories */
    int total_files = 0;
    
    for (int i = 0; i < opts->ref_dir_count; i++) {
        collect_file_info(opts->ref_dirs[i], opts, &total_files, &head);
    }
    
    if (!head) {
        return NULL;
    }
    
    /* Initialize sorted array for file info objects */
    sorted_files = sorted_file_info_init(total_files);
    if (!sorted_files) {
        free_file_list(head);
        return NULL;
    }
    
    /* Add all files to array */
    current = head;
    while (current) {
        file_info_t *next = current->next;
        current->next = NULL; /* Break the linked list connection */
        sorted_file_info_add(sorted_files, current);
        current = next;
    }
    /* Don't free anything - the file_info_t objects are now owned by sorted_files */

    /* Sort once after all files are added */
    qsort(sorted_files->files, sorted_files->count, sizeof(file_info_t *), compare_file_info_size);

    /* Mark files that need MD5 by checking for duplicate sizes in sorted array */
    for (int i = 0; i < sorted_files->count; i++) {
        file_info_t *file = sorted_files->files[i];
        file->needs_md5 = 0; /* Default to not needing MD5 */
        
        /* Check if this file has the same size as the previous or next file */
        if (i > 0 && file->size == sorted_files->files[i - 1]->size) {
            file->needs_md5 = 1;
            /* Also mark the previous file if not already marked */
            if (!sorted_files->files[i - 1]->needs_md5) {
                sorted_files->files[i - 1]->needs_md5 = 1;
            }
        } else if (i + 1 < sorted_files->count && file->size == sorted_files->files[i + 1]->size) {
            file->needs_md5 = 1;
        }
    }
    
    return sorted_files;
}

file_info_t *find_matching_file(sorted_file_info_t *ref_files, const char *src_file, const options_t *opts) {
    struct stat st;

    if (stat(src_file, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat source file %s\n", src_file);
        return NULL;
    }

    /* Create file_info_t structure for source file */
    file_info_t src_info;
    src_info.path = (char *)src_file; /* Cast away const - we won't modify it */
    src_info.size = st.st_size;
    memset(src_info.md5, 0, MD5_DIGEST_LENGTH);
    src_info.needs_md5 = 0; /* Will be set based on reference files */
    src_info.has_md5 = 0;
    src_info.next = NULL;

    /* Binary search for the first file with matching size */
    int left = 0, right = ref_files->count - 1;
    int first_match = -1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (ref_files->files[mid]->size == st.st_size) {
            first_match = mid;
            right = mid - 1; /* Continue searching left for first occurrence */
        } else if (ref_files->files[mid]->size < st.st_size) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    /* No files with matching size found */
    if (first_match == -1) {
        return NULL;
    }
    
    /* Check all files with the same size starting from first_match */
    for (int i = first_match; i < ref_files->count && ref_files->files[i]->size == st.st_size; i++) {
        file_info_t *current = ref_files->files[i];
        
        /* Set source file needs_md5 based on reference file */
        src_info.needs_md5 = current->needs_md5;
        
        /* Use our new files_match function */
        if (files_match(current, &src_info)) {
            if (opts->verbose) {
                printf("Match found: %s matches %s\n", src_file, current->path);
            }
            return current;
        }
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

void free_sorted_file_info(sorted_file_info_t *sorted_files) {
    if (!sorted_files) return;
    
    /* Free all file_info_t objects */
    for (int i = 0; i < sorted_files->count; i++) {
        if (sorted_files->files[i]) {
            free(sorted_files->files[i]->path);
            free(sorted_files->files[i]);
        }
    }
    
    /* Free the array of pointers and the structure itself */
    free(sorted_files->files);
    free(sorted_files);
}
