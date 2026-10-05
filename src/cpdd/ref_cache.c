/*
 * cpdd/src/cpdd/ref_cache.c - Persist a scanned reference index to disk
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

/*
 * Binary format, native byte order and struct-free (fixed-width fields only,
 * length-prefixed strings, no padding) -- this is a local performance cache
 * read back by the same machine that wrote it, not an interchange format,
 * so there is no need for the portability care the (removed) text catalogue
 * format took. A corrupted, truncated or foreign file must never crash the
 * reader or be silently misread as valid data: every length read is bounds
 * checked before use, and any short read or out-of-range value aborts the
 * load and frees everything parsed so far, returning NULL. The cache is
 * fully regenerable from the reference directories at any time, so treating
 * "unreadable" the same as "absent" (a fresh scan) is always safe. By
 * default the caller never trusts a cached match without also doing its own
 * bytewise comparison (see files_match() in matching.c). --no-verify is the
 * exception: it trusts the cached size, name and mtime as they were at scan
 * time, which is why --cache-ttl matters when the reference can change.
 *
 *   u32 magic, u32 version, u32 dir_count
 *   per directory:
 *     u32 path_len, bytes[path_len]      (realpath(), no NUL)
 *     i64 scanned_at
 *     u32 file_count
 *     per file:
 *       u32 rel_len, bytes[rel_len]      (path relative to the directory, no NUL)
 *       i64 size
 *       i64 mtime                        (whole seconds)
 *       i32 cached_blocks
 *       bytes[cached_blocks * BLOCK_HASH_SIZE]
 */

#include "cpdd.h"

#include <stdint.h>
#include <time.h>

#define REF_CACHE_MAGIC    0x43524331u   /* arbitrary sentinel, not "meaningful" text */
#define REF_CACHE_VERSION  2u
#define REF_CACHE_MAX_LEN  (1u << 20)     /* max length for any string field */
#define REF_CACHE_MAX_COUNT (1u << 24)    /* max count for any array field */

#define REF_CACHE_CHECK_SAMPLE_EVERY     256UL  /* only call time() this often */
#define REF_CACHE_CHECKPOINT_INTERVAL_S  30    /* rewrite the cache at most this often */

