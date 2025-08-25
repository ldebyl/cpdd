 /*
   * cpdd/main.c - Content-based copy with deduplication main program
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

int main(int argc, char *argv[]) {
    options_t opts;
    stats_t stats = {0};
    int parse_result;
    
    parse_result = parse_args(argc, argv, &opts);
    if (parse_result != 0) {
        if (parse_result > 0) {
            return 0;
        }
        return 1;
    }
    
    if (copy_directory(&opts, &stats) != 0) {
        if (opts.show_stats && opts.verbose == 0) {
            clear_status_line();
        }
        fprintf(stderr, "Error: Copy operation failed\n");
        return 1;
    }
    
    if (opts.show_stats && opts.verbose == 0) {
        clear_status_line();
    }
    
    if (opts.show_stats) {
        print_statistics(&stats, opts.human_readable);
    }
    
    return 0;
}
