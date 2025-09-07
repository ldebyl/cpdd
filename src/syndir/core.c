/*
 * cpdd/testgen_core.c - Content-based copy with deduplication
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

#include "syndir.h"
#include <math.h>

static unsigned int rand_seed = 0;

size_t generate_file_size(size_t p50, size_t p95, size_t p100)
{
    /* Box-Muller transform to generate normal distribution */
    static int has_spare = 0;
    static double spare;

    double normal;
    if (has_spare)
    {
        has_spare = 0;
        normal = spare;
    }
    else
    {
        has_spare = 1;
        double u = ((double)rand() / RAND_MAX) * 0.99 + 0.005; /* Avoid exactly 0 or 1 */
        double v = ((double)rand() / RAND_MAX) * 0.99 + 0.005;
        double mag = sqrt(-2.0 * log(u));
        spare = mag * cos(2.0 * M_PI * v);
        normal = mag * sin(2.0 * M_PI * v);
    }

    /* Convert percentiles to normal distribution parameters */
    /* For normal distribution: p95 ≈ μ + 1.645σ, p50 = μ */
    double mu = (double)p50;
    double sigma = (double)(p95 - p50) / 1.645;

    /* Apply transformation: size = μ + σ * normal */
    /* Use absolute value to avoid negative sizes, then apply to mu */
    double size_d = mu + sigma * fabs(normal);

    /* Clamp to reasonable bounds with better minimum */
    if (size_d < (double)p50 * 0.1)
        size_d = (double)p50 * 0.1; /* At least 10% of median */
    if (size_d > (double)p100)
        size_d = (double)p100;

    return (size_t)size_d;
}

size_t generate_bucketed_size(size_t p50, size_t p95, size_t p100, int num_buckets)
{
    if (num_buckets <= 0) {
        /* Fall back to normal distribution */
        return generate_file_size(p50, p95, p100);
    }
    
    /* Create size buckets between p50 and p100 */
    size_t min_size = p50;
    size_t max_size = p100;
    size_t range = max_size - min_size;
    
    /* Pick a random bucket */
    int bucket = rand() % num_buckets;
    
    /* Calculate size for this bucket */
    size_t bucket_size = min_size + (bucket * range) / num_buckets;
    
    /* Ensure bounds */
    if (bucket_size < MIN_CONTENT_SIZE) bucket_size = MIN_CONTENT_SIZE;
    if (bucket_size > max_size) bucket_size = max_size;
    
    return bucket_size;
}

char *generate_random_content(size_t size)
{
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 \n\t.,!?-_";
    static const size_t charset_size = sizeof(charset) - 1; /* -1 for null terminator */

    char *content = malloc(size + 1);
    if (!content)
        return NULL;

    /* Generate random data in larger chunks for better performance */
    for (size_t i = 0; i < size; i += 4)
    {
        unsigned int r = (unsigned int)rand();
        size_t remaining = (size - i < 4) ? (size - i) : 4;

        for (size_t j = 0; j < remaining; j++)
        {
            content[i + j] = charset[(r >> (j * 8)) % charset_size];
        }
    }
    content[size] = '\0';

    return content;
}

char *generate_similar_content(const char *base_content, size_t size, double similarity, similarity_pattern_t pattern)
{
    if (!base_content || similarity < 0.0 || similarity > 1.0)
        return NULL;
    
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 \n\t.,!?-_";
    static const size_t charset_size = sizeof(charset) - 1;
    
    char *content = malloc(size + 1);
    if (!content) return NULL;
    
    switch (pattern) {
        case SIMILARITY_EXACT:
            /* Perfect copy regardless of similarity value */
            memcpy(content, base_content, size);
            break;
            
        case SIMILARITY_PREFIX:
            /* Keep first similarity% identical, randomize the rest */
            if (similarity >= 1.0) {
                memcpy(content, base_content, size);
            } else if (similarity <= 0.0) {
                /* Generate completely random */
                for (size_t i = 0; i < size; i++) {
                    content[i] = charset[rand() % charset_size];
                }
            } else {
                size_t keep_bytes = (size_t)(similarity * size);
                memcpy(content, base_content, keep_bytes);
                for (size_t i = keep_bytes; i < size; i++) {
                    content[i] = charset[rand() % charset_size];
                }
            }
            break;
            
        case SIMILARITY_SUFFIX:
            /* Keep last similarity% identical, randomize the beginning */
            if (similarity >= 1.0) {
                memcpy(content, base_content, size);
            } else if (similarity <= 0.0) {
                /* Generate completely random */
                for (size_t i = 0; i < size; i++) {
                    content[i] = charset[rand() % charset_size];
                }
            } else {
                size_t keep_bytes = (size_t)(similarity * size);
                size_t random_bytes = size - keep_bytes;
                /* Generate random content for beginning */
                for (size_t i = 0; i < random_bytes; i++) {
                    content[i] = charset[rand() % charset_size];
                }
                /* Copy similar content for end */
                memcpy(content + random_bytes, base_content + random_bytes, keep_bytes);
            }
            break;
            
        case SIMILARITY_RANDOM:
        default:
            /* Generate completely random content */
            for (size_t i = 0; i < size; i++) {
                content[i] = charset[rand() % charset_size];
            }
            break;
    }
    
    content[size] = '\0';
    return content;
}

