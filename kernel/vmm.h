#ifndef _VMM_H_
#define _VMM_H_

#include "riscv.h"

/* --- utility functions for virtual address mapping --- */
int map_pages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
// permission codes.
enum VMPermision {
  PROT_NONE = 0,
  PROT_READ = 1,
  PROT_WRITE = 2,
  PROT_EXEC = 4,
};

uint64 prot_to_type(int prot, int user);
pte_t *page_walk(pagetable_t pagetable, uint64 va, int alloc);
uint64 lookup_pa(pagetable_t pagetable, uint64 va);

/* --- kernel page table --- */
// pointer to kernel page directory
extern pagetable_t g_kernel_pagetable;

void kern_vm_map(pagetable_t page_dir, uint64 va, uint64 pa, uint64 sz, int perm);

// Initialize the kernel pagetable
void kern_vm_init(void);

/* --- user page table --- */
void *user_va_to_pa(pagetable_t page_dir, void *va);
void user_vm_map(pagetable_t page_dir, uint64 va, uint64 size, uint64 pa, int perm);
void user_vm_unmap(pagetable_t page_dir, uint64 va, uint64 size, int free);

uint64 user_malloc(uint64 size, int perm);
void user_free(uint64 va);

typedef struct bid_linked_list_t {
  struct bid_linked_list_t *pre, *suc;
} bid_linked_list;

typedef struct page_info_t {
  bid_linked_list *pre, *suc;
  uint64 va;
  struct segment_info_t *head;
  struct page_info_t *next;
} page_info;

typedef struct segment_info_t {
  bid_linked_list *pre, *suc;
  uint64 va;
  uint16 size, occupy;
} segment_info;

#endif
