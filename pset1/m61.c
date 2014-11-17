#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// the struct memstat keeps record of all memory statistics
typedef struct memstat {
// the following 6 variables will keep record of counters and size statistics
size_t allocated;
size_t sizeallocated;
size_t freed;
size_t sizefreed;
size_t failed;
size_t failedsize;
// sztbl stores all necessary informations on allocated memory
// [0][1] stores current length, have to increase the number of rows 
// allocated for heavier uses
// [0] is the pointer's address, [1] is actual (data) size, [2] is aligned size
// if [1] < [2], [3] is the data in the next byte after the physical size
// else [3] is the data in the next word after the aligned size
// [4] is the number in the file name, [5] is line number
int sztbl[500][6];
// fltbl stores size and frequency with respect of each file and line pair
// each sheet stores allocation info for each file,
// fist row stores number of files in [0], current file name pointer in [1],
// and number of lines in [2], each of the following row stores info for each
// memory allocating line of code in this file, [1] frequency, [2] total size
size_t fltbl[5][100][3];
} memstat;

// initialize with 0 statistics
memstat stat = {.allocated = 0,
				.sizeallocated = 0,
				.freed = 0,
				.sizefreed = 0,
				.failed = 0,
				.failedsize = 0,
				.sztbl = {{0, 1, 0, 0}}, // skip the 0th row for metadata
				.fltbl = {{{1, 0, 0}}} // there is at least one file
};



