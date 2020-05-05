/*  main.c  - main */

#include <xinu.h>

void sync_printf(char *fmt, ...)
{
    intmask mask = disable();
    void *arg = __builtin_apply_args();
    __builtin_apply((void*)kprintf, arg, 100);
    restore(mask);
}

void process_info(pid32 pid){
    sync_printf("P%d:: virtual pages = %d\n", pid, allocated_virtual_pages(pid));
    sync_printf("P%d:: FFS frames = %d\n", pid, used_ffs_frames(pid));
    sync_printf("P%d:: SWAP frames = %d\n\n", pid, used_swap_frames(pid));
}

process empty_process(){
    process_info(currpid);
    return OK;
}

process vmalloc_process(){

    /* testing vmalloc only */

    sync_printf("P%d:: Allocating 8/4/2 pages...\n", currpid);
    char *ptr1 = vmalloc(8 * PAGE_SIZE);
    char *ptr2 = vmalloc(4 * PAGE_SIZE);
    char *ptr3 = vmalloc(2 * PAGE_SIZE);

    sync_printf("P%d:: ptr1=0x%x, ptr2=0x%x, ptr3=0x%x\n", currpid, ptr1, ptr2, ptr3);
    process_info(currpid);

    /* testing deallocation */
    sync_printf("P%d:: Freeing 4 pages @ ptr2...\n", currpid);
    vfree(ptr2, 4 * PAGE_SIZE);

    process_info(currpid);

    /* testing virtual space handling (must be first-fit) */
    sync_printf("P%d:: Allocating 2/4 pages...\n", currpid);
    char *ptr4 = vmalloc(2 * PAGE_SIZE);
    char *ptr5 = vmalloc(4 * PAGE_SIZE);

    sync_printf("P%d:: ptr4=0x%x, ptr5=0x%x, ptr3=0x%x\n", currpid, ptr4, ptr5, ptr3);
    process_info(currpid);

    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    /* testing FFS allocation */
    sync_printf("P%d:: Accessing 1 page @ ptr1...\n", currpid);
    ptr1[0]=0;
    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);
    sync_printf("P%d:: Accessing again 1 page @ ptr1...\n", currpid);
    ptr1[4]=0;
    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);
    sync_printf("P%d:: Accessing 2nd page from ptr3...\n", currpid);
    ptr3[PAGE_SIZE]=0;
    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    /* testing segmentation fault */
    sync_printf("P%d:: Testing segmentation fault...\n", currpid);
    ptr4[2*PAGE_SIZE]=0;

    sync_printf("P%d :: ERROR: process should already be killed!", currpid);

    return OK;
}

process vmalloc_process2(){

    uint32 i = 0;

    /* testing vmalloc only */

    char *ptr1 = vmalloc(80 * PAGE_SIZE);
    char *ptr2 = vmalloc(80 * PAGE_SIZE);
    char *ptr3 = vmalloc(80 * PAGE_SIZE);

    if (ptr1==(char *)SYSERR || ptr2==(char *)SYSERR || ptr3==(char *)SYSERR)
        sync_printf("P%d:: allocation failed!\n");

    /* testing FFS allocation */
    for (i=0; i<40; i++){
        ptr1[i*PAGE_SIZE]=i;
        ptr1[i*PAGE_SIZE+1]=i;
    }

    for (i=0; i<40; i++){
        if (ptr1[i*PAGE_SIZE]!=i || ptr1[i*PAGE_SIZE+1]!=i){
            sync_printf("P%d:: ERROR - read incorrect data!\n",currpid);
        }
    }

    process_info(currpid);

    sleepms(200); // waiting so that main can see FFS taken

    return OK;
}





process	main(void)
{

    uint32 i = 0;

    sync_printf("\n\nTESTS START NOW...\n");
    sync_printf("-------------------\n\n");

    /* After initialization */
    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    sync_printf("P%d:: Spawning 2 processes that do not perform any allocation...\n\n", currpid);

    resume(vcreate((void *)empty_process, INITSTK, 1, "p1", 0));
    sleepms(1000);
    resume(vcreate((void *)empty_process, INITSTK, 1, "p2", 0));

    receive();
    receive();

    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    sync_printf("P%d:: Spawning 1 process that performs allocations...\n\n", currpid);
    resume(vcreate((void *)vmalloc_process, INITSTK, 1, "empty1", 0));

    receive();
    sleepms(100);

    sync_printf("\nP%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);
    sync_printf("P%d:: Spawning 10 concurrent processes (interleaving can change from run to run)...\n\n", currpid);
    for (i=0; i<10; i++){
        resume(vcreate((void *)vmalloc_process2, INITSTK, 1, "p", 0));
    }

    sleepms(100);

    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    sync_printf("P%d:: Letting the processes terminate...\n\n");
    for (i=0; i<10; i++){
        receive();
    }

    sleepms(100);

    sync_printf("P%d:: Free FFS pages = %d out of %d\n\n", currpid, free_ffs_pages(), MAX_FFS_SIZE);

    return OK;
}