//
// Created by joshmosh on 11/25/2019.
//
#include <xinu.h>
#ifdef DEBUG
    #define DEBUG_STARTUP
#endif
    /* Memory base addresses for each section of virtual memory */
    uint32 *page_table_addr;
    uint32 *ffs_addr;
    uint32 *swap_space_addr;

    /* Memory bitmaps for each section of virtual memory */

    uint32	page_table_memmap[MAX_PT_SIZE/BYTE_SIZE];	/* Head of free page table memory bitmap */
    uint32	v_memmap[MAX_FFS_SIZE/BYTE_SIZE];           /* Head of free virtual memory 	bitmap   */
    uint32	swap_memmap[MAX_SWAP_SIZE/BYTE_SIZE];	    /* Head of free swap memory bitmap       */

    /* SYSTEM PAGE DIRECTORY BASE REGISTER */
    uint32 sys_PDBR;

    /* number of kernel pages */
    uint32 kernel_page_num ;

/* Configure memory System for Paging */
void init_paging() {
    int i;

    //Allocate Space for Page Tables
    page_table_addr = (uint32 *) PAGE_TABLE_START;
    //Allocate Space for FFS
    ffs_addr = (uint32 *) ((uint32) page_table_addr + PAGE_TABLE_SIZE);
    //Allocate Space for Swap Space
    swap_space_addr = (uint32 *) ((uint32) ffs_addr + FFS_SIZE);
#ifdef DEBUG_STARTUP
    kprintf("Page_Table_Space Size : %d Start Address : 0x%x \n", PAGE_TABLE_SIZE, page_table_addr);
    kprintf("FFS_Space        Size : %d Start Address : 0x%x \n", FFS_SIZE, ffs_addr);
    kprintf("Swap_Space       Size : %d Start Address : 0x%x \n",SWAP_SIZE,swap_space_addr);
#endif
    /* Initialize bitmaps */
    for (i = 0; i < MAX_PT_SIZE/BYTE_SIZE; ++i) {
        page_table_memmap[i] = 0;
    }
    for (i = 0; i < MAX_FFS_SIZE/BYTE_SIZE; ++i) {
        v_memmap[i] = 0;
    }
    for (i = 0; i < MAX_SWAP_SIZE/BYTE_SIZE; ++i) {
        swap_memmap[i] = 0;
    }

    /* Initialize Kernel PDE and PTE's */

    uint32* PDE_addr = get_new_page_pt();

    /* Store Kernel PDBR For System Processes */
    sys_PDBR = (uint32) PDE_addr;
    proctab[currpid].PDBR =  (uint32) PDE_addr;
    write_cr3(proctab[currpid].PDBR);
    //write_cr4(0);
    /* Initialize Static pages that are mapped to Xinu's existing memory map */

    uint32 kernel_space = KERNEL_END - KERNEL_START;
    uint32 pages = kernel_space / (PAGE_SIZE * MAX_PT_SIZE);
    kernel_page_num = pages;
    uint32 other_total_space =  (((uint32) swap_space_addr + SWAP_SIZE))  / (PAGE_SIZE * MAX_PT_SIZE);
    pages = other_total_space;
#ifdef DEBUG_STARTUP
    kprintf("Kernel Space Size : %d bytes Pages : %d \n", kernel_space , pages);
#endif
    uint32 j;
    pd_t PDE;
    uint32 physical_addr = KERNEL_START;
    uint32 physical_addr_pte = KERNEL_START;
    for (i = 0; i < pages; ++i) {
        PDE.pd_pres   = 1; PDE.pd_write = 1;
        PDE.pd_user   = 0; PDE.pd_pwt   = 0;
        PDE.pd_pcd    = 0; PDE.pd_acc   = 0;
        PDE.pd_mbz    = 0; PDE.pd_fmb   = 0;
        PDE.pd_global = 0; PDE.pd_avail = 0;
        uint32* page_table_address = get_new_page_pt();
        PDE.pd_base = ((uint32) page_table_address) >> 12;
        PDE_addr[i] = pde2uint32(&PDE);

        /* Create PTE's that map these page tables to XINU's existing memory space */
        /* 1 PTE -> 32 bits 4 bytes => 1024 PTE per page */
        pt_t PTE;

        for (j = 0; j < (PAGE_SIZE / PTDPTESize); j++) {
            /* Create PTE */
            PTE.pt_pres   = 1; PTE.pt_write = 1;
            PTE.pt_user   = 0; PTE.pt_pwt   = 0;
            PTE.pt_pcd    = 0; PTE.pt_acc   = 0;
            PTE.pt_dirty  = 0; PTE.pt_mbz   = 0;
            PTE.pt_global = 0; PTE.pt_avail = 0;
            PTE.pt_base = physical_addr_pte >> 12;
            page_table_address[j] = pte2uint32(&PTE);
            physical_addr_pte = physical_addr_pte + PAGE_SIZE;
        }
    }

#ifdef DEBUG_STARTUP
    kprintf("Kernel PDE_addr : 0x%x \n", PDE_addr);
    kprintf("Free Virtual Memory Pages : %d\n", free_ffs_pages());
    //print_page_directory();
#endif

    /* Page Directory has been created with Directory Entries that point to newly allocated Page tables */


}

