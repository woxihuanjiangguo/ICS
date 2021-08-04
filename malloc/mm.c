/*
 * segregated free lists + implicit free list
 * for further information, check the lab document
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* My student ID */
    "ID: 19302010007",
    /* First member's full name */
    "Li Zhenxin",
    /* First member's email address */
    "19302010007@fudan.edu.cn",
    "",
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* basic constants and macros from textbook */
#define WSIZE 4 /* single word */
#define DSIZE 8 /* double word */
#define CHUNKSIZE (1<<12) /* the length of heap extension */  
#define INITCHUNKSIZE (1<<6)/* the length of init heap size */
/* function max & min */
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

/* pack size and alloc flag into a word */
#define PACK(size, alloc) ((size)|(alloc))

/* read and write from addr p */
#define GET(p)              (*(unsigned int *)(p))
#define PUT(p, val)         (*(unsigned int *)(p) = (val) | GET_TAG(p))
#define PUT_NORTAG(p, val)  (*(unsigned int *)(p) = (val))

/* for manipulating pointers */
#define SET_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))

/* something we can see from the text book 
* they are info contained in the header, no exception
*/
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_TAG(p)   (GET(p) & 0x2)
#define SET_RTAG(p)   (GET(p) |= 0x2)
#define REMOVE_RTAG(p) (GET(p) &= ~0x2)

/* something we can see from the text book */
#define HDRP(ptr) ((char *)(ptr) - WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

/* BLKP, in terms of physical memory alignment (also textbook) */
/* PTR, in terms of the addr in segregated explict lists (the alignment of free blocks) */
#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE((char *)(ptr) - WSIZE))
#define PREV_BLKP(ptr) ((char *)(ptr) - GET_SIZE((char *)(ptr) - DSIZE))
#define SUCC_PTR(ptr) ((char *)(ptr))
#define PRED_PTR(ptr) ((char *)(ptr) + WSIZE)

/* if we dereference the bp or bp + WSIZE, we get ptr to entries in lists */
#define SUCC(ptr) (*(char **)(SUCC_PTR(ptr)))
#define PRED(ptr) (*(char **)(PRED_PTR(ptr)))


/* macros involving segregated free lists */
#define MAXSIZE 20
#define BUFFER  (1 << 7)    // to improve reallocation efficiency
#define GET_FREELISTS(index) (*(free_ptr + index))
#define SET_FREELISTS(index, ptr) (*(free_ptr + index) = ptr)

/* GLOBAL VARIABLES - no arrays, just a scalar pointer */

static void **free_ptr;

/* END GLOBAL VARIABLES */


/* HELPER FUNCTION DECLARATIONS */

static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *place(void *bp, size_t asize);
/* deal with the segregated list here */
static int fit_list(size_t size);
static void *fit_node(size_t size);
static void insert(void *bp, size_t size);
static void delete(void *bp);
static void *stupid_realloc(void *bp, size_t size, size_t asize);
/* a list of checker routines */
static int mm_check(int verbose);
static int heap_in_range_checker(int verbose);
static int coalesce_checker(int verbose);
static int free_in_lists_checker(int verbose);
static int lists_free_valid_checker(int verbose);
/* END FUNCTION DECLARATIONS */