ref_files_t *ref_cache_read(const char *path)
{
    FILE *fp = fopen(path, "rb");
    uint32_t magic = 0, version = 0, dir_count = 0;
    ref_files_t *rf;
    file_info_t *head = NULL;
    int total_files = 0;
    int ok = 1;

    if (!fp) return NULL;

    if (fread(&magic, sizeof magic, 1, fp) != 1 || magic != REF_CACHE_MAGIC ||
        fread(&version, sizeof version, 1, fp) != 1 || version != REF_CACHE_VERSION ||
        fread(&dir_count, sizeof dir_count, 1, fp) != 1 || dir_count > REF_CACHE_MAX_COUNT) {
        fclose(fp);
        return NULL;
    }

    rf = calloc(1, sizeof(*rf));
    if (!rf) { fclose(fp); return NULL; }
    rf->ref_dir_paths = calloc(dir_count ? dir_count : 1, sizeof(char *));
    rf->ref_dir_scanned_at = calloc(dir_count ? dir_count : 1, sizeof(time_t));
    if (!rf->ref_dir_paths || !rf->ref_dir_scanned_at) ok = 0;

    for (uint32_t d = 0; ok && d < dir_count; d++) {
        uint32_t path_len = 0, file_count = 0;
        int64_t scanned_at = 0;
        char *dirpath;

        if (fread(&path_len, sizeof path_len, 1, fp) != 1 ||
            path_len == 0 || path_len > REF_CACHE_MAX_LEN) { ok = 0; break; }

        dirpath = malloc((size_t)path_len + 1);
        if (!dirpath) { ok = 0; break; }
        if (fread(dirpath, 1, path_len, fp) != path_len) { free(dirpath); ok = 0; break; }
        dirpath[path_len] = '\0';

        if (fread(&scanned_at, sizeof scanned_at, 1, fp) != 1 ||
            fread(&file_count, sizeof file_count, 1, fp) != 1 ||
            file_count > REF_CACHE_MAX_COUNT) {
            free(dirpath);
            ok = 0;
            break;
        }

        rf->ref_dir_paths[d] = dirpath;
        rf->ref_dir_scanned_at[d] = (time_t)scanned_at;
        rf->ref_dir_count = (int)d + 1;   /* set as we go, so failure cleanup is accurate */

        for (uint32_t f = 0; ok && f < file_count; f++) {
            uint32_t rel_len = 0;
            int64_t size = 0, mtime = 0;
            int32_t cached_blocks = 0;
            char *relpath;
            file_info_t *node;
            size_t need;

            if (fread(&rel_len, sizeof rel_len, 1, fp) != 1 ||
                rel_len == 0 || rel_len > REF_CACHE_MAX_LEN) { ok = 0; break; }

            relpath = malloc((size_t)rel_len + 1);
            if (!relpath) { ok = 0; break; }
            if (fread(relpath, 1, rel_len, fp) != rel_len) { free(relpath); ok = 0; break; }
            relpath[rel_len] = '\0';

            if (fread(&size, sizeof size, 1, fp) != 1 ||
                fread(&mtime, sizeof mtime, 1, fp) != 1 ||
                fread(&cached_blocks, sizeof cached_blocks, 1, fp) != 1 ||
                cached_blocks < 0 || cached_blocks > MAX_CACHED_BLOCKS) {
                free(relpath);
                ok = 0;
                break;
            }

            node = calloc(1, sizeof(*node));
            if (!node) { free(relpath); ok = 0; break; }

            need = strlen(dirpath) + 1 + rel_len + 1;
            node->path = malloc(need);
            if (!node->path) { free(node); free(relpath); ok = 0; break; }
            snprintf(node->path, need, "%s/%s", dirpath, relpath);
            free(relpath);

            node->basename = strrchr(node->path, '/');
            node->basename = node->basename ? node->basename + 1 : node->path;
            node->size = (off_t)size;
            node->mtime = (time_t)mtime;
            node->ref_dir_index = (int)d;
            init_block_hashes(&node->block_hashes);

            if (cached_blocks > 0) {
                node->block_hashes.block_md5s = malloc((size_t)cached_blocks * BLOCK_HASH_SIZE);
                if (!node->block_hashes.block_md5s ||
                    fread(node->block_hashes.block_md5s, BLOCK_HASH_SIZE,
                          (size_t)cached_blocks, fp) != (size_t)cached_blocks) {
                    free(node->block_hashes.block_md5s);
                    free(node->path);
                    free(node);
                    ok = 0;
                    break;
                }
                node->block_hashes.cached_blocks = cached_blocks;
                node->block_hashes.allocated_blocks = cached_blocks;
            }

            node->next = head;
            head = node;
            total_files++;
        }
    }

    fclose(fp);

    if (!ok) {
        free_file_list(head);
        for (int i = 0; i < rf->ref_dir_count; i++) free(rf->ref_dir_paths[i]);
        free(rf->ref_dir_paths);
        free(rf->ref_dir_scanned_at);
        free(rf);
        return NULL;
    }

    /* Flatten into the array form ref_files_t normally holds. Left unsorted
     * deliberately: the caller merges this with a fresh scan of any
     * directory that wasn't reusable and sorts once at the end, exactly as
     * an uncached scan does. */
    rf->files = malloc(sizeof(file_info_t *) * (size_t)(total_files ? total_files : 1));
    if (!rf->files) {
        free_file_list(head);
        for (int i = 0; i < rf->ref_dir_count; i++) free(rf->ref_dir_paths[i]);
        free(rf->ref_dir_paths);
        free(rf->ref_dir_scanned_at);
        free(rf);
        return NULL;
    }
    rf->count = total_files;
    rf->capacity = total_files;
    {
        file_info_t *cur = head;
        for (int i = 0; i < total_files && cur; i++) {
            file_info_t *nxt = cur->next;
            cur->next = NULL;
            rf->files[i] = cur;
            cur = nxt;
        }
    }
    return rf;
}