char *generate_random_filename(const char *prefix)
{
    char *filename = malloc(256);
    if (!filename)
        return NULL;

    snprintf(filename, 256, "%s_%08x_%04x.txt",
             prefix, rand(), rand() % 10000);

    return filename;
}

int create_directory_tree(const char *root, int num_dirs)
{
    char path[MAX_PATH];

    if (mkdir(root, 0755) != 0 && errno != EEXIST)
    {
        return -1;
    }

    for (int i = 0; i < num_dirs; i++)
    {
        int depth = (rand() % 3) + 1;
        char subpath[MAX_PATH];
        strcpy(subpath, root);

        for (int d = 0; d < depth; d++)
        {
            snprintf(path, sizeof(path), "%s/dir_%d_%d", subpath, i, d);
            if (mkdir(path, 0755) != 0 && errno != EEXIST)
            {
                continue;
            }
            strcpy(subpath, path);
        }
    }

    return 0;
}

static char *choose_random_directory(const char *root)
{
    char find_cmd[MAX_PATH * 2];
    char *result = malloc(MAX_PATH);
    FILE *fp;

    if (!result)
        return NULL;

    snprintf(find_cmd, sizeof(find_cmd), "find '%s' -type d 2>/dev/null | head -20", root);
    fp = popen(find_cmd, "r");
    if (!fp)
    {
        free(result);
        return strdup(root);
    }

    char directories[20][MAX_PATH];
    int dir_count = 0;

    while (fgets(directories[dir_count], MAX_PATH, fp) && dir_count < 20)
    {
        char *newline = strchr(directories[dir_count], '\n');
        if (newline)
            *newline = '\0';
        dir_count++;
    }
    pclose(fp);

    if (dir_count == 0)
    {
        free(result);
        return strdup(root);
    }

    strcpy(result, directories[rand() % dir_count]);
    return result;
}

int create_reference_directory(const char *root, int num_files, int num_dirs,
                               file_entry_t **file_list, const options_t *opts)
{
    file_entry_t *head = NULL;
    file_entry_t *current = NULL;

    if (opts->verbose)
    {
        printf("Creating reference directory: %s\n", root);
        printf("  Files: %d, Directories: %d\n", num_files, num_dirs);
    }

    if (create_directory_tree(root, num_dirs) != 0)
    {
        fprintf(stderr, "Error: Failed to create directory tree in %s\n", root);
        return -1;
    }

    for (int i = 0; i < num_files; i++)
    {
        file_entry_t *entry = malloc(sizeof(file_entry_t));
        if (!entry)
            continue;

        char *dir = choose_random_directory(root);
        char *filename = generate_random_filename("ref");
        char full_path[MAX_PATH];

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);

        size_t content_size;
        if (opts->size_buckets > 0) {
            content_size = generate_bucketed_size(
                (size_t)(opts->size_p50 * opts->size_scale),
                (size_t)(opts->size_p95 * opts->size_scale),
                (size_t)(opts->size_p100 * opts->size_scale),
                opts->size_buckets);
        } else {
            content_size = generate_file_size(
                (size_t)(opts->size_p50 * opts->size_scale),
                (size_t)(opts->size_p95 * opts->size_scale),
                (size_t)(opts->size_p100 * opts->size_scale));
        }
        char *content = generate_random_content(content_size);

        entry->path = strdup(full_path);
        entry->content = content;
        entry->content_size = content_size;
        entry->next = NULL;

        if (!head)
        {
            head = entry;
            current = entry;
        }
        else
        {
            current->next = entry;
            current = entry;
        }

        FILE *f = fopen(full_path, "w");
        if (f)
        {
            fwrite(content, 1, content_size, f);
            fclose(f);

            if (opts->verbose)
            {
                printf("  Created reference file: %s (%zu bytes)\n", full_path, content_size);
            }
            else if ((i + 1) % 10 == 0)
            {
                if (opts->verbose == 0) {
                    print_status_update("Created %d/%d reference files", i + 1, num_files);
                } else {
                    printf("  Created %d/%d reference files\n", i + 1, num_files);
                }
            }
        }
        else
        {
            fprintf(stderr, "Warning: Could not create file %s\n", full_path);
        }

        free(dir);
        free(filename);
    }

    *file_list = head;
    
    /* Clear status line if we were showing progress updates */
    if (opts->verbose == 0) {
        clear_status_line();
    }
    
    return 0;
}