void *m61_malloc(size_t sz, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    void * retptr = malloc(sz);
    // record as fail if returned pointer is null
    if (retptr == NULL) {
        stat.failed++;
        stat.failedsize += sz;
        return retptr;
    } else {
    // on success, record as allocated
        stat.allocated++;
        stat.sizeallocated += sz;
        // record allocation info of this line of code
        for (uint j = 0; j < stat.fltbl[0][0][0]; j++) {
            if (stat.fltbl[0][0][1] == 0) // at start, all pages are empty
                stat.fltbl[0][0][1] = (int) file; // page has file name in row 0
            if ((uint)file == stat.fltbl[j][0][1]) { // same page for same file
                if (stat.fltbl[j][0][2] == 0) { // empty, add first ln metadata
                    stat.fltbl[j][1][0] = line;
                    stat.fltbl[j][0][2] += 1;
                }
                for (uint k = 1; k <= stat.fltbl[j][0][2]; k++) {
                    if ((uint)line == stat.fltbl[j][k][0]) { // if line alloc'd
                        stat.fltbl[j][k][1] += 1;
                        stat.fltbl[j][k][2] += sz;
                        break;
                    } else if (k == stat.fltbl[j][0][2]) { // new row for line
                        stat.fltbl[j][0][2] += 1;
                        stat.fltbl[j][k+1][0] = line;
                    }
                }
                break;
            } else if (j == stat.fltbl[0][0][0]-1) { // use a new page for file
                stat.fltbl[0][0][0] += 1;
                stat.fltbl[j+1][0][1] = (int)file;
            }
        }
        for (int i = 1; i <= stat.sztbl[0][1]; i++) {
            // updates "next block" data, in case of later allocation overwrite
            // don't have to consider byte overwrite since allocation is aligned
            if (stat.sztbl[i][0] != 0 && stat.sztbl[i][1] == stat.sztbl[i][2])
            	stat.sztbl[i][3] = *(int *)((char *)(stat.sztbl[i][0]) + stat.sztbl[i][2]);
            // if occupies a previously emptied block
            if (stat.sztbl[i][0] == (int)retptr && stat.sztbl[i][1] == 0) {
                stat.sztbl[i][1] = sz; // physical size
                while (sz % 4 != 0)
                    sz++;
                stat.sztbl[i][2] = sz; // aligned size
                if (stat.sztbl[i][1] < stat.sztbl[i][2]) {
                	// if two sizes not equal, record canary byte value
                	stat.sztbl[i][3] = *((char *)retptr + sz - 4 + (stat.sztbl[i][2] - stat.sztbl[i][1]) - 1) = (char) rand();
                } else {
                	// record word value after allocated block
                	stat.sztbl[i][3] = *(int *)((char *)retptr + sz);
                }
                stat.sztbl[i][4] = (int)file;
                stat.sztbl[i][5] = line;
                return retptr;
            }
        }
        // put new info of allocation to the end of the table, similar to above
        stat.sztbl[(stat.sztbl[0][1] + 1)][0] = (int)retptr;
        stat.sztbl[(stat.sztbl[0][1] + 1)][1] = sz;
        stat.sztbl[(stat.sztbl[0][1] + 1)][4] = (int)file;
        stat.sztbl[(stat.sztbl[0][1] + 1)][5] = line;
        while (sz % 4 != 0)
            sz++;
        stat.sztbl[(stat.sztbl[0][1] + 1)][2] = sz;
        if (stat.sztbl[(stat.sztbl[0][1] + 1)][1] < stat.sztbl[(stat.sztbl[0][1] + 1)][2])
        	stat.sztbl[(stat.sztbl[0][1] + 1)][3] = *((char *)retptr + sz - 4 + (stat.sztbl[(stat.sztbl[0][1] + 1)][2] - stat.sztbl[(stat.sztbl[0][1] + 1)][1]) - 1) = (char) rand();
        else
        	stat.sztbl[(stat.sztbl[0][1] + 1)][3] = *(int *)((char *)retptr + sz);
        stat.sztbl[0][1] += 1;
        return retptr;
     }
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // out of heap free
    if (0x804b004 > (int)ptr || 0xb000000 < (int)ptr) {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
        return;
    }
    // all other checks
    for (int i = 1; i <= stat.sztbl[0][1]; i++) {
        if (stat.sztbl[i][0] == (int)ptr && stat.sztbl[i][1] != 0) {
        	// check for over-written data after the end of allocated bytes
            if (stat.sztbl[i][1] < stat.sztbl[i][2]) {
                if (stat.sztbl[i][3] != *((char *)stat.sztbl[i][0] + stat.sztbl[i][2] - 4 + (stat.sztbl[i][2] - stat.sztbl[i][1]) - 1)) {
                	printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
                	return;
            	}
            } else {
            	// check for over-written data after the end of an allocated block
                if (stat.sztbl[i][3] != *(int *)((char *)ptr + stat.sztbl[i][2])) {
                	printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
                	return;
            	}
            }
            // free success
            stat.sizefreed += stat.sztbl[i][1];
            // set size as 0
            stat.sztbl[i][1] = 0;
            stat.sztbl[i][2] = 0;
            stat.freed++;
            break;
        }
        // if freeing twice
        if (stat.sztbl[i][0] == (int)ptr && stat.sztbl[i][1] == 0) {
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p\n", file, line, ptr);
            return;
        }
        // an address not in the list
        if (i == stat.sztbl[0][1]) {
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            for (i = 1; i <= stat.sztbl[0][1]; i++) {
                if (stat.sztbl[i][0] < (int)ptr && stat.sztbl[i][0] + stat.sztbl[i][1] > (int)ptr) {
                    printf("  %s:%d: %p is %d bytes inside a %d byte region allocated here\n", file, stat.sztbl[i][5], ptr, (int)ptr-stat.sztbl[i][0], stat.sztbl[i][1]);
                }
            }
            return;
       }
    }
    free(ptr);
}

void *m61_realloc(void *ptr, size_t sz, const char *file, int line) {
    void *new_ptr = NULL;
    if (sz) {
        new_ptr = m61_malloc(sz, file, line);
        if (ptr) {
            // copies data into new block
            for (int i = 0; i < ((*((int *)ptr-1))/16)*16; i++) {
                *((char *)new_ptr+i) = *((char *)ptr+i);
            }
            for (int i = 1; i <= stat.sztbl[0][1]; i++) {
            	// updates data word adjacent to all previously allocated blocks
            	if (stat.sztbl[i][0] != 0 && stat.sztbl[i][1] == stat.sztbl[i][2]) {
            		stat.sztbl[i][3] = *(int *)((char *)(stat.sztbl[i][0]) + stat.sztbl[i][2]);
            	} else if (stat.sztbl[i][0] != 0) {
            		// updates canary byte adjacent to previously allocated bytes
            		stat.sztbl[i][3] = *((char *)stat.sztbl[i][0] + stat.sztbl[i][2] - 4 + (stat.sztbl[i][2] - stat.sztbl[i][1]) - 1) = (char) rand();
            	}
            }
            m61_free(ptr, file, line);
        }
    } else {
        m61_free(ptr, file, line);
    }
    return new_ptr;
}

