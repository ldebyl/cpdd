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
 * Helper function to recursively collect file paths and sizes
 */
static file_info_t *collect_file_info(const char *ref_dir) {
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    file_info_t *head = NULL;
    file_info_t *current = NULL;
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
            file_info_t *subdir_files = collect_file_info(full_path);
            if (subdir_files) {
                if (!head) {
                    head = subdir_files;
                    current = head;
                    while (current->next) {
                        current = current->next;
                    }
                } else {
                    current->next = subdir_files;
                    while (current->next) {
                        current = current->next;
                    }
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            file_info_t *new_file = malloc(sizeof(file_info_t));
            if (!new_file) {
                continue;
            }
            
            new_file->path = strdup(full_path);
            new_file->size = st.st_size;
            new_file->next = NULL;
            /* MD5 is calculated later, only for files with non-unique sizes */
            memset(new_file->md5, 0, MD5_DIGEST_LENGTH);
            
            if (!head) {
                head = new_file;
                current = head;
            } else {
                current->next = new_file;
                current = new_file;
            }
        }
    }
    
    closedir(dir);
    return head;
}

/*
 * Check if a file size appears multiple times in the list
 */
static int size_has_duplicates(file_info_t *list, off_t size) {
    int count = 0;
    file_info_t *current = list;
    
    while (current && count < 2) {
        if (current->size == size) {
            count++;
        }
        current = current->next;
    }
    
    return count > 1;
}

/*
 * Recursively scan reference directory and build linked list of file metadata.
 * Uses two-pass optimization: first pass collects file sizes, second pass
 * calculates MD5 only for files with non-unique sizes.
 * Returns linked list of file_info_t structures, or NULL on error.
 */
file_info_t *scan_reference_directory(const char *ref_dir) {
    file_info_t *head;
    file_info_t *current;
    
    /* First pass: collect all files with sizes only */
    head = collect_file_info(ref_dir);
    if (!head) {
        return NULL;
    }
    
    /* Second pass: calculate MD5 only for files with non-unique sizes */
    current = head;
    while (current) {
        /* Only calculate MD5 if this size appears multiple times */
        if (size_has_duplicates(head, current->size)) {
            if (calculate_md5(current->path, current->md5) != 0) {
                /* Remove this file from list on MD5 calculation failure */
                /* For simplicity, we'll keep it but mark MD5 as invalid */
                memset(current->md5, 0, MD5_DIGEST_LENGTH);
            }
        }
        /* Files with unique sizes keep zero MD5 - they'll never match anyway */
        current = current->next;
    }
    
    return head;
}

file_info_t *find_matching_file(file_info_t *ref_files, const char *src_file, const options_t *opts) {
    struct stat st;
    unsigned char src_md5[MD5_DIGEST_LENGTH];
    file_info_t *current = ref_files;

    if (stat(src_file, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat source file %s\n", src_file);
        return NULL;
    }

    if (calculate_md5(src_file, src_md5) != 0) {
        fprintf(stderr, "Error: Failed to calculate MD5 for source file %s\n", src_file);
        return NULL;
    }

    while (current) {
        printf("Checking %s against %s\n", src_file, current->path);
        if (current->size == st.st_size) {
            printf("Size match: %s (%lld bytes) vs %s (%lld bytes)\n",
                   src_file, (long long)st.st_size, current->path, (long long)current->size);
            // Check the MD5 sum before comparing contents
            // Reference files with a unique size in the collection
            // will have a zero MD5, so we check for that too. This avoids
            // calculating MD5 sums where a file will only ever be compared
            // against files with the same size. (and if we are reading a source
            // file to calculate its MD5, we might as well skip to the bytewise comparison)
            if (memcmp(current->md5, src_md5, MD5_DIGEST_LENGTH) == 0 ||
                memcmp(current->md5, (unsigned char[MD5_DIGEST_LENGTH]){0}, MD5_DIGEST_LENGTH) == 0) {
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
            } else {
                if (opts->verbose) {
                    printf("MD5 mismatch: %s and %s\n", src_file, current->path);
                    // print the md5 hashes for debugging
                    printf("MD5 of %s: ", src_file);
                    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
                        printf("%02x", src_md5[i]);
                    printf("\nMD5 of %s: ", current->path);
                    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
                        printf("%02x", current->md5[i]);
                    printf("\n");
                }
            }
        } else {
            if (opts->verbose) {
                printf("Size mismatch: %s (%lld bytes) and %s (%lld bytes)\n",
                       src_file, (long long)st.st_size, current->path, (long long)current->size);
            }
        }
        current = current->next;
    }

    if (opts->verbose) {
        printf("No match found for %s\n", src_file);
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