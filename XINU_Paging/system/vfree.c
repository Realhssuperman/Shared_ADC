/* vfree.c - vfree */

#include <xinu.h>

/*------------------------------------------------------------------------
 *  vfree  -  frees Virtual Memory
 *------------------------------------------------------------------------
 */
syscall vfree (char* ptr, uint32 nbytes) {
    write_cr3(sys_PDBR);
    /* Determine PDE and PTE of the start of the memory block */
    virt_addr_t address;
    address.pd_offset = (PD_MASK_A & (uint32)ptr) >> 20;
    address.pt_offset = (PT_MASK_A & (uint32)ptr) >> 10;
    address.pg_offset = PG_MASK_A & (uint32)ptr;

    /* Determine the number of pages for the deallocate */
    uint32 pages = nbytes / PAGE_SIZE;

    /* Remove the specified number of pages from the Page tables */
    uint32 i,j;
    uint32 x;
    for (i = address.pd_offset/PTDPTESize; i < (PAGE_SIZE / PTDPTESize); ++i) {
        pd_t *PDE = (pd_t *) (proctab[currpid].PDBR + (i * PTDPTESize));
        uint32 PTEaddress = 0;
        PTEaddress += PDE->pd_base << 12;
        if(i == address.pd_offset/PTDPTESize) {
            x = address.pt_offset/PTDPTESize;
        } else {
            x = 0;
        }
        for (j = x; j < (PAGE_SIZE / PTDPTESize); ++j) {
            pt_t* PTE = (pt_t*)(PTEaddress+(j*PTDPTESize));
            /*remove page PTE points to */
            if(PTE->pt_pres) {
                free_page_vmem((uint32*)(PTE->pt_base >> 12));
            } else if (PTE->pt_avail) {
                free_page_swap((uint32*)(PTE->pt_base >> 12));
            }

            /*set PTE as open. */
            PTE->pt_pres   = 0; PTE->pt_write = 0;
            PTE->pt_user   = 0; PTE->pt_pwt   = 0;
            PTE->pt_pcd    = 0; PTE->pt_acc   = 0;
            PTE->pt_dirty  = 0; PTE->pt_mbz   = 0;
            PTE->pt_global = 0; PTE->pt_avail = 0;
            PTE->pt_base = 0;
            pages--;
            if(pages == 0) {
                write_cr3(proctab[currpid].PDBR);
                return OK;
            }
        }
    }

    write_cr3(proctab[currpid].PDBR);
    return SYSERR;
}