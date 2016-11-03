/* esh - embedded shell
 *
 * Copyright (c) 2016, Chris Pavlina.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 */

// History allocation types
#define STATIC 1
#define MANUAL 2
#define MALLOC 3

#include <esh.h>
#define ESH_INTERNAL_INCLUDE
#include <esh_internal.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESH_HIST_ALLOC
// Begin actual history implementation

/**
 * Initialize the history buffer.
 *
 * The initial value is a NUL byte followed by a fill of 0xff. This avoids
 * the extra empty-string history entry that would be seen if the buffer
 * were filled with 0x00.
 */
static void init_buffer(char * buffer)
{
    memset(buffer, 0xff, ESH_HIST_LEN);
    buffer[0] = 0;
}

/**
 * True signed modulo, for wraparound signed arithmetic. This is mathematically
 * equivalent to "0 + a   mod b".
 */
static int modulo(int n, int modulus)
{
    int rem = n % modulus;
    return (rem >= 0) ? rem : rem + modulus;
}

/**
 * Given an offset in the ring buffer, call the callback once for each
 * character in the string starting there. This is meant to abstract away
 * ring buffer access.
 *
 * @param esh - esh instance
 * @param offset - offset into the ring buffer
 * @param callback - will be called once per character
 *      - callback:param esh - esh instance
 *      - callback:param c - character
 *      - callback:return - true to stop iterating, false to continue
 *
 * Regardless of the callback's return value, iteration will always stop at NUL
 * or if the loop wraps all the way around.
 */
static void for_each_char(esh_t * esh, int offset,
        bool (*callback)(esh_t * esh, char c))
{
    for (int i = offset; esh->hist.hist[i]; i = (i + 1) % ESH_HIST_LEN) {
        if (i == modulo(offset - 1, ESH_HIST_LEN)) {
            // Wrapped around and didn't encounter NUL. Stop here to prevent
            // an infinite loop.
            return;
        }

        if (callback(esh, esh->hist.hist[i])) {
            return;
        }
    }
}


/**
 * Internal callback passed to for_each_char by clobber_buffer
 */
static bool clobber_cb(esh_t * esh, char c)
{
    esh->buffer[esh->cnt] = c;
    ++esh->cnt;
    ++esh->ins;
    return false;
}


/**
 * Put the selected history item in the buffer. Make sure to call
 * esh_restore afterward to display the buffer.
 * @param esh - esh instance
 * @param offset - offset into the ring buffer
 */
static void clobber_buffer(esh_t * esh, int offset)
{
    if (offset < 0 || offset >= ESH_HIST_LEN) {
        return;
    }

    esh->cnt = 0;
    esh->ins = 0;
    for_each_char(esh, offset, &clobber_cb);
}


bool esh_hist_init(esh_t * esh)
{
#if ESH_HIST_ALLOC == STATIC
    static char esh_hist[ESH_HIST_LEN] = {0};
    esh->hist.hist = &esh_hist[0];
    init_buffer(esh->hist.hist);
    return false;
#elif ESH_HIST_ALLOC == MALLOC
    esh->hist.hist = malloc(ESH_HIST_LEN);
    if (esh->hist.hist) {
        init_buffer(esh->hist.hist);
        return false;
    } else {
        return true;
    }
#elif ESH_HIST_ALLOC == MANUAL
    esh->hist.hist = NULL;
    return false;
#endif
}


int esh_hist_nth(esh_t * esh, int n)
{
    const int start = modulo(esh->hist.tail - 1, ESH_HIST_LEN);
    const int stop = (esh->hist.tail + 1) % ESH_HIST_LEN;

    for (int i = start; i != stop; i = modulo(i - 1, ESH_HIST_LEN)) {
        if (n && esh->hist.hist[i] == 0) {
            --n;
        } else if (esh->hist.hist[i] == 0) {
            return (i + 1) % ESH_HIST_LEN;
        }
    }

    return -1;
}


bool esh_hist_add(esh_t * esh, char const * s)
{
    const int start = (esh->hist.tail + 1) % ESH_HIST_LEN;

    for (int i = start; ; i = (i + 1) % ESH_HIST_LEN)
    {
        if (i == modulo(esh->hist.tail - 1, ESH_HIST_LEN)) {
            // Wrapped around
            esh->hist.tail = 0;
            init_buffer(esh->hist.hist);
            return true;
        }

        esh->hist.hist[i] = *s;

        if (*s) {
            ++s;
        } else {
            esh->hist.tail = i;
            return false;
        }
    }
}


void esh_hist_print(esh_t * esh, int offset)
{
    // Clear the line
    esh_puts(esh, FSTR(ESC_ERASE_LINE "\r"));

    esh_print_prompt(esh);

    if (offset >= 0) {
        for_each_char(esh, offset, esh_putc);
    }
}


bool esh_hist_substitute(esh_t * esh)
{
    if (esh->hist.idx) {
        int offset = esh_hist_nth(esh, esh->hist.idx - 1);
        clobber_buffer(esh, offset);
        esh_restore(esh);
        esh->hist.idx = 0;
        return true;
    } else {
        return false;
    }
}

#endif // ESH_HIST_ALLOC

#if defined(ESH_HIST_ALLOC) && ESH_HIST_ALLOC == MANUAL

void esh_set_histbuf(esh_t * esh, char * buffer)
{
    esh->hist.hist = buffer;
    init_buffer(esh->hist.hist);
}

#else // ESH_HIST_ALLOC == MANUAL

void esh_set_histbuf(esh_t * esh, char * buffer)
{
    (void) esh;
    (void) buffer;
}

#endif // ESH_HIST_ALLOC == MANUAL