/*
* extend_heap - called during init, malloc, realloc
*               init (quite apparent)
*               malloc (when current heap size is too small)
*               realloc (when buffer can't meet our demands)
*/
static void *extend_heap(size_t size)
{
    void *bp;
    size_t asize = ALIGN(size);

    // failed to increase brk
    if(((bp = mem_sbrk(asize)) == (void *)-1)){
        return NULL;
    }

    // Set headers and footer (the start of everything)
    PUT_NORTAG(HDRP(bp), PACK(asize, 0));  
    PUT_NORTAG(FTRP(bp), PACK(asize, 0));   
    // next block has size --- 0
    PUT_NORTAG(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 
    insert(bp, asize);

    return coalesce(bp);
}

/*
* coalesce - 4 cases presented on the text books
*            deal with the implicit list
*            the RTAG, we don't interfere with it
*/
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // do not coalesce previous block marked with rtag
    if(GET_TAG(HDRP(PREV_BLKP(bp)))){
        prev_alloc = 1;
    }
    // case 1 - shouldn't insert the node into segregated list (already there)
    if(prev_alloc && next_alloc){
        return bp;
    }
    // case 2 - prev empty
    else if(prev_alloc && !next_alloc){
        delete(bp);
        delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));   // size to locate footer modified
    }
    // case 3 - next empty
    else if(!prev_alloc && next_alloc){
        delete(bp);
        delete(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));   // size to locate footer modified
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // case 4 - prev & next both empty
    else {
        delete(bp);
        delete(PREV_BLKP(bp));
        delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    insert(bp, size);
    return bp;
}

/*
* place - put the allocated block into a free block
*         
*/
static void *place(void *bp, size_t asize)
{
    size_t bp_block_size = GET_SIZE(HDRP(bp));
    size_t leftover = bp_block_size - asize;

    delete(bp);
    
    if (leftover <= DSIZE * 2) {
        // Do not split block 
        PUT(HDRP(bp), PACK(bp_block_size, 1)); 
        PUT(FTRP(bp), PACK(bp_block_size, 1)); 
    }
    // if too big, place at the back of the free block
    else if (asize >= 112) {
        // Split block
        PUT(HDRP(bp), PACK(leftover, 0));
        PUT(FTRP(bp), PACK(leftover, 0));
        PUT_NORTAG(HDRP(NEXT_BLKP(bp)), PACK(asize, 1));
        PUT_NORTAG(FTRP(NEXT_BLKP(bp)), PACK(asize, 1));
        insert(bp, leftover);
        return NEXT_BLKP(bp);
    }
    else {
        // Split block
        PUT(HDRP(bp), PACK(asize, 1)); 
        PUT(FTRP(bp), PACK(asize, 1)); 
        PUT_NORTAG(HDRP(NEXT_BLKP(bp)), PACK(leftover, 0)); 
        PUT_NORTAG(FTRP(NEXT_BLKP(bp)), PACK(leftover, 0)); 
        // the leftover should be inserted into free_lists
        insert(NEXT_BLKP(bp), leftover);
    }
    return bp;
}
/*
* fit_list - find the home for our new node 
*/
static int fit_list(size_t size)
{
    int temp = 0;

    while((size > 1) && (temp < MAXSIZE - 1)){
        temp ++;
        size >>= 1;
    }
    
    return temp;
}

/*
* fit_node - during the malloc and realloc, get the fit node
*/
static void *fit_node(size_t size)
{
    int num = fit_list(size);
    void *ptr = GET_FREELISTS(num);
    
    // we start at a bigger listnum
    while (num < MAXSIZE) {
        if (GET_FREELISTS(num) != NULL) {
            ptr = GET_FREELISTS(num);
            // Ignore blocks that are too small or marked with the reallocation bit
            while ((ptr != NULL) && ((size > GET_SIZE(HDRP(ptr))) || (GET_TAG(HDRP(ptr)))))
            {
                ptr = SUCC(ptr);
            }
            if (ptr != NULL)
                break;
        }
        num ++;
    }
    return ptr;
}