// Helper function to select a random reference file
static file_entry_t *select_random_reference(file_entry_t *ref_files, int ref_count)
{
    if (!ref_files || ref_count <= 0)
        return NULL;

    int ref_index = rand() % ref_count;
    file_entry_t *current = ref_files;

    for (int i = 0; i < ref_index && current; i++)
    {
        current = current->next;
    }

    return current;
}

int create_source_directory(const char *root, int num_files, int num_dirs,
                            file_entry_t *ref_files, const options_t *opts)
{

    if (opts->verbose)
    {
        printf("Creating source directory: %s\n", root);
        printf("  Files: %d, Directories: %d, Duplicates: %d%%\n",
               num_files, num_dirs, opts->duplicate_percent);
    }

    if (create_directory_tree(root, num_dirs) != 0)
    {
        fprintf(stderr, "Error: Failed to create directory tree in %s\n", root);
        return -1;
    }

    int num_duplicates = (num_files * opts->duplicate_percent) / 100;
    int duplicates_created = 0;

    file_entry_t *ref_current = ref_files;
    int ref_count = 0;
    while (ref_current)
    {
        ref_count++;
        ref_current = ref_current->next;
    }

    for (int i = 0; i < num_files; i++)
    {
        char *dir = choose_random_directory(root);
        char *filename = generate_random_filename("src");
        char full_path[MAX_PATH];

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);

        FILE *f = fopen(full_path, "w");
        if (!f)
        {
            fprintf(stderr, "Warning: Could not create file %s\n", full_path);
            free(dir);
            free(filename);
            continue;
        }

        if (duplicates_created < num_duplicates && ref_count > 0)
        {
            // Use the helper function to select a random reference file
            file_entry_t *selected_ref = select_random_reference(ref_files, ref_count);

            if (selected_ref)
            {
                // Determine similarity pattern based on distribution percentages
                similarity_pattern_t pattern;
                int pattern_rand = rand() % 100;
                
                if (pattern_rand < opts->exact_percent) {
                    pattern = SIMILARITY_EXACT;
                } else if (pattern_rand < opts->exact_percent + opts->prefix_percent) {
                    pattern = SIMILARITY_PREFIX;
                } else if (pattern_rand < opts->exact_percent + opts->prefix_percent + opts->suffix_percent) {
                    pattern = SIMILARITY_SUFFIX;
                } else {
                    pattern = SIMILARITY_RANDOM;
                }
                
                // Determine final size with some variation to create more realistic test scenarios
                size_t final_size = selected_ref->content_size;
                int size_variation = rand() % 3; // 0=same size, 1=truncate, 2=pad
                
                if (size_variation == 1) {
                    // Truncate by 10-30%
                    double truncate_factor = 0.1 + (rand() % 21) / 100.0; // 0.1 to 0.3
                    final_size = (size_t)(final_size * (1.0 - truncate_factor));
                    if (final_size < MIN_CONTENT_SIZE) final_size = MIN_CONTENT_SIZE;
                } else if (size_variation == 2) {
                    // Pad by 10-30%
                    double pad_factor = 0.1 + (rand() % 21) / 100.0; // 0.1 to 0.3
                    final_size = (size_t)(final_size * (1.0 + pad_factor));
                }
                
                // Generate similar content based on pattern
                char *similar_content = generate_similar_content(selected_ref->content, 
                                                               selected_ref->content_size, 
                                                               opts->similarity,
                                                               pattern);
                
                if (similar_content) {
                    // Adjust content size if needed
                    if (final_size != selected_ref->content_size) {
                        char *adjusted_content = malloc(final_size + 1);
                        if (adjusted_content) {
                            if (final_size < selected_ref->content_size) {
                                // Truncate
                                memcpy(adjusted_content, similar_content, final_size);
                            } else {
                                // Pad with random data
                                memcpy(adjusted_content, similar_content, selected_ref->content_size);
                                // Fill the rest with random content
                                char *padding = generate_random_content(final_size - selected_ref->content_size);
                                if (padding) {
                                    memcpy(adjusted_content + selected_ref->content_size, padding, 
                                           final_size - selected_ref->content_size);
                                    free(padding);
                                }
                            }
                            adjusted_content[final_size] = '\0';
                            free(similar_content);
                            similar_content = adjusted_content;
                        }
                    }
                    // Debugging output to verify reference file selection
                    if (opts->verbose)
                    {
                        const char *pattern_names[] = {"exact", "prefix", "suffix", "random"};
                        printf("  Creating similar file (%s pattern, %.1f%% similarity): %s -> %s\n", 
                               pattern_names[pattern], opts->similarity * 100.0, selected_ref->path, full_path);
                    }

                    // Write the similar content to the new file
                    fwrite(similar_content, 1, final_size, f);
                    free(similar_content);
                    duplicates_created++;

                    if (opts->verbose)
                    {
                        printf("  Created similar file: %s (%zu bytes)\n",
                               full_path, final_size);
                    }
                } else {
                    fprintf(stderr, "Error: Failed to generate similar content\n");
                }
            }
            else
            {
                fprintf(stderr, "Error: Failed to select a valid reference file for duplication\n");
            }
        }
        else
        {
            // Create a completely new file with random content
            size_t content_size;
            if (opts->size_buckets > 0) {
                content_size = generate_bucketed_size(
                    (size_t)(opts->size_p50 * opts->size_scale),
                    (size_t)(opts->size_p95 * opts->size_scale),
                    (size_t)(opts->size_p100 * opts->size_scale),
                    opts->size_buckets);
            } else {
                content_size = generate_file_size(
                    (size_t)(opts->size_p50 * opts->size_scale),
                    (size_t)(opts->size_p95 * opts->size_scale),
                    (size_t)(opts->size_p100 * opts->size_scale));
            }
            char *content = generate_random_content(content_size);

            fwrite(content, 1, content_size, f);
            free(content);
        }

        fclose(f);

        if ((i + 1) % 10 == 0)
        {
            if (opts->verbose == 0) {
                print_status_update("Created %d/%d source files (%d duplicates so far)",
                                  i + 1, num_files, duplicates_created);
            } else if (opts->verbose) {
                printf("  Created %d/%d source files (%d duplicates so far)\n",
                       i + 1, num_files, duplicates_created);
            }
        }

        free(dir);
        free(filename);
    }

    /* Clear status line if we were showing progress updates */
    if (opts->verbose == 0) {
        clear_status_line();
    }

    return duplicates_created;
}

