/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * 
 * For more information, please refer to <http://unlicense.org/>
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

// FIXME: lots of pointer-arith and conversion warnings
// gcc -Wall -Wextra -Wconversion -ansi -Wpedantic -std=c11 -O0 -g buddy.c

// re using structs and typedef to reduce refactor
struct bmap_allocator;
typedef struct bmap_allocator bmap_allocator_t;


/* For a detailed description of this binary buddy allocation algorithm
 * please refers to :
 *     The Art of Computer Programming Vol.1
 *     The buddy system p.442
 */

void* internal_heap_start;

// each pages represent PAGE_SIZE bytes
#define PAGE_SIZE 1024

// type index stands for a block number index
typedef uint16_t index;

// a node in the double linked list
struct buddy_node
{
    uint8_t tag:1; // 0 = reserved, 1 = available
    uint8_t size:5; // size of a block (memory available = 2^size, here max is 2^(2^5))
    uint8_t filler:2; // not used
    index backward; // # of next node in the linked list
    index forward; // # of backward node in the linked list
};

// total size of memory 2^LOG2_NR_PAGES, currently testing with only 2^8 pages... This is 'm' in TAOCP
#define LOG2_NR_PAGES 4

index* avail; // array that point to the head of the linked list for each block size
struct buddy_node* blocks; // each page of memory is represented by a node block in this array

// LIMIT is the maximum size for merging free blocks together (should be = LOG2_NR_PAGES which represent a single block for all the available memory)
#define LIMIT LOG2_NR_PAGES
// ALPHA is a special constant for the role of NULL in the linked lists
#define ALPHA 65535U

// the overhead of this algorithm is the memory used for avail and blocks
size_t metadata_size = (1+(sizeof(index)*(LOG2_NR_PAGES+1) + sizeof(struct buddy_node)*(1<<LOG2_NR_PAGES))/PAGE_SIZE)*PAGE_SIZE;
bool buddy_init = false;


// allow conversion between memory addresses and blocks indexes
#define BLOCK_TO_ADDRESS(n) (internal_heap_start + metadata_size + n*PAGE_SIZE)
#define ADDRESS_TO_BLOCK(a) ((a - (internal_heap_start + metadata_size))/PAGE_SIZE)



void buddy_pushfront(index block, size_t k)
{
    blocks[block].tag = 1;
    blocks[block].size = k;
    blocks[block].forward = avail[k];
    blocks[block].backward = ALPHA;

    if (avail[k]!=ALPHA)
        blocks[avail[k]].backward = block;

    avail[k] = block;
}

index buddy_popfront(size_t k)
{
    assert(avail[k]!=ALPHA);

    index current = avail[k];
    avail[k] = blocks[current].forward;

    return current;
}

void buddy_remove(index block, size_t k)
{
    index backward = blocks[block].backward;
    index forward = blocks[block].forward;

    blocks[block].backward = ALPHA;
    blocks[block].forward = ALPHA;
    blocks[block].tag = 1;

    if (backward != ALPHA)
        blocks[backward].forward = forward;
    else avail[k] = forward;

    if (forward!=ALPHA)
        blocks[forward].backward = backward;
}

bmap_allocator_t *bmap_init(uint64_t start_addr, size_t n_pages)
{
    // TODO: use given parameter instead of re processing the values
    avail = (index*)internal_heap_start;
    blocks = (struct buddy_node*)(internal_heap_start + sizeof(index)*(LOG2_NR_PAGES+1));

    for (size_t i=0; i<=LOG2_NR_PAGES; ++i)
    {
        avail[i] = ALPHA;
    }
    buddy_pushfront(0, LOG2_NR_PAGES);

    // used to match the "structure" of the function
    return NULL;
}

