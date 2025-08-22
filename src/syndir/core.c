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

        size_t content_size = generate_file_size(
            (size_t)(opts->size_p50 * opts->size_scale),
            (size_t)(opts->size_p95 * opts->size_scale),
            (size_t)(opts->size_p100 * opts->size_scale));
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
                printf("  Created %d/%d reference files\n", i + 1, num_files);
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
                // Debugging output to verify reference file selection
                if (opts->verbose)
                {
                    printf("  Duplicating file: %s -> %s\n", selected_ref->path, full_path);
                }

                // Write the content of the reference file to the new file
                fwrite(selected_ref->content, 1, selected_ref->content_size, f);
                duplicates_created++;

                if (opts->verbose)
                {
                    printf("  Created duplicate: %s (%zu bytes)\n",
                           full_path, selected_ref->content_size);
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
            size_t content_size = generate_file_size(
                (size_t)(opts->size_p50 * opts->size_scale),
                (size_t)(opts->size_p95 * opts->size_scale),
                (size_t)(opts->size_p100 * opts->size_scale));
            char *content = generate_random_content(content_size);

            fwrite(content, 1, content_size, f);
            free(content);
        }

        fclose(f);

        if (opts->verbose && (i + 1) % 10 == 0)
        {
            printf("  Created %d/%d source files (%d duplicates so far)\n",
                   i + 1, num_files, duplicates_created);
        }

        free(dir);
        free(filename);
    }

    if (opts->verbose)
    {
        printf("Completed: %d duplicates out of %d files (%.1f%%)\n",
               duplicates_created, num_files,
               (float)duplicates_created / num_files * 100);
    }

    return 0;
}

int generate_test_data(const options_t *opts)
{
    file_entry_t *ref_files = NULL;

    if (rand_seed == 0)
    {
        rand_seed = time(NULL);
        srand(rand_seed);
    }

    if (opts->verbose)
    {
        printf("Test data generation started (seed: %u)\n", rand_seed);
    }

    if (create_reference_directory(opts->ref_root, opts->num_files, opts->num_dirs,
                                   &ref_files, opts) != 0)
    {
        return -1;
    }

    if (create_source_directory(opts->src_root, opts->num_files, opts->num_dirs,
                                ref_files, opts) != 0)
    {
        free_file_list(ref_files);
        return -1;
    }

    free_file_list(ref_files);

    if (opts->verbose)
    {
        printf("Test data generation completed successfully\n");
    }

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