void *m61_calloc(size_t nmemb, size_t sz, const char *file, int line) {
    void *ptr = NULL;
    // prevents size overflow
    if ((long long)nmemb * (long long)sz < (long long)((size_t) - 1))
        ptr = m61_malloc(nmemb * sz, file, line);
    else
        stat.failed++;
    if (ptr)
        memset(ptr, 0, nmemb * sz);
    return ptr;
}

void m61_getstatistics(struct m61_statistics *stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(struct m61_statistics));
    //writes cumulative variables to struct
    stats->nactive = stat.allocated - stat.freed;
    stats->active_size = stat.sizeallocated - stat.sizefreed;
    stats->ntotal = stat.allocated;
    stats->total_size = stat.sizeallocated;
    stats->nfail = stat.failed;
    stats->fail_size = stat.failedsize;
}

void m61_printstatistics(void) {
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

void m61_printleakreport(void) {
    for (int i = 1; i <= stat.sztbl[0][1]; i++)
        if (stat.sztbl[i][1] != 0)
            printf("LEAK CHECK: %s:%d: allocated object %p with size %i\n", (char *)stat.sztbl[i][4], stat.sztbl[i][5], (char *)stat.sztbl[i][0], stat.sztbl[i][1]);
}

// a compare function for qsort
int comparesizet (const void *a, const void *b) { 
       size_t *lla = (size_t *) a;
       size_t *llb = (size_t *) b;
       return (*lla < *llb) - (*lla > *llb);
} 

void m61_printhhreport(void) {
    for (uint j = 0; j < stat.fltbl[0][0][0]; j++) {
        // the following two arrays are for sorting with respect to freq and size
        size_t freqarray[100] = {0};
        size_t sizearray[100] = {0};
        uint k;
        // copies freq data and size data to the two separate arrays
        for (k = 1; k <= stat.fltbl[j][0][2]; k++) {
            freqarray[k-1] = stat.fltbl[j][k][1];
            sizearray[k-1] = stat.fltbl[j][k][2];
        }
        // sorts the two arrays
        qsort(freqarray, k, sizeof(size_t), comparesizet);
        qsort(sizearray, k, sizeof(size_t), comparesizet);
        // print the top several heavy-hitter by size that occupy >12% total size
        for (int i = 0; i < 8; i++)
            if ((double)sizearray[i]/(double)stat.sizeallocated > 0.12)
                for (uint m = 1; m <= k; m++)
                    if (stat.fltbl[j][m][2] == sizearray[i])
                        printf("HEAVY HITTER: %s:%zu: %zu bytes (~%.1lf%%)\n", (char *)stat.fltbl[j][0][1], stat.fltbl[j][m][0], stat.fltbl[j][m][2], (double)stat.fltbl[j][m][2] * 100 / (double)stat.sizeallocated);
        // print the top several heavy-hitter by freq that occupy >12% total size
        for (int i = 0; i < 8; i++)
            if ((double)freqarray[i] / (double)stat.allocated > 0.12)
                for (uint m = 1; m <= k; m++)
                    if (stat.fltbl[j][m][1] == freqarray[i])
                        printf("HEAVY HITTER: %s:%zu: %zu times (~%.1lf%%)\n", (char *)stat.fltbl[j][0][1], stat.fltbl[j][m][0], stat.fltbl[j][m][2], (double)stat.fltbl[j][m][1] * 100 / (double)stat.allocated);
    }
}