// returns memory for 'count' pages
void *bmap_alloc(bmap_allocator_t *alloc, size_t count)
// void * buddy_malloc(size_t count)
{
    if (!buddy_init)
    {
        buddy_init = true;
        bmap_init((uint64_t)NULL, 0);
    }

    //FIXME: change that 'magic constant' 31...
    // we need the exponent of the immediate upper power of two
    size_t k = 31- __builtin_clz(count);

    if (k>LOG2_NR_PAGES) return NULL;

    // search for the 'j' (k <= j <= m) that have a free block (size=2^j)
    // R1 instructions p.443
    size_t j=k;
    while (!(j==LOG2_NR_PAGES+1 || avail[j]!=ALPHA))
    {
        ++j;
    }
    // if no suitable 'j'
    if (j==LOG2_NR_PAGES+1) return NULL;

    // else we have to split the avail[j] block in smaller ones
    // R2 instructions
    index block = buddy_popfront(j);

    while (!(j==k))
    {
        // R4 instructions
        --j;
        buddy_pushfront(block+(1<<j), j);
    }
    // and finally we have the block at index block with a correct size
    // R3 condition
    blocks[block].tag = 0;
    blocks[block].size = k;
    return BLOCK_TO_ADDRESS(block);
}

void bmap_free(bmap_allocator_t *alloc, void *addr, size_t n)
// void buddy_free(void* addr)
{
    index block = ADDRESS_TO_BLOCK(addr);
    uint16_t k = blocks[block].size;

    index buddy_block;

    // find our buddy block (Eq.10 p.442)
    if (((block>>k) & 0x1) == 0)
        buddy_block = block + (1<<k);
    else
        buddy_block = block - (1<<k);

    // if we can merge this block and its buddy (S1 condition p.444)
    while(!(k==LOG2_NR_PAGES || blocks[buddy_block].tag == 0 || blocks[buddy_block].size != k))
    {
        // S2 instructions
        buddy_remove(buddy_block, k);
        ++k;

	if(buddy_block<block) block=buddy_block;

        // update the buddy block
        if (((block>>k) & 0x1) == 0)
            buddy_block = block + (1<<k);
        else
            buddy_block = block - (1<<k);
    }
    // S3
    buddy_pushfront(block, k);
}




#define N 100
void * allocations[N];
size_t n_alloc=0;

void test_malloc(size_t s, index i)
{
	printf("%s %d ", __func__, s);
	void *a = bmap_alloc(NULL, s);
	allocations[n_alloc] = a;
	n_alloc=n_alloc+1;

	index block;
	if (a!=NULL)
	{
		block = ADDRESS_TO_BLOCK(a);
	} else {
		block = ALPHA;
	}
	printf("(addr==%p, block==%d) ", a, block);
	printf(": %s\n", (block==i)?"OK":"FAILED");
}

void test_free(void *a)
{
	printf("%s %p\n", __func__, a);
	bmap_free(NULL, a, 0);
}

int main()
{
	internal_heap_start = malloc(metadata_size+(1<<LOG2_NR_PAGES)*PAGE_SIZE);
	printf("----------\n");
	printf("We have %ld pages of memory (total usable == %ld + metadata_size == %ld)\n", (unsigned long int)(1<<LOG2_NR_PAGES), (unsigned long int)(PAGE_SIZE*(1<<LOG2_NR_PAGES)), metadata_size);
	printf("----------\n");

	test_malloc(8, 0);
	test_malloc(1, 8);
	test_malloc(1, 9);
	test_malloc(1, 10);
	test_malloc(4, 12);
	test_malloc(2, ALPHA);
	test_malloc(1, 11); // memory should be full now
	test_malloc(1, ALPHA);
	test_free(allocations[0]);
	test_malloc(1, 0);
	test_malloc(4, 4);
	test_malloc(1, 1);
	test_malloc(4, ALPHA); // we have not enough memory for this block
	test_free(allocations[1]);
	test_free(allocations[2]);
	test_free(allocations[3]);
	test_free(allocations[6]);
	test_malloc(4, 8); // now it's ok
	test_malloc(4, ALPHA);
	test_malloc(2, 2);

	return 0;
}
