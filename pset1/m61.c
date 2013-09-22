#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// the following 6 variables will keep record of statistics
long long unsigned allocated = 0;
long long unsigned sizeallocated = 0;
long long unsigned freed = 0;
long long unsigned sizefreed = 0;
long long unsigned failed = 0;
long long unsigned failedsize = 0;

// sztbl stores all necessary informations on allocated memory
// [0][1] stores current length, have to increase the number of rows 
// allocated for heavier uses
// [0] is the pointer's address, [1] is actual (data) size,
// [2] is aligned size (multiple of 4)
// [3] is the data in the next word after the current allocated block
// [4] is the number in the file name, [5] is line number
int sztbl[500][6] = {{0, 1, 0, 0}};

// fltable stores size and frequency with respect of each file and line pair
// each sheet stores allocation info for each file,
// fist row stores number of files in [0], current file name pointer in [1],
// and number of lines in [2], each of the following row stores info for each
// memory allocating line of code in this file, [1] frequency, [2] total size
unsigned long long fltbl[5][100][3] = {{{1, 0, 0}}};

void *m61_malloc(size_t sz, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    void * retptr = malloc(sz);
    // record as fail if returned pointer is null
    if (retptr == NULL){
        failed++;
        failedsize += sz;
        return retptr;
    }
    else {
    // on success, record as allocated
        allocated++;
        sizeallocated += sz;
        // record allocation info of this line of code
        for(uint j = 0; j < fltbl[0][0][0]; j++){
            if(fltbl[0][0][1] == 0){
                fltbl[0][0][1] = (int)file;
            }
            if((uint)file == fltbl[j][0][1]){
                if(fltbl[j][0][2] == 0){
                    fltbl[j][1][0] = line;
                    fltbl[j][0][2] += 1;
                }
                for(uint k = 1; k <= fltbl[j][0][2]; k++){
                    if((uint)line == fltbl[j][k][0]){
                        fltbl[j][k][1] += 1;
                        fltbl[j][k][2] += sz;
                        break;
                    } else if(k == fltbl[j][0][2]){
                        fltbl[j][0][2] += 1;
                        fltbl[j][k+1][0] = line;
                    }
                }
                break;
            } else if(j == fltbl[0][0][0]-1){
                fltbl[0][0][0] += 1;
                fltbl[j+1][0][1] = (int)file;
            }
        }
        for(int i = 1; i <= sztbl[0][1]; i++){
            // updates "next block" data, in case of later allocation overwrite
            if(sztbl[i][0] != 0){
                sztbl[i][3] = *(int *)((char *)(sztbl[i][0]) + sztbl[i][2]);
            }
            // if occupies a previously emptied block
            if(sztbl[i][0] == (int)retptr && sztbl[i][1] == 0){
                sztbl[i][1] = sz;
                while (sz%4 != 0){
                    sz++;
                }
                sztbl[i][2] = sz;
                sztbl[i][3] = *(int *)((char *)retptr+sz);
                sztbl[i][4] = (int)file;
                sztbl[i][5] = line;
                return retptr;
            }
        }
        // put new info of allocation to the end of the table
        sztbl[(sztbl[0][1] + 1)][0] = (int)retptr;
        sztbl[(sztbl[0][1] + 1)][1] = sz;
        sztbl[(sztbl[0][1] + 1)][4] = (int)file;
        sztbl[(sztbl[0][1] + 1)][5] = line;
        while (sz%4 != 0){
            sz++;
        }
        sztbl[(sztbl[0][1] + 1)][2] = sz;
        sztbl[(sztbl[0][1] + 1)][3] = *(int *)((char *)retptr+sz);                
        sztbl[0][1] += 1;
        return retptr;
     }
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // out of heap free
    if (0x804b004 > (int)ptr || 0xb000000 < (int)ptr){
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, \
not in heap\n", file, line, ptr);
        return;
    }
    for (int i = 1; i <= sztbl[0][1]; i++){
        if (sztbl[i][0] == (int)ptr && sztbl[i][1] != 0){
            // check for over-written data after the end of an allocated block
            if(*(int *)((char *)ptr + sztbl[i][2]) != sztbl[i][3]){
                printf("MEMORY BUG: %s:%d: detected wild write during \
free of pointer %p\n", file, line, ptr);
                return;
            }
            // free success
            sizefreed += sztbl[i][1];
            // set size as 0
            sztbl[i][1] = 0;
            sztbl[i][2] = 0;
            freed++;
            break;
        }
        // if freeing twice
        if (sztbl[i][0] == (int)ptr && sztbl[i][1] == 0){
            printf("MEMORY BUG: %s:%d: invalid free of \
pointer %p\n", file, line, ptr);
            return;
        }
        // an address not in the list
        if (i == sztbl[0][1]){
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, \
not allocated\n", file, line, ptr);
            for(i = 1; i <= sztbl[0][1]; i++){
                if(sztbl[i][0] < (int)ptr && sztbl[i][0] + sztbl[i][1] > (int)ptr){
                    printf("  %s:%d: %p is %d bytes inside a %d byte region \
allocated here\n", file, sztbl[i][5], ptr, (int)ptr-sztbl[i][0], sztbl[i][1]);
                }
            }
            return;
       }
    }
    free(ptr);
}