/*
* insert - insert a node into one list
* one list from the free_lists:
* Start (NULL)
*   |
* (PRED up)
* Node 1    -- size small
* (SUCC down)
*   |
* Node 2    -- size big
*/
static void insert(void *bp, size_t size)
{
    // the list for our node
    int list = fit_list(size);
    // bp1 refers to the search result
    void *bp1 = GET_FREELISTS(list);
    // bp2 refers to the place to insert
    void *bp2 = NULL;

    while ((bp1 != NULL) && (size > GET_SIZE(HDRP(bp1)))){
        bp2 = bp1;
        bp1 = SUCC(bp1);
    }

    // 4 insert cases
    if (!bp1 && !bp2) {
        // case 1 -- bp1 & bp2 both NULL; a new list
        SET_PTR(PRED_PTR(bp), NULL);
        SET_PTR(SUCC_PTR(bp), NULL);
        SET_FREELISTS(list, bp);
    } else if (!bp1 && bp2) {
        // case 2 -- bp1 NULL; a node inserted at the bottom
        SET_PTR(SUCC_PTR(bp), NULL);
        SET_PTR(PRED_PTR(bp), bp2);
        SET_PTR(SUCC_PTR(bp2), bp);
    } else if (bp1 && !bp2) {
        // case 3 -- bp2 NULL; a node inserted at the top
        SET_PTR(SUCC_PTR(bp), bp1);
        SET_PTR(PRED_PTR(bp1), bp);
        SET_PTR(PRED_PTR(bp), NULL);
        SET_FREELISTS(list, bp);
    } else {
        // case 4 -- bp1 & bp2 neither NULL; just normal
        SET_PTR(SUCC_PTR(bp), bp1);
        SET_PTR(PRED_PTR(bp1), bp);
        SET_PTR(PRED_PTR(bp), bp2);
        SET_PTR(SUCC_PTR(bp2), bp);
    }
}

/*
* delete - delete a node that is inside one list
*/
static void delete(void *bp)
{
    int list = fit_list(GET_SIZE(HDRP(bp)));
    // 4 delete cases
    if (!SUCC(bp) && !PRED(bp)) {
        // case 1 -- bp is the lonely node
        SET_FREELISTS(list, NULL);
    } else if (!SUCC(bp) && PRED(bp)) {
        // case 2 -- bp is at the bottom
        SET_PTR(SUCC_PTR(PRED(bp)), NULL);
    } else if (SUCC(bp) && !PRED(bp)) {
        // case 3 -- bp is at the top
        SET_PTR(PRED_PTR(SUCC(bp)), NULL);
        SET_FREELISTS(list, SUCC(bp));
    } else {
        // case 4 -- bp is in the middle
        SET_PTR(SUCC_PTR(PRED(bp)), SUCC(bp));
        SET_PTR(PRED_PTR(SUCC(bp)), PRED(bp));
    }
}

void *stupid_realloc(void *bp, size_t size, size_t asize){
    void *result = mm_malloc(size);
    memcpy(result, bp, MIN(size, asize));
    mm_free(bp);
    return result;
}

/* end of the definition of helper routines */
/* the beginning of 4 functions to implement & our heap checker */

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // we just check our textbook
    char *heap_listp;

    // init our free_lists on the heap
    if ((long)(free_ptr = mem_sbrk(MAXSIZE * sizeof(void *))) == -1)
		return -1;

    // clear it
    for(int i = 0; i < MAXSIZE; i ++){
        SET_FREELISTS(i, NULL);
    }

    // Allocate memory for the initial empty heap 
    if ((long)(heap_listp = mem_sbrk(4 * WSIZE)) == -1)
        return -1;
    
    PUT_NORTAG(heap_listp, 0);                           /* alignment padding */
    PUT_NORTAG(heap_listp + WSIZE, PACK(DSIZE, 1));      /* prologue header */
    PUT_NORTAG(heap_listp + DSIZE, PACK(DSIZE, 1));      /* prologue footer */
    PUT_NORTAG(heap_listp + WSIZE + DSIZE, PACK(0, 1));  /* epilogue header */

    // my checker
    //mm_check(1);

    if (extend_heap(INITCHUNKSIZE) == NULL){
        return -1;
    }
    return 0;
}

/* 
 * mm_malloc - search the free lists thoroughly
 *             get the best block
 */
