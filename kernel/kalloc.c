// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"



struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
typedef struct node node;

struct node {
     node* next;
} ;

typedef struct {
    node* free_list;
    char* allocated;
    char* split;
} free_area_t;

void*
list_pop(node** n) {
    void* p = *n;
    (*n) = (*n)->next;
    return p;
}

void
list_push(node** n, node* p) {
    p->next = (*n);
    (*n) = p;
}

void
list_remove(node** n, node* p) {
    node* temp = *n;
    if(temp == p) {
        list_pop(n);
        return;
    }
    while(temp && temp->next != p) {
        temp = temp->next;
    }
    if(!temp) {
        return;
    }
    temp->next = temp->next->next;
    return;
}
void
list_print(node* n) {
    node* temp = n;
    while(temp) {
        cprintf(" %p", temp);
        temp = temp->next;
    }
    cprintf("\n");
}
struct {
    struct spinlock lock;
    free_area_t free_areas[MAXORDER];
} free_area_list;


// The allocator keeps track of free blocks in the free_area_list struct
// the free_area_list contains an array of free areas that keeps
// track of free blocks.
// Each index of the array keeps track of a different size order of
// free blocks
// Each free area has character arrays that keep track of whether the
// the blocks of that size are allocated or split
// Each character consists of 8 bits, so each character can store
// info about 8 different blocks.

// checks if a bit is set
int bit_is_set(char* arr, int index) {
    char a = arr[index >> 3];
    char b = 1 << (index & (7));
    return (a & b) == b;
}

// sets a bit to 1
void set_bit(char* arr, int index) {
    char a = arr[index >> 3];
    char b = 1 << (index & (7));
    arr[index >> 3] = (a | b); //keep all of the original bits, plus
                               //the added bit at index
}

// clears a bit to 0
void clear_bit(char* arr, int index) {
    char a = arr[index >> 3];
    char b = 1 << (index & 7);
    arr[index >> 3] = (a & ~b); //keep the original bits (a) but set
                                //one of them to 0
}

extern char end[]; // first address after kernel loaded from ELF file


void
print_allocator() {
    cprintf("===Allocator State===\n");
    void* base = (void*)ROUNDUP((uint)end, MAXSIZE);  //the address of the first page
    void* bounds = (void*) ROUNDDOWN((uint)PHYSTOP, MAXSIZE); //the byte after the last byte of the last page
    for(int i = 0; i < MAXORDER; i++) {
        cprintf("Free list for size %d (%d bytes):\n", i, BLOCKSIZE(i));
        list_print(free_area_list.free_areas[i].free_list);

        unsigned int n_pages = ((uint)bounds - (uint)base) / (BLOCKSIZE(i));
        cprintf("Allocated: \n");
        for(int j = 0; j < n_pages; j++) {
            cprintf(" %d", bit_is_set(free_area_list.free_areas[i].allocated, j));

            
        }
        cprintf("\n");
        if(i > 0) {
            cprintf("Split: \n");
             for(int j = 0; j < n_pages; j++) {
                cprintf(" %d", bit_is_set(free_area_list.free_areas[i].split, j));

                
            }
            cprintf("\n");
           
        }
    }
}



void
buddy_init(void) {
    initlock(&free_area_list.lock, "buddy");
    void* base = (void*)ROUNDUP((uint)end, MAXSIZE);  //the address of the first page
    void* bounds = (void*) ROUNDDOWN((uint)PHYSTOP, MAXSIZE); //the byte after the last byte of the last page
    void* offset = end;
    for(int i = MAXSIZE; i >= 0; i--) {
        //initialize each of the free areas
        //calculate the max number of pages that could be generated at that size
        unsigned int n_pages = ((uint)bounds - (uint)base) / (BLOCKSIZE(i));
        // calculate how many characters are needed for there to be at least 
        // one bit for every page
        uint n_chars = (((n_pages + 7) >> 3 ) << 3) >> 3;
        //point the allocated bitmap to the current offset;
        free_area_list.free_areas[i].allocated = offset;
        //nothing is allocated so set the whole bitmap to 0
        memset(offset, 0, n_chars);
        //increment the offset by the size of the bitmap
        offset = (void*)((uint)offset +n_chars);
        if(i != 0) {
            //all orders need a split bitmap except order 0
            free_area_list.free_areas[i].split = offset;
            //nothing has been split yet so set the whole bitmap to 0
            memset(offset, 0, n_chars);
            //increment the offset
            offset = (void*)((uint)offset +n_chars);
        }

    }
    // the free_list points to the base because every page is available
    free_area_list.free_areas[MAXSIZE].free_list = base;
    offset = base;
    for(; (uint)offset + MAXPGSIZE <= (uint)PHYSTOP; offset = (void*)((uint)offset + MAXPGSIZE)) {
        if((uint)offset + 2* MAXPGSIZE > (uint)PHYSTOP) {
            // if this is the last page then next = NULL
            ((node*)offset)->next = NULL; 
        } else {
            // if there are more pages, then set next to the next block
            ((node*)offset)->next = offset + MAXPGSIZE;
        }
    }
}


