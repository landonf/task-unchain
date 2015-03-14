/*
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <err.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>

#define PATCH_POS 0
static const uint8_t target_pattern[] = {
    0x44, 0x88, 0xF2, 0x80, 0xE2, 0x01, 0x88, 0x95, 0xE0, 0xFE, 0xFF, 0xFF, 0x8A, 0x8D, 0xF3, 0xFE,
    0xFF, 0xFF, 0x80, 0xE1, 0x01, 0x88, 0x8D, 0xF3, 0xFE, 0xFF, 0xFF, 0x8A, 0x9D, 0xF4, 0xFE, 0xFF,
    0xFF, 0x88, 0xD8, 0x24, 0x01, 0x0F, 0xB6, 0xC9, 0x89, 0x0C, 0x24, 0x0F, 0xB6, 0xCA, 0x44, 0x0F,
    0xB6, 0xC0, 0x44, 0x88, 0xE8, 0x24, 0x01, 0x44, 0x0F, 0xB6, 0xC8, 0x48, 0x8D, 0x3D, 0xD0, 0x6F,
    0x00, 0x00, 0x48, 0x8D, 0x35, 0x7A, 0x70, 0x00, 0x00, 0x44, 0x8B, 0xBD, 0xD4, 0xFE, 0xFF, 0xFF,
    0x44, 0x89, 0xFA, 0x30, 0xC0, 0xE8, 0xAA, 0x4F, 0x00, 0x00, 0x45, 0x08, 0xF5, 0x41, 0xF6, 0xC5,
    0x01, 0x74, 0x11, 0x44, 0x89, 0xFF, 0xBE, 0x03, 0x00, 0x00, 0x00, 0x31, 0xD2, 0x31, 0xC9, 0xE8,
    0xCD, 0x52, 0x00, 0x00, 0x44, 0x08, 0xF3, 0xF6, 0xC3, 0x01, 0x0F, 0x84, 0xBD, 0x01,
                                        /* patch s/jz/jmp/ here ^^^^^^^^^^^^^^^^^^^^^^ */
};
static const uint8_t target_patch[] = { 0xE9, 0xBE, 0x01, 0x00 };
static const size_t target_patch_pos = sizeof(target_pattern) - sizeof(target_patch);

/**
 * BMH search pattern.
 *
 * A Pratt-Boyer-Moore string search, written by Jerry Coffin
 * sometime or other in 1991.  Removed from original program, and
 * (incorrectly) rewritten for separate, generic use in early 1992.
 * Corrected with help from Thad Smith, late March and early
 * April 1992...hopefully it's correct this time. Revised by Bob Stout.
 *
 * This is hereby placed in the Public Domain by its author.
 *
 * 10/21/93   rdg     Fixed bug found by Jeff Dunlop
 * 12/16/2013 landonf Removed globals, updated to use C99 types.
 * 03/14/2015 landonf Convert to a C++ class for use in task-unchain.
 */
class bmh_pattern {
public:
    /**
     * Initialize a pattern.
     *
     * @param string The binary string to search for.
     * @param len The length of @a string.
     */
    bmh_pattern (const uint8_t *string, size_t len) : _findme(string), _len(len) {
        size_t i;
        
        for (i = 0; i <= UINT8_MAX; i++)                      /* rdg 10/93 */
            _table[i] = len;
        for (i = 0; i < len; i++)
            _table[(unsigned char)string[i]] = len - i - 1;
    }
    
    /**
     * Search for this pattern in @a string.
     *
     * @param string The buffer to be searched.
     * @param limit The size of @a string in bytes.
     */
    const uint8_t *search (const uint8_t *string, size_t limit) {
        size_t shift = 0;
        size_t pos = _len - 1;
        const uint8_t *here;
        
        while (pos < limit) {
            while (pos < limit && (shift = _table[(unsigned char)string[pos]]) > 0) {
                pos += shift;
            }
            
            if (0 == shift) {
                here = &string[pos - _len+1];
                if (0 == memcmp(_findme, here, _len)) {
                    return (here);
                }
                else pos++;
            }
        }
        return NULL;
    }
    
private:
    size_t _table[UINT8_MAX + 1];
    size_t _len;
    const uint8_t *_findme;
};

int main(int argc, const char *argv[]) {
    int fd;
    struct stat statbuf;
    
    /* Parse the arguments */
    if (argc != 2)
        errx(EXIT_FAILURE, "Usage: %s <path to taskgated>", argv[0]);
    const char * const fname = argv[1];
    
    /* open the target file */
    fd = open(fname, O_RDWR);
    if (fd < 0)
        err(EXIT_FAILURE, "open(%s)", fname);
    
    /* compute the file size */
    if (fstat(fd, &statbuf) != 0)
        err(EXIT_FAILURE, "fstat(%s)", fname);
    
    if (statbuf.st_size > SIZE_MAX)
        errx(EXIT_FAILURE, "File is too large to be mapped on this system.");
    size_t len = statbuf.st_size;
    
    /* mmap the file */
    uint8_t * const start = (uint8_t * const) mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (start == MAP_FAILED)
        err(EXIT_FAILURE, "mmap(%s)", fname);
    
    const uint8_t * const end = start + len;
    
    /* search for our patch pattern */
    bmh_pattern bmh(target_pattern, sizeof(target_pattern));
    
    uint8_t *pos = start;
    while (pos + sizeof(target_pattern) < end) {
        uint8_t *result = (uint8_t *) bmh.search(pos, end - pos);
        if (result == NULL)
            break;
        
        printf("Found at file offset %lx [%hhu], patching ...\n", (result - start), result[target_patch_pos]);
        pos = result + sizeof(target_pattern);

        /* Apply out patch */
        memcpy(result + target_patch_pos, target_patch, sizeof(target_patch));
        
        /* We shouldn't need to patch more than one instance */
        break;
    }
    
    if (munmap(start, len) != 0)
        warn("munmap()");
    
    if (fsync(fd) != 0)
        warn("fsync()");
    
    if (close(fd) != 0)
        err(EXIT_FAILURE, "close(%s)", fname);
    
    
    return EXIT_SUCCESS;
}