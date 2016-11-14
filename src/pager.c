#include "pager.h"

#include <sys/mman.h>

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mmu.h"

typedef struct {
    int isvalid;
    int frame_number;
    int block_number;
    int prot;
    intptr_t vaddr;
} PageInfo;

typedef struct {
    pid_t pid;
    int npages;
    PageInfo *pages;
} PageTable;

typedef struct {
    pid_t pid;
    PageInfo *page;
    int accessed;
} FrameTable;

int _nframes, _nblocks, _page_size;
FrameTable frame_table[1000];
PageTable processes_list[100];
int _block_cnt = 0;
int _nprocesses = 0;
int _second_chance_index = 0;
int _used_blocks = 0;

/****************************************************************************
 * external functions
 ***************************************************************************/
int get_new_frame() {
    for(int i = 0; i < _nframes; i++) {
        if(frame_table[i].pid == -1) return i;
    }
    return -1;
}

PageTable* find_page_table(pid_t pid) {
    for(int i = 0; i < _nprocesses; i++) {
        if(processes_list[i].pid == pid) return &processes_list[i];
    }
    printf("error: Pid not found\n");
    exit(-1);
}

PageInfo* get_page(PageTable *pt, intptr_t vaddr) {
    PageInfo *pages = pt->pages;
    for(int i=0; i < pt->npages; i++) {
        if(vaddr == pages[i].vaddr) return &pages[i];
    }
    printf("error in get_page_index: page was not found");
    exit(-1);
}

void pager_init(int nframes, int nblocks)
{
    _nframes = nframes;
    _nblocks = nblocks;
    _page_size = sysconf(_SC_PAGESIZE);
    
    for(int i = 0; i < _nframes; i++) {
        frame_table[i].pid = -1;
    }
    //frame_table = malloc(_nframes * sizeof(FrameTable));
}

void pager_create(pid_t pid)
{
    //PageTable *pt = (PageTable*) malloc(sizeof(PageTable));
    PageTable *pt = &processes_list[_nprocesses++];
    pt->pid = pid;
    pt->npages = 0;
    pt->pages = (PageInfo*) malloc(_nframes * sizeof(PageInfo));
    
    for(int i = 0; i < _nframes; i++) {
        pt->pages[i].isvalid = 0;
    }
}

void *pager_extend(pid_t pid)
{
    if(_block_cnt++ == _nblocks) {
        return NULL;
    }

    PageTable *pt = find_page_table(pid); 
    int npages = pt->npages++;

    PageInfo *page = &pt->pages[npages];
    page->isvalid = 0;
    page->vaddr = UVM_BASEADDR + npages * _page_size;
    page->block_number = -1;

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
    return frame_to_swap;
}

void pager_fault(pid_t pid, void *vaddr)
{
    PageTable *pt = find_page_table(pid); 
    PageInfo *page = get_page(pt, (intptr_t)vaddr); 

    if(page->isvalid == 1) {
        page->prot = PROT_READ | PROT_WRITE;
        mmu_chprot(pid, vaddr, page->prot);
    } else {
        int frame = get_new_frame();

        //there is no frame available
        if(frame == -1) {
            frame = second_chance();
            PageInfo *removed_page = frame_table[frame].page;
            removed_page->isvalid = 0;
            removed_page->block_number = _used_blocks++;
            mmu_disk_write(frame, removed_page->block_number);
            mmu_nonresident(frame_table[frame].pid, (void*)removed_page->vaddr); 
        }
        frame_table[frame].pid = pid;
        frame_table[frame].page = page;
        frame_table[frame].accessed = 1;
        page->frame_number = frame;
        page->prot = PROT_READ;
        page->isvalid = 1;

        mmu_zero_fill(frame);
        mmu_resident(pid, vaddr, page->frame_number, page->prot);
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
