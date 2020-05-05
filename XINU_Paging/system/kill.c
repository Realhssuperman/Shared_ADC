/* kill.c - kill */

#include <xinu.h>
#ifdef DEBUG
    #define DEBUG_KILL
#endif
/*------------------------------------------------------------------------
 *  kill  -  Kill a process and remove it from the system
 *------------------------------------------------------------------------
 */
syscall kill(
        pid32 pid        /* ID of process to kill	*/
) {
    intmask mask;            /* Saved interrupt mask		*/
    struct procent *prptr;        /* Ptr to process's table entry	*/
    int32 i;            /* Index into descriptors	*/

    mask = disable();
    if (isbadpid(pid) || (pid == NULLPROC)
        || ((prptr = &proctab[pid])->prstate) == PR_FREE) {
        restore(mask);
        return SYSERR;
    }

    if (--prcount <= 1) {        /* Last user process completes	*/
        xdone();
    }

    send(prptr->prparent, pid);
    for (i = 0; i < 3; i++) {
        close(prptr->prdesc[i]);
    }
    freestk(prptr->prstkbase, prptr->prstklen);
    if (prptr->PDBR != sys_PDBR) {
        /* Free all Virtual Memory */
        uint32 PDBR = read_cr3();
        write_cr3(sys_PDBR);
        uint32 ffspages = used_ffs_frames(pid);
        uint32 swap_pages = used_swap_frames(pid);

        /* Free all kernel, vmem, and swap pages */

        /* Determine the number of pages for the deallocate */
        uint32 pages = ffspages + swap_pages;
#ifdef DEBUG_KILL
        //Ensure directory is deleted correctly.
        kprintf("Total Pages to delete : %d", pages);
        kprintf("FSS pages : %d kernel pages %d Swap pages : %d \n", ffspages,
                kernel_page_num * (PAGE_SIZE / PTDPTESize), swap_pages);
        kprintf("P%d:: virtual pages = %d\n", pid, allocated_virtual_pages(pid));
        kprintf("P%d:: FFS frames = %d\n", pid, used_ffs_frames(pid));
        kprintf("P%d:: SWAP frames = %d\n", pid, used_swap_frames(pid));
#endif
        /* Remove the specified number of pages from the Page tables */
        uint32 k, j;
        for (k = 0; k < (PAGE_SIZE / PTDPTESize); ++k) {
            pd_t *PDE = (pd_t *) (prptr->PDBR + (k * PTDPTESize));
            if (PDE->pd_pres && PDE->pd_write) {
                uint32 PTEaddress = 0;
                PTEaddress += PDE->pd_base << 12;
                for (j = 0; j < (PAGE_SIZE / PTDPTESize); ++j) {
                    pt_t *PTE = (pt_t *) (PTEaddress + (j * PTDPTESize));
                    if (PTE->pt_pres && PTE->pt_write) {
                        if (k < kernel_page_num) {
                            /*kernel page*/
                            //This page does not need to be unallocated as it does take up heap memory in FFS or SWAP
                        } else if (!(PTE->pt_avail)) {
#ifdef DEBUG_KILL
                            kprintf("Removing Vmem page\n");
                            pte_info(PTE);
#endif
                            /*FFS page*/
                            free_page_vmem((uint32 *) (PTE->pt_base << 12));
                        } else {
                            /*swap space page */
                            //This wont work there needs to be a way to determine where a page is
                            // located in the SWAP space so we can delete it
                            // it is not going to be at the location in the PTE->pt_base + offset
                            free_page_swap((uint32 *) (PTE->pt_base << 12));
                        }
                        pages--;
                    }
                    /*set PTE as open. */
                    PTE->pt_pres = 0;
                    PTE->pt_write = 0;
                    PTE->pt_user = 0;
                    PTE->pt_pwt = 0;
                    PTE->pt_pcd = 0;
                    PTE->pt_acc = 0;
                    PTE->pt_dirty = 0;
                    PTE->pt_mbz = 0;
                    PTE->pt_global = 0;
                    PTE->pt_avail = 0;
                    PTE->pt_base = 0;
                }
            }
        }

        /* Free all Page Tables */
        for (k = 0; k < (PAGE_SIZE / PTDPTESize); ++k) {
            pd_t *PDE = (pd_t *) (prptr->PDBR + (k * PTDPTESize));
            if (PDE->pd_pres) {
                free_page_pt((uint32 *) (PDE->pd_base << 12));
            }
            PDE->pd_pres = 0;
            PDE->pd_write = 0;
            PDE->pd_user = 0;
            PDE->pd_pwt = 0;
            PDE->pd_pcd = 0;
            PDE->pd_acc = 0;
            PDE->pd_mbz = 0;
            PDE->pd_fmb = 0;
            PDE->pd_global = 0;
            PDE->pd_avail = 0;
            PDE->pd_base = 0;
        }
#ifdef DEBUG_KILL
        //Ensure directory is deleted correctly.
        kprintf("After Kill \n");
        kprintf("P%d:: virtual pages = %d\n", pid, allocated_virtual_pages(pid));
        kprintf("P%d:: FFS frames = %d\n", pid, used_ffs_frames(pid));
        kprintf("P%d:: SWAP frames = %d\n\n", pid, used_swap_frames(pid));
        print_page_directory_only(pid);
#endif

        /* Free Directory Page */
        free_page_pt((uint32 *) prptr->PDBR);

        prptr->PDBR = sys_PDBR;
#ifdef DEBUG_KILL
        kprintf("Page Directory removed");
#endif
    }

    switch (prptr->prstate) {
        case PR_CURR:
            prptr->prstate = PR_FREE;    /* Suicide */
            resched();

        case PR_SLEEP:
        case PR_RECTIM:
            unsleep(pid);
            prptr->prstate = PR_FREE;
            break;

        case PR_WAIT:
            semtab[prptr->prsem].scount++;
            /* Fall through */

        case PR_READY:
            getitem(pid);        /* Remove from queue */
            /* Fall through */

        default:
            prptr->prstate = PR_FREE;
    }

    restore(mask);
    return OK;
}
