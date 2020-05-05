/* vcreate.c - vcreate */

#include <xinu.h>
#ifdef DEBUG
    #define DEBUG_VCREATE
#endif
/*------------------------------------------------------------------------
 *  vcreate  -  creates a user process
 *------------------------------------------------------------------------
 */
pid32 vcreate (void *funcaddr, uint32 ssize, pri16 priority, char *name,
               uint32 nargs, ...) {
    uint32		savsp, *pushsp;
    intmask 	mask;    	/* Interrupt mask		*/
    pid32		pid;		/* Stores new process id	*/
    struct	procent	*prptr;		/* Pointer to proc. table entry */
    int32		i;
    uint32		*a;		/* Points to list of args	*/
    uint32		*saddr;		/* Stack address		*/

    mask = disable();
    uint32 PDBR =  read_cr3();
    write_cr3(sys_PDBR);
    if (ssize < MINSTK)
        ssize = MINSTK;
    ssize = (uint32) roundmb(ssize);
    if ( (priority < 1) || ((pid=newpid()) == SYSERR) ||
         ((saddr = (uint32 *)getstk(ssize)) == (uint32 *)SYSERR) ) {
        write_cr3(PDBR);
        restore(mask);
        return SYSERR;
    }

    prcount++;
    prptr = &proctab[pid];

    /* Initialize process table entry for new process */
    prptr->prstate = PR_SUSP;	/* Initial state is suspended	*/
    prptr->prprio = priority;
    prptr->prstkbase = (char *)saddr;
    prptr->prstklen = ssize;
    prptr->prname[PNMLEN-1] = NULLCH;
    for (i=0 ; i<PNMLEN-1 && (prptr->prname[i]=name[i])!=NULLCH; i++)
        ;
    prptr->prsem = -1;
    prptr->prparent = (pid32)getpid();
    prptr->prhasmsg = FALSE;

    /* Set up stdin, stdout, and stderr descriptors for the shell	*/
    prptr->prdesc[0] = CONSOLE;
    prptr->prdesc[1] = CONSOLE;
    prptr->prdesc[2] = CONSOLE;
    /* Initialize stack as if the process was called		*/

    *saddr = STACKMAGIC;
    savsp = (uint32)saddr;

    /* Push arguments */
    a = (uint32 *)(&nargs + 1);	/* Start of args		*/
    a += nargs -1;			/* Last argument		*/
    for ( ; nargs > 0 ; nargs--)	/* Machine dependent; copy args	*/
        *--saddr = *a--;	/* onto created process's stack	*/
    *--saddr = (long)INITRET;	/* Push on return address	*/

    /* The following entries on the stack must match what ctxsw	*/
    /*   expects a saved process state to contain: ret address,	*/
    /*   ebp, interrupt mask, flags, registers, and an old SP	*/

    *--saddr = (long)funcaddr;	/* Make the stack look like it's*/
    /*   half-way through a call to	*/
    /*   ctxsw that "returns" to the*/
    /*   new process		*/
    *--saddr = savsp;		/* This will be register ebp	*/
    /*   for process exit		*/
    savsp = (uint32) saddr;		/* Start of frame for ctxsw	*/
    *--saddr = 0x00000200;		/* New process runs with	*/
    /*   interrupts enabled		*/

    /* Basically, the following emulates an x86 "pushal" instruction*/

    *--saddr = 0;			/* %eax */
    *--saddr = 0;			/* %ecx */
    *--saddr = 0;			/* %edx */
    *--saddr = 0;			/* %ebx */
    *--saddr = 0;			/* %esp; value filled in below	*/
    pushsp = saddr;			/* Remember this location	*/
    *--saddr = savsp;		/* %ebp (while finishing ctxsw)	*/
    *--saddr = 0;			/* %esi */
    *--saddr = 0;			/* %edi */
    *pushsp = (unsigned long) (prptr->prstkptr = (char *)saddr);

    /* Initialize Page Directory for this process and map existing Xinu area */

    /* return paging operation to system page table */
    write_cr3(sys_PDBR);

    /* Initialize Kernel PDE and PTE's */
    uint32* PDE_addr = get_new_page_pt();

    proctab[pid].PDBR =  (uint32) PDE_addr;

#ifdef DEBUG_VCREATE
    kprintf("PDBR for Process %d %x \n", pid,proctab[pid].PDBR );
#endif
    //write_cr4(0);
    /* Initialize Static pages that are mapped to Xinu's existing memory map */

    uint32 kernel_space = KERNEL_END - KERNEL_START;
    uint32 pages = kernel_space / (PAGE_SIZE * MAX_PT_SIZE);
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
#ifdef DEBUG_VCREATE
   // print_page_directory(pid);
    print_page_directory_only(pid);
#endif
    /* return paging operation to current processes table */
    write_cr3(PDBR);

    restore(mask);
    return pid;
}