int generate_test_data(const options_t *opts)
{
    file_entry_t *ref_files = NULL;

    /* Use the seed from options */
    rand_seed = opts->seed;
    srand(rand_seed);

    printf("Generating test data (seed: %u)\n", rand_seed);

    if (create_reference_directory(opts->ref_root, opts->num_files, opts->num_dirs,
                                   &ref_files, opts) != 0)
    {
        return -1;
    }

    int duplicates_created = create_source_directory(opts->src_root, opts->num_files, opts->num_dirs,
                                                    ref_files, opts);
    if (duplicates_created < 0)
    {
        free_file_list(ref_files);
        return -1;
    }

    free_file_list(ref_files);

    /* Print final summary */
    printf("\nSyndir Summary:\n");
    printf("  Reference files:  %d\n", opts->num_files);
    printf("  Source files:     %d\n", opts->num_files);
    printf("  Duplicates:       %d (%.1f%%)\n", duplicates_created,
           (float)duplicates_created / opts->num_files * 100);
    printf("  Unique files:     %d\n", opts->num_files - duplicates_created);
    printf("  Directories:      %d per tree\n", opts->num_dirs);
    
    if (opts->size_buckets > 0) {
        printf("  Size buckets:     %d (p50=%zu, p95=%zu, max=%zu)\n", 
               opts->size_buckets, opts->size_p50, opts->size_p95, opts->size_p100);
    }
    
    printf("  Similarity:       %.0f%% (exact=%d%%, prefix=%d%%, suffix=%d%%)\n",
           opts->similarity * 100, opts->exact_percent, opts->prefix_percent, opts->suffix_percent);
    printf("  Seed:             %u\n", opts->seed);

    return 0;
}

void free_file_list(file_entry_t *list)
{
    file_entry_t *current = list;
    file_entry_t *next;

    while (current)
    {
        next = current->next;
        free(current->path);
        free(current->content);
        free(current);
        current = next;
    }
}