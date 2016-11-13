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
} FramesTable;

int _nframes, _nblocks, _page_size;
FramesTable frames_table[1000];
PageTable processes_list[100];
int _block_cnt = 0;
int _nprocesses = 0;

/****************************************************************************
 * external functions
 ***************************************************************************/
int get_new_frame() {
    int i;
    for(i = 0; i < _nframes; i++) {
        //printf("get_new_page: i - %d \t pid - %d\n",i, frames_table[i].pid);
        if(frames_table[i].pid == -1) return i;
    }
    return -1;
}

PageTable* find_page_table(pid_t pid) {
    int i;
    //printf("find pid: %d\n", pid);
    //printf("first pid: %d\n", processes_list[0].pid);
    //printf("comparison: %d\n", (processes_list[0].pid == pid) ? 1:0);
    for(i = 0; i < _nprocesses; i++) {
        if(processes_list[i].pid == pid) return &processes_list[i];
    }
    printf("error: Pid not found\n");
    exit(-1);
}

void pager_init(int nframes, int nblocks)
{
    _nframes = nframes;
    _nblocks = nblocks;
    _page_size = sysconf(_SC_PAGESIZE);
    int i;
    for(i = 0; i < _nframes; i++) {
        frames_table[i].pid = -1;
    }

    //frames_table = malloc(_nframes * sizeof(FramesTable));
}

void pager_create(pid_t pid)
{
    //printf("pager_Create pid: %d\n", pid);
    //PageTable *pt = (PageTable*) malloc(sizeof(PageTable));
    PageTable *pt = &processes_list[_nprocesses];
    pt->pid = pid;
    pt->npages = 0;
    pt->pages = (PageInfo*) malloc(_nframes * sizeof(PageInfo));
    int i;
    for(i = 0; i < _nframes; i++) {
        pt->pages[i].isvalid = 0;
    }
    _nprocesses++;
}

void *pager_extend(pid_t pid)
{
	if(_block_cnt == _nblocks) {
        return NULL;
    }

    PageTable *pt = find_page_table(pid); 
    int npages = pt->npages;
    intptr_t vaddr = UVM_BASEADDR + npages * _page_size;

    PageInfo *page = &pt->pages[npages];
    page->isvalid = 0;
    page->vaddr = vaddr;
    page->block_number = _block_cnt;

    pt->npages++;
    
    _block_cnt++;
    return (void*)vaddr;
}

PageInfo* get_page(PageTable *pt, intptr_t vaddr) {
    PageInfo *pages = pt->pages;
    int i;
    for(i=0; i < pt->npages; i++) {
        if(vaddr == pages[i].vaddr) return &pages[i];
    }
    printf("error in get_page_index: page was not found");
    exit(-1);
}

void pager_fault(pid_t pid, void *vaddr)
{
    PageTable *pt = find_page_table(pid); 
    PageInfo *page = get_page(pt, (intptr_t)vaddr); 
    
    if(page->isvalid == 0) {
        int frame = get_new_frame();
    
        //there is no frame available
        if(frame == -1) {
            //run second change algorithm
            //send one frame to disk
            //call mmu_nonresident
            //update variable frame
        }
        frames_table[frame].pid = pid;
        frames_table[frame].page = page;
        frames_table[frame].accessed = 1;
        page->frame_number = frame;
        page->prot = PROT_READ;
        page->isvalid = 1;

        mmu_zero_fill(frame);
        mmu_resident(pid, vaddr, page->frame_number, page->prot);
    } else if(page->prot < PROT_WRITE) {

        page->prot = PROT_READ | PROT_WRITE;
        mmu_chprot(pid, vaddr, page->prot);
    }
}

int pager_syslog(pid_t pid, void *addr, size_t len)
{

    PageTable *pt = find_page_table(pid); 
    char *buf = (char*) malloc(len + 1);

    for (int i = 0, m = 0; i < len; i++) {
        PageInfo *page = get_page(pt, (intptr_t)addr); 
        int frame = page->frame_number;
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
}