int ref_cache_write(const char *path, const ref_files_t *ref_files)
{
    FILE *fp;
    uint32_t magic = REF_CACHE_MAGIC, version = REF_CACHE_VERSION;
    uint32_t dir_count;
    int ok = 1;

    /* Guarded here too, defense in depth: ref_files is legitimately NULL
     * whenever the reference scan found nothing (an empty or nonexistent
     * reference directory), a normal case elsewhere in this codebase, not
     * an error -- whatever calls this must never crash on it. There is
     * nothing to persist in that case; leave any existing cache alone
     * rather than overwrite it with an empty one. */
    if (!ref_files) return 0;

    fp = fopen(path, "wb");
    if (!fp) {
        print_warning("Cannot write reference cache %s: %s", path, strerror(errno));
        return -1;
    }
    dir_count = (uint32_t)ref_files->ref_dir_count;

    if (fwrite(&magic, sizeof magic, 1, fp) != 1 ||
        fwrite(&version, sizeof version, 1, fp) != 1 ||
        fwrite(&dir_count, sizeof dir_count, 1, fp) != 1) {
        ok = 0;
    }

    for (int d = 0; ok && d < ref_files->ref_dir_count; d++) {
        const char *dp = ref_files->ref_dir_paths[d];
        size_t dl = strlen(dp);
        uint32_t path_len = (uint32_t)dl;
        int64_t scanned_at = (int64_t)ref_files->ref_dir_scanned_at[d];
        uint32_t file_count = 0;

        for (int i = 0; i < ref_files->count; i++) {
            if (ref_files->files[i]->ref_dir_index == d) file_count++;
        }

        if (fwrite(&path_len, sizeof path_len, 1, fp) != 1 ||
            fwrite(dp, 1, dl, fp) != dl ||
            fwrite(&scanned_at, sizeof scanned_at, 1, fp) != 1 ||
            fwrite(&file_count, sizeof file_count, 1, fp) != 1) {
            ok = 0;
            break;
        }

        for (int i = 0; ok && i < ref_files->count; i++) {
            file_info_t *f = ref_files->files[i];
            const char *rel;
            uint32_t rel_len;
            int64_t size, mtime;
            int32_t cached_blocks;

            if (f->ref_dir_index != d) continue;

            /* f->path is "<dir>/<rel>"; recover rel by stripping the
             * directory prefix. Falls back to the basename in the
             * (shouldn't-happen) case a path doesn't actually start with
             * its own recorded directory, rather than write a record that
             * wouldn't reconstruct to the right path on read. */
            if (strncmp(f->path, dp, dl) == 0 && f->path[dl] == '/') {
                rel = f->path + dl + 1;
            } else {
                rel = f->basename;
            }
            rel_len = (uint32_t)strlen(rel);
            size = (int64_t)f->size;
            mtime = (int64_t)f->mtime;
            cached_blocks = f->block_hashes.cached_blocks;

            if (fwrite(&rel_len, sizeof rel_len, 1, fp) != 1 ||
                fwrite(rel, 1, rel_len, fp) != rel_len ||
                fwrite(&size, sizeof size, 1, fp) != 1 ||
                fwrite(&mtime, sizeof mtime, 1, fp) != 1 ||
                fwrite(&cached_blocks, sizeof cached_blocks, 1, fp) != 1) {
                ok = 0;
                break;
            }
            if (cached_blocks > 0 &&
                fwrite(f->block_hashes.block_md5s, BLOCK_HASH_SIZE,
                       (size_t)cached_blocks, fp) != (size_t)cached_blocks) {
                ok = 0;
                break;
            }
        }
    }

    if (fflush(fp) != 0) ok = 0;
    if (fclose(fp) != 0) ok = 0;

    if (!ok) {
        print_warning("Failed writing reference cache %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

void ref_cache_checkpoint(const ref_files_t *ref_files, const options_t *opts)
{
    static unsigned long files_since_check = 0;
    static time_t last_checkpoint = 0;
    time_t now;

    if (!opts->cache_file || !ref_files) return;

    if (++files_since_check < REF_CACHE_CHECK_SAMPLE_EVERY) return;
    files_since_check = 0;

    now = time(NULL);
    if (last_checkpoint != 0 && now - last_checkpoint < REF_CACHE_CHECKPOINT_INTERVAL_S) return;
    last_checkpoint = now;

    ref_cache_write(opts->cache_file, ref_files);
}
