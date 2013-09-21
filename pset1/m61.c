#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// the following 6 variables will keep record of statistics
int allocated = 0;
int sizeallocated = 0;
int freed = 0;
int sizefreed = 0;
int failed = 0;
int failedsize = 0;
// sztbl stores all necessary informations on allocated memory, [0][1] stores current length
// [0] is the pointer's address, [1] is actual (data) size, [2] is aligned size (multiple of 4)
// [3] is the data in the next word after the current allocated block
// [4] is the number in the file name, [5] is line number
int sztbl[1000][6] = {0, 1};
// top is above any possible heap address
size_t top = 0xb000000;

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
        int i;
        for(i=1; i<=sztbl[0][1]; i++){
            // updates "next block" data, in case of over-written due to later allocation
            if(sztbl[i][0]!=0){
                sztbl[i][3] = *(int *)((char *)(sztbl[i][0])+sztbl[i][2]);
            }
            // if occupies a previously emptied block
            if(sztbl[i][0] == retptr && sztbl[i][1] == 0){
                sztbl[i][1] = sz;
                while (sz%4 != 0){
                    sz++;
                }
                sztbl[i][2] = sz;
                sztbl[i][3] = *(int *)((char *)retptr+sz);
                sztbl[i][4] = (file[4]-48)*100 + (file[5]-48)*10 + (file[6]-48);
                sztbl[i][5] = line;
                return retptr;
            }
        }
        // put new info of allocation to the end of the table
        sztbl[(sztbl[0][1]+1)][0] = retptr;
        sztbl[(sztbl[0][1]+1)][1] = sz;
        sztbl[(sztbl[0][1]+1)][4] = (file[4]-48)*100 + (file[5]-48)*10 + (file[6]-48);
        sztbl[(sztbl[0][1]+1)][5] = line;
        while (sz%4 != 0){
                    sz++;
                }
                sztbl[(sztbl[0][1]+1)][2] = sz;
                sztbl[(sztbl[0][1]+1)][3] = *(int *)((char *)retptr+sz);                
        sztbl[0][1] += 1;
//        top = top + (*((uint *)top)) - (*((uint *)top))%16;
        return retptr;
     }
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    int i;
    // out of heap free
    if (ptr < 0x804b004 || ptr > top){
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
        return;
    }
    for (i=1;i<=sztbl[0][1];i++){
        if (sztbl[i][0] == ptr && sztbl[i][1] != 0){
            // check for over-written data after the end of an allocated block
            if(*(int *)((char *)ptr+sztbl[i][2])!=sztbl[i][3]){
                printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
                return;
            }
            // free success
            sizefreed += sztbl[i][1];
            // set size as 0
            sztbl[i][1]=0;
            sztbl[i][2]=0;
            freed++;
            break;
        }
        // if freeing twice
        if (sztbl[i][0] == ptr && sztbl[i][1] == 0){
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p\n", file, line, ptr);
            return;
        }
        // an address not in the list
        if (i == sztbl[0][1]){
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            for(i=1;i<=sztbl[0][1];i++){
                if(ptr>sztbl[i][0] && ptr<sztbl[i][0]+sztbl[i][1]){
                    printf("  %s:%d: %p is %d bytes inside a %d byte region allocated here\n", file, sztbl[i][5], ptr, ptr-sztbl[i][0],sztbl[i][1]);
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
            uint i;
            // copies data into new block
            for (i=0; i < ((*((int *)ptr-1))/16)*16; i++){
                *((char *)new_ptr+i) = *((char *)ptr+i);
            }
            // updates data word adjacent to all previously allocated blocks
            for(i=1; i<=sztbl[0][1]; i++){
                if(sztbl[i][0]!=0){
                    sztbl[i][3] = *(int *)((char *)(sztbl[i][0])+sztbl[i][2]);
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
    if ((long long)nmemb * (long long)sz < (long long)((size_t) -1)) {
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

/*
struct m61_statistics {
    unsigned long long nactive;         // # active allocations
    unsigned long long active_size;     // # bytes in active allocations
    unsigned long long ntotal;          // # total allocations
    unsigned long long total_size;      // # bytes in total allocations
    unsigned long long nfail;           // # failed allocation attempts
    unsigned long long fail_size;       // # bytes in failed alloc attempts
};
*/

void m61_printstatistics(void) {
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

void m61_printleakreport(void) {
    int i;
    for(i=1;i<=sztbl[0][1];i++){
        if(sztbl[i][1]!=0){
            printf("LEAK CHECK: test%03d.c:%d: allocated object %p with size %i\n",sztbl[i][4],sztbl[i][5],sztbl[i][0],sztbl[i][1]);
        }
    }
}