void *mm_malloc(size_t size)
{
    // Ignore size 0 cases
    if (size == 0){
        return NULL;
    }

    void *ptr = NULL;                /* pointer */
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : (ALIGN(size + DSIZE));      /* adjusted block size */
    
    // search our free_lists
    ptr = fit_node(asize);

    // after our exhaustive search
    // good block found :)
    if (ptr != NULL) {
        ptr = place(ptr, asize);
        // my checker
        //mm_check(1);
        return ptr;
    }

    // no good block :(
    if ((ptr = extend_heap(MAX(asize, CHUNKSIZE))) == NULL){
        return NULL;
    }
    ptr = place(ptr, asize);
    // my checker
    //mm_check(1);
    return ptr;
}

/*
 * mm_free - Freeing a block
 *           insert first, the coalescing process will delete the node
 *           and insert again
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
 
    REMOVE_RTAG(HDRP(NEXT_BLKP(ptr)));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    
    insert(ptr,size);
    coalesce(ptr);

    // my checker
    //mm_check(1);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *result = ptr;    /* Pointer to be returned */
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : (ALIGN(size + DSIZE)); /* Size of new block */
    int block_buffer = GET_SIZE(HDRP(ptr)) - asize; // current block buffer
    
    // simple cases here
    if (size == 0){
        mm_free(ptr);
    }
    
    if (ptr == NULL){
        return mm_malloc(size);
    }

    // 3 cases for small blocks
    if (block_buffer < 0) {
        // 1- fail to eat the next block (the next block is legit and allocated)
        if (GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && GET_SIZE(HDRP(NEXT_BLKP(ptr)))) {
            // do not modify, the only solution, except we check the PREV_BLKPs
            result = stupid_realloc(ptr, size, asize);
        } else {
            int flag = 1;
            int leftover = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr))) - asize;
            if (GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
                // 2.1 - lack heap, reach boundary
                if (leftover < 0) {
                    size_t extendsize = MAX(CHUNKSIZE, -leftover);
                    if (extend_heap(extendsize) != NULL){
                        leftover += extendsize;
                    } else {
                        return NULL;
                    }
                }
            } else if (leftover < 0) {
                // 2.2 - a normal block follows, but is too small :(
                result = stupid_realloc(ptr, size, asize);
                // do not eat the next block
                flag = 0;
            }
            
            // 3 eat the next block
            if (flag) {
                // flag to ignore stupid realloc cases
                delete(NEXT_BLKP(ptr));
                PUT_NORTAG(HDRP(ptr), PACK(asize + leftover, 1)); 
                PUT_NORTAG(FTRP(ptr), PACK(asize + leftover, 1));
            }
            
        }
        block_buffer = GET_SIZE(HDRP(result)) - asize;
    }
    
    if (block_buffer <= BUFFER)
        SET_RTAG(HDRP(NEXT_BLKP(result)));

    // my checker
    //mm_check(1);
    // Return the reallocated block 
    return result;
}

/*
* mm_check - function mm_check returns -1 if mistakes are detected
*            returns 0 if there are no mistakes
*            returns -1 if there are mistakes
*/
static int mm_check(int verbose){
    if(verbose)
        printf("************HEAP CHECKER STARTED************\n");
    int result1 = heap_in_range_checker(verbose);
    int result2 = coalesce_checker(verbose);
    int result3 = free_in_lists_checker(verbose);
    int result4 = lists_free_valid_checker(verbose);
    if(verbose)
        printf("************HEAP CHECKER ENDED************\n");
    return result1 | result2 | result3 | result4;
}

/*
* heap_in_range_checker - make sure we do not have blocks outside the heap
*/
static int heap_in_range_checker(int verbose){
    if(verbose) {
         printf("NO.1 - HEAP IN RANGE CHECKER READY TO GO\n");
     }
    void *heap_start = mem_heap_lo();
    void *heap_end = mem_heap_hi();
    int ok_flag = 1;

    void *temp = NEXT_BLKP(heap_start); // used for scanning, the first block should be omitted (always in heap) 

    while (GET_SIZE(temp)) {
        // loop until the boundary is hit
        if ((temp < heap_start)||(temp > heap_end)) {
            if (verbose) {
                printf("%p IS OUT OF THE HEAP!!!!\n", temp);
            }
            ok_flag = 0;
        }
        temp = NEXT_BLKP(temp);
    }

     if(verbose) {
         if (ok_flag) {
            printf("HEAP IN RANGE CHECKER SAYS OK!\n");
         } else {
            printf("HEAP IN RANGE CHECKER SAYS BAD!\n");
         }
     }
    return 0;
}