/* returns base address of newly allocated of size PAGE_SIZE */
uint32* get_new_page(uint32* map, uint32* start_addr, uint32 size) {
    uint32 i;
    for (i = 0; i < size; ++i) {
        if(!TestBit(map,i)) {
            //page is unallocated
            SetBit(map,i);
            return (start_addr + (PAGE_SIZE*i));
        }
    }
    return SYSERR;
}
uint32* get_new_page_pt() {
    return get_new_page((uint32 *) &page_table_memmap, page_table_addr, MAX_PT_SIZE);
}
uint32* get_new_page_vmem() {
    return get_new_page((uint32 *) &v_memmap, ffs_addr, MAX_FFS_SIZE);
}
uint32* get_new_page_swap() {
    return get_new_page((uint32 *) &swap_memmap, swap_space_addr, MAX_SWAP_SIZE);
}

void free_page_pt(uint32* page_addr){
    free_page((uint32 *) &page_table_memmap, page_table_addr, page_addr);
}

void free_page_vmem(uint32* page_addr){
    free_page((uint32 *) &v_memmap, ffs_addr, page_addr);
}

void free_page_swap(uint32* page_addr){
    free_page((uint32 *) &swap_memmap, swap_space_addr, page_addr);
}

syscall free_page(uint32* map, const uint32* start_addr, const uint32* page_addr) {
    unsigned int page_num = ((uint32)page_addr - (uint32)start_addr) / PAGE_SIZE;
    page_num = page_num / PTDPTESize;
    //page is unallocated
#ifdef DEBUG_STARTUP
        kprintf("Page is already unallocated %d\n", page_num);
#endif
    ClearBit(map,page_num);
    return OK;
}

/* debug functions */
uint32 free_ffs_pages(){
    uint32 free = 0;
    uint32 i;
    for (i = 0; i < MAX_FFS_SIZE; ++i) {
        if(!TestBit(v_memmap,i)) {
            free++;
        }
    }
    return free;
}

uint32 free_swap_pages(){
    uint32 free = 0;
    uint32 i;
    for (i = 0; i < MAX_SWAP_SIZE; ++i) {
        if(!TestBit(swap_memmap,i)) {
            free++;
        }
    }
    return free;
}

uint32 allocated_virtual_pages(pid32 pid){
    intmask mask = disable();
    uint32 PDBR =  read_cr3();
    write_cr3(sys_PDBR);
    uint32 used = 0;
    uint32 i, j;
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); i++) {
        pd_t *pde = (pd_t *) (proctab[pid].PDBR + (i * PTDPTESize));
        if(pde->pd_write && pde->pd_pres) {
            //check PTE's
            uint32 address = 0;
            address += pde->pd_base << 12;
            for (j = 0; j < (PAGE_SIZE / PTDPTESize); j++) {
                pt_t* pte = (pt_t*)(address+(j*PTDPTESize));
                if(pte->pt_write) {
                    used++;
                }
            }
        }
    }
    write_cr3(PDBR);
    restore(mask);
    return used;
}

uint32 used_ffs_frames(pid32 pid) {
    intmask mask = disable();
    uint32 PDBR =  read_cr3();
    write_cr3(sys_PDBR);
    uint32 used = 0;
    uint32 i, j;
    for (i = kernel_page_num; i < (PAGE_SIZE / PTDPTESize); i++) {
        pd_t *pde = (pd_t *) (proctab[pid].PDBR + (i * PTDPTESize));
        if(pde->pd_write && pde->pd_pres) {
            //check PTE's
            uint32 address = 0;
            address += pde->pd_base << 12;
            for (j = 0; j < (PAGE_SIZE / PTDPTESize); j++) {
                pt_t* pte = (pt_t*)(address+(j*PTDPTESize));
                if(pte->pt_write && pte->pt_pres && !pte->pt_avail) {
                    used++;
                }
            }
        }
    }
    write_cr3(PDBR);
    restore(mask);
    return used;
}

