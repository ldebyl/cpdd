/*
 * cpdd/include/md5.h - MD5 message digest implementation
 * 
 * This is the RSA Data Security, Inc. MD5 Message-Digest Algorithm
 * Reference implementation from RFC 1321, placed in the public domain.
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

#ifndef MD5_H
#define MD5_H

#include <stdint.h>
#include <stddef.h>

#define MD5_DIGEST_LENGTH 16

/* Pre-computed MD5 of empty file */
extern const unsigned char NULL_MD5[MD5_DIGEST_LENGTH];

/* MD5 context structure */
typedef struct {
    uint32_t state[4];        /* MD5 state (A, B, C, D) */
    uint32_t count[2];        /* Number of bits processed */
    unsigned char buffer[64]; /* Input buffer */
} MD5_CTX;

/* MD5 computation functions */
void MD5_Init(MD5_CTX *ctx);
void MD5_Update(MD5_CTX *ctx, const void *data, size_t len);
void MD5_Final(unsigned char digest[MD5_DIGEST_LENGTH], MD5_CTX *ctx);
#endif
