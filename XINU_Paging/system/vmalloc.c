/* vmalloc.c - vmalloc */

#include <xinu.h>
#include <math.h>
#ifdef DEBUG
#define DEBUG_VMALLOC
#endif
/*------------------------------------------------------------------------
 *  vmalloc  -  allocates Virtual Memory
 *------------------------------------------------------------------------
 */
char* vmalloc (uint32 nbytes) {
    intmask mask = disable();
    uint32 PDBR =  read_cr3();
    /* return paging operation to system page table */
    write_cr3(sys_PDBR);

    /* Initialize point to PD Table*/
    uint32* PDE_addr = (uint32*)proctab[currpid].PDBR;

    /* Determine the number of pages for the allocation */
    uint32 pages = nbytes / PAGE_SIZE;
#ifdef DEBUG_VMALLOC
    kprintf("Allocating Pages : %d \n", pages);
    kprintf("Base Register : 0x%x \n", proctab[currpid].PDBR);
#endif
    uint32 i,j;
    int freePages = 0;
    uint32 startPTE = 0;
    uint32 PDE_address = 0;
    /* Determine if a Page table already exists that has additional PTE's for page allocation */
    for (i = 0; i < (PAGE_SIZE / PTDPTESize); ++i) {
        pd_t* PDE = (pd_t*)(proctab[currpid].PDBR+(i*PTDPTESize));
        if(PDE->pd_write == 1){
            //Page table exists
            uint32 address = 0;
            address += PDE->pd_base << 12;
            /* Check Page Table for free PTE's */
            freePages = 0;
            for (j = 0; j < (PAGE_SIZE / PTDPTESize); j++) {
                pt_t* pte = (pt_t*)(address+(j*PTDPTESize));
                if(!pte->pt_write) {
                    freePages++;
                } else {
                    freePages = 0;
                }
                if(freePages == pages) {
                    startPTE = (address+((j-pages + 1)*PTDPTESize));
                    PDE_address = ((uint32)PDE_addr+(i*PTDPTESize));
                    break;
                }
            }
            if(freePages == pages) {
                break;
            }
        } else {
            //we have searched all PDE's and Page Tables for empty PTE's
            //Create a new PDE and Page table for the new PTE's
            PDE->pd_pres   = 1; PDE->pd_write = 1;
            PDE->pd_user   = 0; PDE->pd_pwt   = 0;
            PDE->pd_pcd    = 0; PDE->pd_acc   = 0;
            PDE->pd_mbz    = 0; PDE->pd_fmb   = 0;
            PDE->pd_global = 0; PDE->pd_avail = 0;
            uint32* page_table_address = get_new_page_pt();
            PDE->pd_base = ((uint32) page_table_address) >> 12;
            freePages = pages;
            uint32 address = 0;
            address += PDE->pd_base << 12;
            startPTE =  address;
            PDE_address =  ((uint32)PDE_addr+(i*PTDPTESize));
#ifdef DEBUG_VMALLOC
            kprintf("Created new PDE entry at i = %d \n",i );
#endif
            break;
        }
    }

    //Ensure a PTE was found for requested Malloc else return ERROR
    if(freePages != pages) {
        //unable to allocate memory
        write_cr3(PDBR);
        restore(mask);
        return SYSERR;
    }
#ifdef DEBUG_VMALLOC
    pde_info((pd_t*)PDE_address);
#endif
    /* Configure PTE's by setting valid bit */
    /* Lazy allocator will allocate memory when requested */
    for (i = 0; i < pages; ++i) {
        pt_t* PTE = (pt_t*)(startPTE+(i*PTDPTESize));
        PTE->pt_pres   = 0; PTE->pt_write = 1;
        PTE->pt_user   = 0; PTE->pt_pwt   = 0;
        PTE->pt_pcd    = 0; PTE->pt_acc   = 0;
        PTE->pt_dirty  = 0; PTE->pt_mbz   = 0;
        PTE->pt_global = 0; PTE->pt_avail = 0;
        PTE->pt_base = 0;
#ifdef DEBUG_VMALLOC
   // pte_info(PTE);
#endif
    }
    /* create virtual address to this memory location */
    virt_addr_t virtaddr;
    virtaddr.pd_offset = PD_MASK & PDE_address;
    virtaddr.pt_offset = PT_MASK & startPTE;
    virtaddr.pg_offset = 0;
#ifdef DEBUG_VMALLOC
    kprintf("Virtual Address : %x %x %x \n", virtaddr.pd_offset, virtaddr.pt_offset, virtaddr.pg_offset);
    kprintf("Virtual Address Char : %x \n", (char*)(uint32*)virt_addr_t2uint32(&virtaddr));
    print_page_directory_only(currpid);
#endif

    /* return paging operation to current processes table */
    char* result = (char*)(uint32*)virt_addr_t2uint32(&virtaddr);
    write_cr3(PDBR);
    restore(mask);
    return result;
}