/*
* coalesce_checker - make sure consecutive free blocks are coalesced
*/
static int coalesce_checker(int verbose){
    if(verbose) {
         printf("NO.2 - COALESCE CHECKER READY TO GO\n");
     }
    int ok_flag = 1;
    void *temp = NEXT_BLKP(mem_heap_lo);   // still, skip the first block
    
    while (GET_SIZE(NEXT_BLKP(temp))) {
        // loop until the boundary is hit (the NEXT_BLKP of temp is not epilogue)
        void *temp_next = NEXT_BLKP(temp);
        if (temp_next == temp) {
            break;
        }
        if (!GET_ALLOC(HDRP(temp)) && !GET_ALLOC(HDRP(temp))) {
            if (verbose) {
                printf("%p and %p SHOULD COALESCE!!!!\n", temp, temp_next);
            }
            ok_flag = 0;
        }
        temp = temp_next;
    }

    if(verbose) {
        if (ok_flag) {
            printf("COALESCE CHECKER SAYS OK!\n");
        } else {
            printf("COALESCE CHECKER SAYS BAD!\n");
        }  
     }
    return 0;
}

/*
* free_in_lists_checker - make sure free ones in the heap are in the lists
*                         scan the heap
*/
static int free_in_lists_checker(int verbose){
    if(verbose) {
         printf("NO.3 - FREE IN LISTS CHECKER READY TO GO\n");
     }
    int ok_flag = 1;
    void *temp = NEXT_BLKP(mem_heap_lo);   // still, skip the first block

    while(GET_SIZE(temp)){
        void *next_temp = NEXT_BLKP(temp);
        if (next_temp == temp) {
            break;
        }
        if (!GET_ALLOC(HDRP(temp))) {
            if (!SUCC_PTR(temp) && !PRED_PTR(temp)) {
                if (verbose) {
                    printf("%p SHOULD BE IN ONE FREE LIST!!!!\n", temp);
                }
                ok_flag = 0;
            }
        }
        temp = next_temp;
    }

    if(verbose) {
        if (ok_flag) {
            printf("FREE IN LISTS CHECKER SAYS OK!\n");
        } else {
            printf("FREE IN LISTS CHECKER SAYS BAD!\n");
        }  
     }
    return 0;
}

/*
* lists_free_valid_checker - make sure those in the lists have correct HDR, FTR
*                            scan the lists
*/
static int lists_free_valid_checker(int verbose){
    if(verbose) {
         printf("NO.4 - FREE VALID CHECKER READY TO GO\n");
     }
    int ok_flag = 1;
    // scan MAXSIZE lists
    for (int i = 0;i < MAXSIZE;i++) {
        void *temp;
        if ((temp = GET_FREELISTS(i)) == NULL) {
            continue;
        }
        // for one individual list
        while (temp != NULL){
            // an allocated node is present (HDR OR FTR)
            if (GET_ALLOC(HDRP(temp)) || GET_ALLOC(FTRP(temp))) {
                if (verbose) {
                    printf("%p is allocated, it's in list %d\n", temp, i);
                }
                ok_flag = 0;
            }
            // a node has different sizes in HDR AND FTR
            if (GET_SIZE(HDRP(temp)) != GET_SIZE(FTRP(temp))) {
                if (verbose) {
                    printf("%p has different sizes in HDR and FTR, it's in list %d\n", temp, i);
                }
                ok_flag = 0;
            }
            temp = SUCC(temp);
        }
    }

    if(verbose) {
        if (ok_flag) {
            printf("FREE VALID CHECKER SAYS OK!\n");
        } else {
            printf("FREE VALID CHECKER SAYS BAD!\n");
        }  
     }
    return 0;
}