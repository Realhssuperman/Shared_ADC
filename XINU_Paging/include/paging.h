/* paging.h */
#ifndef _PAGING_H_
#define _PAGING_H_

/* Macros */

#define XINU_PAGES      4096    /* page size of kernel pages */

#define PAGE_SIZE       4096    /* number of bytes per page                              */
#define MAX_SWAP_SIZE   8192    /* size of swap space (in frames)                        */
#define MAX_FFS_SIZE    4096    /* size of FFS space  (in frames)                        */
#define MAX_PT_SIZE     1024    /* size of space used for page tables (in frames)        */
#define PTDPTESize      4
#define BYTE_SIZE       32

#define PAGE_TABLE_SIZE (MAX_PT_SIZE * PAGE_SIZE )
#define FFS_SIZE (MAX_FFS_SIZE * PAGE_SIZE )
#define SWAP_SIZE (MAX_SWAP_SIZE * PAGE_SIZE )
/* Memory Sections */
#define KERNEL_START    0x00000000
#define KERNEL_END      0x01000000
//#define PAGE_DIRECTORY_START_KERNEL 0x01000000
#define PAGE_TABLE_START            0x01000000


//#define DEBUG
/* Structure for a page directory entry */

typedef struct {

  unsigned int pd_pres	: 1;		/* page table present?		*/
  unsigned int pd_write : 1;		/* page is writable?		*/
  unsigned int pd_user	: 1;		/* is use level protection?	*/
  unsigned int pd_pwt	: 1;		/* write through cachine for pt?*/
  unsigned int pd_pcd	: 1;		/* cache disable for this pt?	*/
  unsigned int pd_acc	: 1;		/* page table was accessed?	*/
  unsigned int pd_mbz	: 1;		/* must be zero			*/
  unsigned int pd_fmb	: 1;		/* four MB pages?		*/
  unsigned int pd_global: 1;		/* global (ignored)		*/
  unsigned int pd_avail : 3;		/* for programmer's use		*/
  unsigned int pd_base	: 20;		/* location of page table?	*/
} pd_t;

/* Structure for a page table entry */

typedef struct {

  unsigned int pt_pres	: 1;		/* page is present?		*/
  unsigned int pt_write : 1;		/* page is writable?		*/
  unsigned int pt_user	: 1;		/* is use level protection?	*/
  unsigned int pt_pwt	: 1;		/* write through for this page? */
  unsigned int pt_pcd	: 1;		/* cache disable for this page? */
  unsigned int pt_acc	: 1;		/* page was accessed?		*/
  unsigned int pt_dirty : 1;		/* page was written?		*/
  unsigned int pt_mbz	: 1;		/* must be zero			*/
  unsigned int pt_global: 1;		/* should be zero in 586	*/
  unsigned int pt_avail : 3;		/* for programmer's use		*/
  unsigned int pt_base	: 20;		/* location of page?		*/
} pt_t;

/* Structure for a virtual address */

typedef struct{
  unsigned int pg_offset : 12;		/* page offset			*/
  unsigned int pt_offset : 10;		/* page table offset		*/
  unsigned int pd_offset : 10;		/* page directory offset	*/
} virt_addr_t;
#define PG_MASK 0x00000FFF
#define PT_MASK 0x000003FF
#define PD_MASK 0x000003FF

#define PG_MASK_A 0x00000FFF
#define PT_MASK_A 0x003FF000
#define PD_MASK_A 0xFFC00000

/* Structure for a physical address */
#define FM_OFFSET_MASK  0x00000FFF
#define FM_NUM_MASK     0xFFFFF000
typedef struct{
  unsigned int fm_offset : 12;		/* frame offset			*/
  unsigned int fm_num : 20;		    /* frame number			*/
} phy_addr_t;



/* Functions to manipulate control registers and enable paging (see control_reg.c)	 */

unsigned long read_cr0(void);

unsigned long read_cr2(void);

unsigned long read_cr3(void);

unsigned long read_cr4(void);

void write_cr0(unsigned long n);

void write_cr3(unsigned long n);

void write_cr4(unsigned long n);

void enable_paging();

/* bitmap.c */
void SetBit(uint32 A[], uint32 k);
void ClearBit(uint32 A[], uint32 k);
int TestBit(uint32 A[], uint32 k);

/* init_paging - paging_util.c*/
void init_paging();
uint32* get_new_page_pt();
uint32* get_new_page_vmem();
uint32* get_new_page_swap();
void free_page_pt(uint32* page_addr);
void free_page_vmem(uint32* page_addr);
void free_page_swap(uint32* page_addr);
syscall free_page(uint32* map, const uint32* start_addr, const uint32* page_addr);
uint32 pde2uint32(pd_t* pde);
uint32 pte2uint32(pt_t* pte);
uint32 virt_addr_t2uint32(virt_addr_t* virtAddr);

/* debug functions - paging_util.c */
uint32 free_ffs_pages();
uint32 free_swap_pages();
uint32 allocated_virtual_pages(pid32 pid);
uint32 used_ffs_frames(pid32 pid);
uint32 used_swap_frames(pid32 pid);
void pde_info(pd_t* pde);
void pte_info(pt_t* pte);
void print_page_directory(pid32 pid);
void print_page_directory_only(pid32 pid);
/* Page Fault Handler Function - pagefault_handler.c*/
syscall pagefault_handler();

extern uint32 sys_PDBR;
extern uint32 kernel_page_num;
#endif
