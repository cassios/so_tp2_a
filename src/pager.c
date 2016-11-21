#include "pager.h"

#include <sys/mman.h>

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mmu.h"

/////////////////////////////////list////////////////////////////////////////
struct dlist {
	struct dnode *head;
	struct dnode *tail;
	int count;
};
struct dnode {
	struct dnode *prev;
	struct dnode *next;
	void *data;
};
typedef void (*dlist_data_func)(void *data);
typedef int (*dlist_cmp_func)(const void *e1, const void *e2, void *userdata); 
struct dlist *dlist_create(void);
void dlist_destroy(struct dlist *dl, dlist_data_func);
void *dlist_pop_left(struct dlist *dl);
void *dlist_pop_right(struct dlist *dl);
void *dlist_push_right(struct dlist *dl, void *data);

/* this function calls =cmp to compare =data and each value in =dl.  if a
 * match is found, it is removed from the list and its pointer is returned.
 * returns NULL if no match is found. */
void *dlist_find_remove(struct dlist *dl, void *data, dlist_cmp_func cmp,
			void *userdata);

int dlist_empty(struct dlist *dl);

/* gets the data at index =idx.  =idx can be negative. */
void * dlist_get_index(const struct dlist *dl, int idx);
/* changes the data at index =idx.  does nothing if =idx does not exist. */
void dlist_set_index(struct dlist *dl, int idx, void *data);
////////////////////////////list end///////////////////////////////////////////////

typedef struct {
    int isvalid;
    int frame_number;
    int block_number;
    int prot;
    intptr_t vaddr;
} PageInfo;

typedef struct {
    pid_t pid;
    struct dlist *pages;
} PageTable;

typedef struct {
    pid_t pid;
    PageInfo *page;
    int accessed;
    int dirty;
} FrameTable;

typedef struct {
    PageInfo *page;
    int used;
} BlockTable;

int _nframes, _nblocks, _page_size;
FrameTable *frame_table;
BlockTable *blocks;
struct dlist *page_tables;
int _second_chance_index = 0;

/****************************************************************************
 * external functions
 ***************************************************************************/
int get_new_frame() {
    for(int i = 0; i < _nframes; i++) {
        if(frame_table[i].pid == -1) return i;
    }
    return -1;
}

int get_new_block() {
    for(int i = 0; i < _nblocks; i++) {
        if(blocks[i].page == NULL) return i;
    }
    return -1;
}

PageTable* find_page_table(pid_t pid) {
    for(int i = 0; i < page_tables->count; i++) {
        PageTable *pt = dlist_get_index(page_tables, i);
        if(pt->pid == pid) return pt;
    }
    printf("error: Pid not found\n");
    exit(-1);
}

PageInfo* get_page(PageTable *pt, intptr_t vaddr) {
    for(int i=0; i < pt->pages->count; i++) {
        PageInfo *page = dlist_get_index(pt->pages, i);
        if(page->vaddr == vaddr) return page;
    }
    printf("error in get_page_index: page was not found");
    exit(-1);
}

void pager_init(int nframes, int nblocks)
{
    _nframes = nframes;
    _nblocks = nblocks;
    _page_size = sysconf(_SC_PAGESIZE);

    frame_table = malloc(_nframes * sizeof(FrameTable));
    for(int i = 0; i < _nframes; i++) {
        frame_table[i].pid = -1;
    }

    blocks = malloc(_nblocks * sizeof(BlockTable));
    for(int i = 0; i < _nblocks; i++) {
        blocks[i].used = 0;
    }
    page_tables = dlist_create();
}

void pager_create(pid_t pid)
{
    PageTable *pt = (PageTable*) malloc(sizeof(PageTable));
    pt->pid = pid;
    pt->pages = dlist_create();
    dlist_push_right(page_tables, pt);
}

void *pager_extend(pid_t pid)
{
    int block_no = get_new_block();

    //there is no block available anymore
    if(block_no == -1) {
        return NULL;
    }

    PageTable *pt = find_page_table(pid); 
    PageInfo *page = (PageInfo*) malloc(sizeof(PageInfo));
    page->isvalid = 0;
    page->vaddr = UVM_BASEADDR + pt->pages->count * _page_size;
    page->block_number = block_no;
    dlist_push_right(pt->pages, page);

    blocks[block_no].page = page;

    return (void*)page->vaddr;
}

int second_chance() {
    int frame_to_swap = -1;
    while(frame_to_swap == -1) {
        if(frame_table[_second_chance_index].accessed == 0) {
            frame_to_swap = _second_chance_index;
        } else {
            frame_table[_second_chance_index].accessed = 0;
        }
        _second_chance_index = (_second_chance_index + 1) % _nframes;
    }

    if(frame_to_swap == 0) {
        for(int i = 0; i < _nframes; i++) {
            PageInfo *page = frame_table[i].page;
            page->prot = PROT_NONE;
            mmu_chprot(frame_table[i].pid, (void*)page->vaddr, page->prot);
        }
    }

    return frame_to_swap;
}

