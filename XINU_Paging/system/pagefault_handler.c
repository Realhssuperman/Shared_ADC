/* pagefault_handler.c - pagefault_handler */
#include <xinu.h>
#ifdef DEBUG
    #define DEBUG_HANDLER
#endif
syscall pagefault_handler() {
#ifdef DEBUG_HANDLER
    kprintf("PAGE FAULT \n");
#endif
    uint32 userPBDR = read_cr3();
    write_cr3(sys_PDBR);
    unsigned long linear_address = read_cr2();
    virt_addr_t address;
    address.pd_offset = (PD_MASK_A & linear_address) >> 20;
    address.pt_offset = (PT_MASK_A & linear_address) >> 10;
    address.pg_offset = PG_MASK_A & linear_address;

#ifdef DEBUG_HANDLER
    kprintf("Linear Address = 0x%x \n", linear_address);
    kprintf("Virtual Address = 0x%x %x %x\n", address.pd_offset , address.pt_offset, address.pg_offset);
#endif
    /* Acquire Page Directory Entry and Page Table Entry*/
    pd_t *pde = (pd_t *)(userPBDR + address.pd_offset);
    pt_t *pte = (pt_t *)((uint32)(pde->pd_base << 12) + (uint32)address.pt_offset);
#ifdef DEBUG_HANDLER
    pde_info(pde);
    pte_info(pte);
#endif
    if(!pte->pt_pres && !pte->pt_avail && pte->pt_write) {
        //!present and !disk and Valid
        if(free_ffs_pages() != 0 && allocated_virtual_pages(currpid) < (MAX_PT_SIZE*MAX_PT_SIZE)) {
            pte->pt_pres = 1;
            uint32* page_address = get_new_page_vmem();
            pte->pt_base = ((uint32) page_address) >> 12;

        } else {
            //SWAP required
            //Eviction Required
        }
    } else if(!pte->pt_write && !pte->pt_pres){
        //Segmentation Fault - current process must die
        kprintf("P%d:: SEGMENTATION_FAULT\n", currpid);
        kill(currpid);
        //This function will not return
    } else {
        //Swap from Disk
    }
#ifdef DEBUG_HANDLER
    kprintf("Updated \n");
    pde_info(pde);
    pte_info(pte);
#endif
    write_cr3(userPBDR);
    return OK;
}