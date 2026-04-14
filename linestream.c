#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "linestream.h"
#ifndef BUFLEN
    #define BUFLEN 256
#endif
#ifndef DEBUG
    #define DEBUG 0
#endif

// impementation of linestream - useful for parsing lines in a text file

// initialize lines based on passed in lines_t pointer and int for file descriptor
void lines_init (lines_t *l, int fd){
    l->fd = fd;
    l->cap = BUFLEN;
    l->buf = malloc (BUFLEN);
    l->len = 0;
    l->pos = 0;
}

// free buffer based on passed in lines_t pointer
void lines_destroy (lines_t *l){
    free (l->buf);
}

// return string by processing lines_t pointer
char * lines_next (lines_t *l) {

    if (DEBUG) printf ("lines_next pos %d; len %d; cap %d\n", l->pos, l->len, l->cap);
    
    // if nothing in the line, return NULL
    if (l->len < 0) return NULL;
    
    int start = l->pos;
    
    while (1) {

        // update buffer
        for (; l->pos < l->len; l->pos++) {
            if (l->buf[l->pos] == '\n') {
                l->buf[l->pos] = '\0';
                l->pos++;
                return l->buf + start;
            }
        }

        if (DEBUG) printf (" eob %d/%d/%d/%d\n", start, l->pos, l->len, l->cap);
        
        // reached end of buffer
        int seglen = l->pos - start;

        // if there is no line, start fresh
        if (seglen == 0) {
            l->pos = 0;
            start = 0;
        // if there is space, shift line in progress to start
        } else if (start > 0 && seglen > 0) {
            memmove (l->buf, l->buf + start, seglen);
            l->len = seglen;
            l->pos = seglen;
            start = 0;
        // if there is not enough space, expand the buffer
        } else if (seglen > 0 && l->pos == l->cap) {
            l->cap *= 2;
            l->buf = realloc (l->buf, l->cap);
        }
        
        // refill buffer
        int bytes = read (l->fd, l->buf + l->pos, l->cap - l->pos);
        
        if (DEBUG) printf (" read -> %d\n", bytes);
        
        // handle edge case of no bytes
        if (bytes < 1) {
            l->buf[l->pos] = '\0';
            l->len = -1;
            return seglen > 0 ? l->buf + start : NULL;
        }
        
        l->len = l->pos + bytes;
        
    }

    return NULL;
}