int
min_order(unsigned int n) {
    //what is the minimum order that can fit a request for n bytes
    int order = 0;
    int size = PGSIZE;
    while(size < n){
        order++;
        size *= 2;
    }
    return order;
}

int
get_index(void* p, int o) {
    // find index of the page at address p with order 0
    int n = (uint)p - ROUNDUP((uint)end, MAXSIZE);
    return n / BLOCKSIZE(o);
}

void*
get_address(int i, int o) {
    // get the address associated with index i of size o
    return (void*)(ROUNDUP((uint)end, MAXSIZE) + i * BLOCKSIZE(o));
}

void*
buddy_alloc(uint size){
    acquire(&free_area_list.lock);
    int min;
    //find a free page with the smallest order that will fit the request
    min = min_order(size);
    int i;
    for(i = min; i < MAXORDER; i++) {
        if(free_area_list.free_areas[i].free_list) {
            break;
        }
    }
    if(i == MAXORDER) {
        //if no free pages were found then return null
        release(&free_area_list.lock);       
        return NULL;
    }

    //weve found a page, now split it until it is the correct size
    //first pop it from its free list
    void* p = list_pop(&free_area_list.free_areas[i].free_list);
    //mark it as allocated
    set_bit(free_area_list.free_areas[i].allocated, get_index(p, i));
    for( ; i > min; i--) {
        //find the other half of the split block that will be free
        void* q = (void*)((uint)p + BLOCKSIZE(i-1));
        //mark the block that was split as such
        set_bit(free_area_list.free_areas[i].split, get_index(p, i));
        //mark one of  the resultant split blocks as allocated
        set_bit(free_area_list.free_areas[i-1].allocated, get_index(p, i-1));
        //push the free half to its free list;
        list_push(&(free_area_list.free_areas[i-1].free_list), q);

    }

    release(&free_area_list.lock);
    return p;
    
} 

uint size_of(void* p) {
    for(int i = 0; i < MAXSIZE; i++) {
        if(bit_is_set(free_area_list.free_areas[i+1].split, get_index(p, i+1)))
            return i;
    }
    return 0;
}
void
buddy_free(void* p) {
    //first find the size of p, then check for coalescing up to the max size
    uint sz = size_of(p);

    if(!bit_is_set(free_area_list.free_areas[sz].allocated, get_index(p, sz))) {
        return;
    }
    int i = 0;
    acquire(&free_area_list.lock);
    for(i = sz; i < MAXORDER; i++) {
        //get the index of p
        int index = get_index(p, i);
        //set it to unallocated
        clear_bit(free_area_list.free_areas[i].allocated, index);
        //get its buddy.
        //case 1: p is the second block in its pair, then we need index -1
        //case 2: p is the first block in its pair, then we need index + 1
        int buddy_index;
        if(index % 2  == 0) {
            //if p is the first block:
            buddy_index = index + 1;
        } else {
            buddy_index = index -1;
        }
        if(bit_is_set(free_area_list.free_areas[i].allocated, buddy_index)) {
            //if the buddy is allocated then we're done;
            break;
        }

        //remove the buddy from the free list
        void* buddy_address = get_address(buddy_index, i);
        list_remove(&free_area_list.free_areas[i].free_list, buddy_address);
        //if the buddy is the first in its pair, then reset p to be equal to the buddy
        if(buddy_index % 2 == 0) {
            p = buddy_address;
        }
        //the block one level up is no longer split
        //mark it as such
        if(i < MAXSIZE)
        clear_bit(free_area_list.free_areas[i+1].split, get_index(p, i+1));
    }
    list_push(&free_area_list.free_areas[i].free_list, p);
    release(&free_area_list.lock);
}


// Initialize free list of physical pages, of size 4MB (MAXPGSIZE)
void
kinit(void)
{
    buddy_init();
//  char *p;
//
//  initlock(&free_area_list.lock, "free_list");
//  //align p to 4MB alignment
//  p = (char*)PGROUNDUP((uint)end);
//  for(; p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE)
//    //add the 4MB page to the free area
//    //TODO
//    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP) 
    panic("kfree");


  buddy_free(v);
//  struct run *r;
//
//  if((uint)v % PGSIZE || v < end || (uint)v >= PHYSTOP) 
//    panic("kfree");
//  //figure out how big the page is
//  
//  // Fill with junk to catch dangling refs.
//  memset(v, 1, PGSIZE);
//
//  acquire(&kmem.lock);
//  r = (struct run*)v;
//  r->next = kmem.freelist;
//  kmem.freelist = r;
//  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  return buddy_alloc(4096);
//  acquire(&kmem.lock);
//  r = kmem.freelist;
//  if(r)
//    kmem.freelist = r->next;
//  release(&kmem.lock);
//  return (char*)r;
}