void pager_fault(pid_t pid, void *vaddr)
{
    PageTable *pt = find_page_table(pid); 
    PageInfo *page = get_page(pt, (intptr_t)vaddr); 

    if(page->isvalid == 1) {
        page->prot = PROT_READ | PROT_WRITE;
        mmu_chprot(pid, vaddr, page->prot);
        frame_table[page->frame_number].accessed = 1;
        frame_table[page->frame_number].dirty = 1;
    } else if(page->isvalid == 0) {
        int frame = get_new_frame();

        //there is no frame available
        if(frame == -1) {
            frame = second_chance();
            PageInfo *removed_page = frame_table[frame].page;
            removed_page->isvalid = 0;
            mmu_nonresident(frame_table[frame].pid, (void*)removed_page->vaddr); 
            if(frame_table[frame].dirty == 1) {
                blocks[removed_page->block_number].used = 1;
                mmu_disk_write(frame, removed_page->block_number);
            }
        }
        frame_table[frame].pid = pid;
        frame_table[frame].page = page;
        frame_table[frame].accessed = 1;
        frame_table[frame].dirty = 0;

        page->isvalid = 1;
        page->frame_number = frame;
        page->prot = PROT_READ;


        if(blocks[page->block_number].used == 1) {
            mmu_disk_read(page->block_number, frame);
        } else {
            mmu_zero_fill(frame);
        }
        mmu_resident(pid, vaddr, page->frame_number, page->prot);
    } else {
        printf("missing case");
    }
}

int pager_syslog(pid_t pid, void *addr, size_t len)
{

    PageTable *pt = find_page_table(pid); 
    char *buf = (char*) malloc(len + 1);

    for (int i = 0, m = 0; i < len; i++) {
        PageInfo *page = get_page(pt, (intptr_t)addr); 
        int frame = page->frame_number;

        //TODO: must check page boundaries and return 1 in case it is out of the page
        buf[m++] = pmem[frame * _page_size + i];
    }
    for(int i = 0; i < len; i++) { // len é o número de bytes a imprimir
        printf("%02x", (unsigned)buf[i]); // buf contém os dados a serem impressos
    }
    if(len > 0) printf("\n");
    return 0;
}

void pager_destroy(pid_t pid)
{
    //TODO: must destroy all allocated resourses to that page
}
///////////////////////////////////////////////////


struct dlist *dlist_create(void) /* {{{ */
{
	struct dlist *dl = malloc(sizeof(struct dlist));
	assert(dl);
	dl->head = NULL;
	dl->tail = NULL;
	dl->count = 0;
	return dl;
} /* }}} */

void dlist_destroy(struct dlist *dl, dlist_data_func cb) /* {{{ */
{
	while(!dlist_empty(dl)) {
		void *data = dlist_pop_left(dl);
		if(cb) cb(data);
	}
	free(dl);
} /* }}} */

void *dlist_pop_left(struct dlist *dl) /* {{{ */
{
	void *data;
	struct dnode *node;

	if(dlist_empty(dl)) return NULL;

	node = dl->head;

	dl->head = node->next;
	if(dl->head == NULL) dl->tail = NULL;
	if(node->next) node->next->prev = NULL;

	data = node->data;
	free(node);

	dl->count--;
	assert(dl->count >= 0);
	return data;
} /* }}} */

void *dlist_pop_right(struct dlist *dl) /* {{{ */
{
	void *data;
	struct dnode *node;

	if(dlist_empty(dl)) return NULL;

	node = dl->tail;

	dl->tail = node->prev;
	if(dl->tail == NULL) dl->head = NULL;
	if(node->prev) node->prev->next = NULL;

	data = node->data;
	free(node);

	dl->count--;
	assert(dl->count >= 0);
	return data;
} /* }}} */

void *dlist_push_right(struct dlist *dl, void *data) /* {{{ */
{
	struct dnode *node = malloc(sizeof(struct dnode));
	assert(node);

	node->data = data;
	node->prev = dl->tail;
	node->next = NULL;

	if(dl->tail) dl->tail->next = node;
	dl->tail = node;

	if(dl->head == NULL) dl->head = node;

	dl->count++;
	return data;
} /* }}} */

void *dlist_find_remove(struct dlist *dl, void *data, /* {{{ */
		dlist_cmp_func cmp, void *user_data)
{
	struct dnode *curr;
	for(curr = dl->head; curr; curr = curr->next) {
		if(!curr->data) continue;
		if(cmp(curr->data, data, user_data)) continue;
		void *ptr = curr->data;
		if(dl->head == curr) dl->head = curr->next;
		if(dl->tail == curr) dl->tail = curr->prev;
		if(curr->prev) curr->prev->next = curr->next;
		if(curr->next) curr->next->prev = curr->prev;
		dl->count--;
		free(curr);
		return ptr;
	}
	return NULL;
} /* }}} */

int dlist_empty(struct dlist *dl) /* {{{ */
{
	if(dl->head == NULL) {
		assert(dl->tail == NULL);
		assert(dl->count == 0);
		return 1;
	} else {
		assert(dl->tail != NULL);
		assert(dl->count > 0);
		return 0;
	}
} /* }}} */

void * dlist_get_index(const struct dlist *dl, int idx) /* {{{ */
{
	struct dnode *curr;
	if(idx >= 0) {
		curr = dl->head;
		while(curr && idx--) curr = curr->next;
	} else {
		curr = dl->tail;
		while(curr && ++idx) curr = curr->prev;
	}
	if(!curr) return NULL;
	return curr->data;
} /* }}} */

void dlist_set_index(struct dlist *dl, int idx, void *data) /* {{{ */
{
	struct dnode *curr;
	if(idx >= 0) {
		curr = dl->head;
		while(curr && idx--) curr = curr->next;
	} else {
		curr = dl->tail;
		while(curr && ++idx) curr = curr->prev;
	}
	if(!curr) return;
	curr->data = data;
} /* }}} */