uint32 used_swap_frames(pid32 pid) {
    intmask mask = disable();
    uint32 PDBR =  read_cr3();
    write_cr3(sys_PDBR);
    uint32 used = 0;
    uint32 i, j;
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); i++) {
        pd_t *pde = (pd_t *) (proctab[pid].PDBR + (i * PTDPTESize));
        if(pde->pd_write) {
            //check PTE's
            uint32 address = 0;
            address += pde->pd_base << 12;
            for (j = 0; j < (PAGE_SIZE / PTDPTESize); j++) {
                pt_t* pte = (pt_t*)(address+(j*PTDPTESize));
                //is PTE valid but not present and on DISK
                if(pte->pt_write && !pte->pt_pres && pte->pt_avail) {
                    used++;
                }
            }
        }
    }
    write_cr3(PDBR);
    restore(mask);
    return used;
}

uint32 pde2uint32(pd_t* pde) {
    uint32 result = 0;
    result += pde->pd_base << 12;
    result += pde->pd_avail << 9;
    result += pde->pd_global << 8;
    result += pde->pd_fmb << 7;
    result += pde->pd_mbz << 6;
    result += pde->pd_acc << 5;
    result += pde->pd_pcd << 4;
    result += pde->pd_pwt << 3;
    result += pde->pd_user << 2;
    result += pde->pd_write << 1;
    result += pde->pd_pres;
    return result;
}
uint32 pte2uint32(pt_t* pte) {
    uint32 result = 0;
    result += pte->pt_base << 12;
    result += pte->pt_avail << 9;
    result += pte->pt_global << 8;
    result += pte->pt_mbz << 7;
    result += pte->pt_dirty << 6;
    result += pte->pt_acc << 5;
    result += pte->pt_pcd << 4;
    result += pte->pt_pwt << 3;
    result += pte->pt_user << 2;
    result += pte->pt_write << 1;
    result += pte->pt_pres;
    return result;
}

uint32 virt_addr_t2uint32(virt_addr_t* virtAddr) {
    uint32 result = 0;
    result += virtAddr->pd_offset << 20;
    result += virtAddr->pt_offset << 10;
    result += virtAddr->pg_offset;
    return result;
}
/* prints info about pde */
void pde_info(pd_t* pde) {
    kprintf("PDE Physical Location : 0x%08x ",pde);
    kprintf("PDE : %032b ", pde2uint32(pde));
    kprintf("Base: %020b 0x%05x U: %03b G: %b S: %b R: %b A: %b"
            " CD: %b WT: %b U/S: %b R/W: %b P: %b \n"
            ,pde->pd_base, pde->pd_base, pde->pd_avail, pde->pd_global, pde->pd_fmb, pde->pd_mbz, pde->pd_acc,
            pde->pd_pcd, pde->pd_pwt, pde->pd_user, pde->pd_write, pde->pd_pres);
}

/* prints info about pte */
void pte_info(pt_t* pte) {
    kprintf("PTE Physical Location : 0x%08x ",pte);
    kprintf("PTE : %032b ", pte2uint32(pte));
    kprintf("Base: %020b 0x%05x U: %03b G: %b S: %b R: %b A: %b"
            " CD: %b WT: %b U/S: %b R/W: %b P: %b \n"
            ,pte->pt_base, pte->pt_base, pte->pt_avail, pte->pt_global, pte->pt_mbz, pte->pt_dirty, pte->pt_acc,
            pte->pt_pcd, pte->pt_pwt, pte->pt_user, pte->pt_write, pte->pt_pres);
}

/* prints page table of PDE */
void print_page_table(uint32 page_table_base_address) {
    int i;
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); i++) {
        pt_t* pte = (pt_t*)(page_table_base_address+(i*PTDPTESize));
            pte_info(pte);
    }
}

/* prints page directory of current process */
void print_page_directory(pid32 pid) {
    int i;
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); i++) {
        pd_t* pde = (pd_t*)(proctab[pid].PDBR+(i*PTDPTESize));
        if(pde->pd_base != 0) {
            pde_info(pde);
            uint32 address = 0;
            address += pde->pd_base << 12;
            print_page_table(address);
        }
    }
};

void print_page_directory_only(pid32 pid) {
    int i;
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); i++) {
        pd_t* pde = (pd_t*)(proctab[pid].PDBR+(i*PTDPTESize));
        if(pde->pd_base != 0) {
            pde_info(pde);
        }
    }
};