void *m61_realloc(void *ptr, size_t sz, const char *file, int line) {
    void *new_ptr = NULL;
    if (sz){
        new_ptr = m61_malloc(sz, file, line);
        if(ptr){
            // copies data into new block
            for (int i = 0; i < ((*((int *)ptr-1))/16)*16; i++){
                *((char *)new_ptr+i) = *((char *)ptr+i);
            }
            // updates data word adjacent to all previously allocated blocks
            for(int i = 1; i <= sztbl[0][1]; i++){
                if(sztbl[i][0] != 0){
                    sztbl[i][3] = *(int *)((char *)(sztbl[i][0]) + sztbl[i][2]);
                }
            }            
            m61_free(ptr, file, line);
        }
    }
    else{
        m61_free(ptr, file, line);
    }
    return new_ptr;
}

void *m61_calloc(size_t nmemb, size_t sz, const char *file, int line) {
    void *ptr = NULL;
    // prevents size overflow
    if ((long long)nmemb * (long long)sz < (long long)((size_t) - 1)) {
        ptr = m61_malloc(nmemb * sz, file, line);
    }
    else {
        failed++;
    }
    if (ptr)
        memset(ptr, 0, nmemb * sz);
    return ptr;
}

void m61_getstatistics(struct m61_statistics *stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(struct m61_statistics));
    //writes cumulative variables to struct
    stats->nactive = allocated - freed;
    stats->active_size = sizeallocated - sizefreed;
    stats->ntotal = allocated;
    stats->total_size = sizeallocated;
    stats->nfail = failed;
    stats->fail_size = failedsize;
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
    for(int i = 1; i <= sztbl[0][1]; i++){
        if(sztbl[i][1] != 0){
            printf("LEAK CHECK: %s:%d: allocated object %p with size \
%i\n", (char *)sztbl[i][4], sztbl[i][5], (char *)sztbl[i][0], sztbl[i][1]);
        }
    }
}

// a compare function for qsort
int comparellu (const void *a, const void *b){ 
       unsigned long long *lla = (unsigned long long *) a;
       unsigned long long *llb = (unsigned long long *) b;
       return (*lla < *llb) - (*lla > *llb);
} 

void m61_printhhreport(void){
    for(uint j = 0; j < fltbl[0][0][0]; j++){
        // the following two arrays arefor sorting with respect to freq and size
        unsigned long long freqarray[100] = {0};
        unsigned long long sizearray[100] = {0};
        uint k;
        // copies freq data and size data to the two separate arrays
        for(k = 1; k <= fltbl[j][0][2]; k++){
            freqarray[k-1] = fltbl[j][k][1];
            sizearray[k-1] = fltbl[j][k][2];
        }
        // sorts the two arrays
        qsort(freqarray, k, sizeof(unsigned long long), comparellu);
        qsort(sizearray, k, sizeof(unsigned long long), comparellu);
        // print the top several heavy-hitter by size that occupy >12% total size
        for(int i = 0; i < 8; i++){
            if((double)sizearray[i]/(double)sizeallocated > 0.12)
                for(uint m = 1; m <= k; m++){
                    if(fltbl[j][m][2] == sizearray[i]){
                        printf("HEAVY HITTER: %s:%llu: %llu bytes (~%.1lf%%)\n\
", (char *)fltbl[j][0][1], fltbl[j][m][0], fltbl[j][m][2], (double)fltbl[j][m][2] * 100 / (double)sizeallocated);
                    }
                }
        }
        // print the top several heavy-hitter by freq that occupy >12% total size
        for(int i = 0; i < 8; i++){
            if((double)freqarray[i] / (double)allocated > 0.12)
                for(uint m = 1; m <= k; m++){
                    if(fltbl[j][m][1] == freqarray[i]){
                        printf("HEAVY HITTER: %s:%llu: %llu times (~%.1lf%%)\n\
", (char *)fltbl[j][0][1], fltbl[j][m][0], fltbl[j][m][2], (double)fltbl[j][m][1] * 100 / (double)allocated);
                    }
                }
        }
    }
}
