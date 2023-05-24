/*
 * virtual address mapping related functions.
 */

#include "vmm.h"
#include "riscv.h"
#include "pmm.h"
#include "util/types.h"
#include "memlayout.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "process.h"

/* --- utility functions for virtual address mapping --- */
//
// establish mapping of virtual address [va, va+size] to phyiscal address [pa, pa+size]
// with the permission of "perm".
//
int map_pages(pagetable_t page_dir, uint64 va, uint64 size, uint64 pa, int perm) {
  uint64 first, last;
  pte_t *pte;

  for (first = ROUNDDOWN(va, PGSIZE), last = ROUNDDOWN(va + size - 1, PGSIZE);
      first <= last; first += PGSIZE, pa += PGSIZE) {
    if ((pte = page_walk(page_dir, first, 1)) == 0) return -1;
    if (*pte & PTE_V)
      panic("map_pages fails on mapping va (0x%lx) to pa (0x%lx)", first, pa);
    *pte = PA2PTE(pa) | perm | PTE_V;
  }
  return 0;
}

//
// convert permission code to permission types of PTE
//
uint64 prot_to_type(int prot, int user) {
  uint64 perm = 0;
  if (prot & PROT_READ) perm |= PTE_R | PTE_A;
  if (prot & PROT_WRITE) perm |= PTE_W | PTE_D;
  if (prot & PROT_EXEC) perm |= PTE_X | PTE_A;
  if (perm == 0) perm = PTE_R;
  if (user) perm |= PTE_U;
  return perm;
}

//
// traverse the page table (starting from page_dir) to find the corresponding pte of va.
// returns: PTE (page table entry) pointing to va.
//
pte_t *page_walk(pagetable_t page_dir, uint64 va, int alloc) {
  if (va >= MAXVA) panic("page_walk");

  // starting from the page directory
  pagetable_t pt = page_dir;

  // traverse from page directory to page table.
  // as we use risc-v sv39 paging scheme, there will be 3 layers: page dir,
  // page medium dir, and page table.
  for (int level = 2; level > 0; level--) {
    // macro "PX" gets the PTE index in page table of current level
    // "pte" points to the entry of current level
    pte_t *pte = pt + PX(level, va);

    // now, we need to know if above pte is valid (established mapping to a phyiscal page)
    // or not.
    if (*pte & PTE_V) {  //PTE valid
      // phisical address of pagetable of next level
      pt = (pagetable_t)PTE2PA(*pte);
    } else { //PTE invalid (not exist).
      // allocate a page (to be the new pagetable), if alloc == 1
      if( alloc && ((pt = (pte_t *)alloc_page(1)) != 0) ){
        memset(pt, 0, PGSIZE);
        // writes the physical address of newly allocated page to pte, to establish the
        // page table tree.
        *pte = PA2PTE(pt) | PTE_V;
      }else //returns NULL, if alloc == 0, or no more physical page remains
        return 0;
    }
  }

  // return a PTE which contains phisical address of a page
  return pt + PX(0, va);
}

//
// look up a virtual page address, return the physical page address or 0 if not mapped.
//
uint64 lookup_pa(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = page_walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || ((*pte & PTE_R) == 0 && (*pte & PTE_W) == 0))
    return 0;
  pa = PTE2PA(*pte);

  return pa;
}

/* --- kernel page table part --- */
// _etext is defined in kernel.lds, it points to the address after text and rodata segments.
extern char _etext[];

// pointer to kernel page director
pagetable_t g_kernel_pagetable;

//
// maps virtual address [va, va+sz] to [pa, pa+sz] (for kernel).
//
void kern_vm_map(pagetable_t page_dir, uint64 va, uint64 pa, uint64 sz, int perm) {
  // map_pages is defined in kernel/vmm.c
  if (map_pages(page_dir, va, sz, pa, perm) != 0) panic("kern_vm_map");
}

//
// kern_vm_init() constructs the kernel page table.
//
void kern_vm_init(void) {
  // pagetable_t is defined in kernel/riscv.h. it's actually uint64*
  pagetable_t t_page_dir;

  // allocate a page (t_page_dir) to be the page directory for kernel. alloc_page is defined in kernel/pmm.c
  t_page_dir = (pagetable_t)alloc_page();
  // memset is defined in util/string.c
  memset(t_page_dir, 0, PGSIZE);

  // map virtual address [KERN_BASE, _etext] to physical address [DRAM_BASE, DRAM_BASE+(_etext - KERN_BASE)],
  // to maintain (direct) text section kernel address mapping.
  kern_vm_map(t_page_dir, KERN_BASE, DRAM_BASE, (uint64)_etext - KERN_BASE,
         prot_to_type(PROT_READ | PROT_EXEC, 0));

  sprint("KERN_BASE 0x%lx\n", lookup_pa(t_page_dir, KERN_BASE));

  // also (direct) map remaining address space, to make them accessable from kernel.
  // this is important when kernel needs to access the memory content of user's app
  // without copying pages between kernel and user spaces.
  kern_vm_map(t_page_dir, (uint64)_etext, (uint64)_etext, PHYS_TOP - (uint64)_etext,
         prot_to_type(PROT_READ | PROT_WRITE, 0));

  sprint("physical address of _etext is: 0x%lx\n", lookup_pa(t_page_dir, (uint64)_etext));

  g_kernel_pagetable = t_page_dir;
}

/* --- user page table part --- */
//
// convert and return the corresponding physical address of a virtual address (va) of
// application.
//
void *user_va_to_pa(pagetable_t page_dir, void *va) {
  // TODO (lab2_1): implement user_va_to_pa to convert a given user virtual address "va"
  // to its corresponding physical address, i.e., "pa". To do it, we need to walk
  // through the page table, starting from its directory "page_dir", to locate the PTE
  // that maps "va". If found, returns the "pa" by using:
  // pa = PYHS_ADDR(PTE) + (va & (1<<PGSHIFT -1))
  // Here, PYHS_ADDR() means retrieving the starting address (4KB aligned), and
  // (va & (1<<PGSHIFT -1)) means computing the offset of "va" inside its page.
  // Also, it is possible that "va" is not mapped at all. in such case, we can find
  // invalid PTE, and should return NULL.
  // panic( "You have to implement user_va_to_pa (convert user va to pa) to print messages in lab2_1.\n" );

  uint64 pa = lookup_pa(page_dir, (uint64)va);
  if (!pa) return NULL;
  pa |= ((uint64)va & ((1<<PGSHIFT)-1));
  return (void*) pa;
}

//
// maps virtual address [va, va+sz] to [pa, pa+sz] (for user application).
//
void user_vm_map(pagetable_t page_dir, uint64 va, uint64 size, uint64 pa, int perm) {
  if (map_pages(page_dir, va, size, pa, perm) != 0) {
    panic("fail to user_vm_map .\n");
  }
}

//
// unmap virtual address [va, va+size] from the user app.
// reclaim the physical pages if free!=0
//
void user_vm_unmap(pagetable_t page_dir, uint64 va, uint64 size, int free) {
  // TODO (lab2_2): implement user_vm_unmap to disable the mapping of the virtual pages
  // in [va, va+size], and free the corresponding physical pages used by the virtual
  // addresses when if 'free' (the last parameter) is not zero.
  // basic idea here is to first locate the PTEs of the virtual pages, and then reclaim
  // (use free_page() defined in pmm.c) the physical pages. lastly, invalidate the PTEs.
  // as naive_free reclaims only one page at a time, you only need to consider one page
  // to make user/app_naive_malloc to behave correctly.
  // panic( "You have to implement user_vm_unmap to free pages using naive_free in lab2_2.\n" );

  uint64 first, last;
  pte_t *pte;

  for (first = ROUNDDOWN(va, PGSIZE), last = ROUNDDOWN(va + size - 1, PGSIZE);
      first <= last; first += PGSIZE) {
    if ((pte = page_walk(page_dir, first, 1)) == 0) continue;
    *pte &= ~PTE_V;

    uint64 pa = PTE2PA((uint64) *pte);

    free_page((void*) pa);
  }
}

bid_linked_list valid_page_info_head, empty_page_info_head;
bid_linked_list valid_segment_info_head, empty_segment_info_head;

void bid_linked_list_del(bid_linked_list *p) {
  if (p->pre) p->pre->suc = p->suc;
  if (p->suc) p->suc->pre = p->pre;
}

void bid_linked_list_app(bid_linked_list *p, bid_linked_list *np) {
  np->pre = p;
  np->suc = p->suc;
  if (p->suc) p->suc->pre = np;
  p->suc = np;
}

void delete_element(bid_linked_list *empty, bid_linked_list* p) {
  bid_linked_list_del(p);
  bid_linked_list_app(empty, p);
}

void* get_element(bid_linked_list *valid, bid_linked_list *empty, int size) {
  if (empty->suc == NULL) {
    uint64 pa = (uint64)alloc_page();
    // sprint(">>> pa = %p\n", pa);
    memset((void*)pa, 0, sizeof(pa));
    for (int i=0;i+size<=PGSIZE;i+=size) {
      delete_element(empty, (void*)(pa+i));
    }
  }
  bid_linked_list *ret = empty->suc;
  bid_linked_list_del(ret);
  // bid_linked_list_app(valid, ret);
  return ret;
}

void delete_page_info(page_info *p) {delete_element(&empty_page_info_head, (bid_linked_list*)p);}
page_info *get_page_info() {return (page_info*) get_element(&valid_page_info_head, &empty_page_info_head, sizeof(page_info));}
void delete_segment_info(segment_info *p) {delete_element(&empty_segment_info_head, (bid_linked_list*)p);}
segment_info *get_segment_info() {return (segment_info*) get_element(&valid_segment_info_head, &empty_segment_info_head, sizeof(segment_info));}

uint64 alloc_page_with_vm(int perm) {
  uint64 pa = (uint64)alloc_page();
  g_ufree_page = ROUNDDOWN(g_ufree_page+PGSIZE-1, PGSIZE);
  pte_t *pte = page_walk((pagetable_t)current->pagetable, g_ufree_page, 1);
  *pte = PA2PTE(pa) | perm | PTE_V;
  uint64 ret = g_ufree_page;
  g_ufree_page += PGSIZE;
  sprint(">>> new page with pa=%p va=%p\n", pa, ret);
  return ret;
}

uint64 user_malloc_small(uint64 size, int perm) {
  sprint(">>> user malloc small %d\n", size);
  segment_info *found = NULL;

  sprint(">>> iteration begin\n");
  for (segment_info *p=(segment_info*)valid_segment_info_head.suc;p;p=(segment_info*)p->suc) 
    sprint(">>> searching va=%p size=%d occupy=%d\n", p->va, p->size, p->occupy);
  sprint(">>> iteration end\n");
  
  for (segment_info *p=(segment_info*)valid_segment_info_head.suc;p;p=(segment_info*)p->suc) {
    // sprint(">>> searching va=%p size=%d occupy=%d\n", p->va, p->size, p->occupy);
    if (!p->occupy && p->size >= size) {
      found = p;
      break;
    }
  }

  if (!found) {
    // sprint(">>> not found\n");
    found = get_segment_info();
    bid_linked_list_app(&valid_segment_info_head, (bid_linked_list*)found);
    found->va = alloc_page_with_vm(perm);
    found->size = PGSIZE;
    found->occupy = 0;
  }

  sprint(">>> found va=%p size=%d\n", found->va, found->size);

  found->occupy = 1;
  if (found->size > size) {
    segment_info *rest = get_segment_info();
    bid_linked_list_app((bid_linked_list*)found, (bid_linked_list*)rest);
    rest->va = found->va + size;
    rest->size = found->size - size;
    rest->occupy = 0;
    sprint(">>> rest va=%p size=%d\n", rest->va, rest->size);
  }
  found->size = size;
  return found->va;
}

uint64 user_malloc_big(uint64 size, int perm) {  
  g_ufree_page = ROUNDDOWN(g_ufree_page+PGSIZE-1, PGSIZE);
  uint64 va = g_ufree_page;
  int pagenum = (size+PGSIZE+1) / PGSIZE;
  page_info *last = NULL;
  for (int i=0;i<pagenum;i++) {
    page_info *now = get_page_info();
    if (last != NULL) last->next = now;
    last = now;
    now->va = alloc_page_with_vm(perm);
  }
  return va;
}

uint64 user_malloc(uint64 size, int perm) {
  if (size >= PGSIZE) {
    return user_malloc_big(size, perm);
  } else {
    return user_malloc_small(size, perm);
  }
}

void free_page_by_va(uint64 va) {
  sprint(">>> free page by va=%p\n", va);
  pte_t *pte;
  if ((pte = page_walk((pagetable_t)current->pagetable, va, 0)) == 0) return;
  *pte &= ~PTE_V;
  uint64 pa = PTE2PA((uint64) *pte);
  free_page((void*) pa);
}

void user_free_small(segment_info *p) {
  sprint(">>> user free small %p\n", p->va);
  p->occupy = 0;
  if (p->pre && p->pre != &valid_segment_info_head) {
    segment_info *L = (segment_info*) p->pre;
    if (ROUNDDOWN(L->va, PGSIZE) == ROUNDDOWN(p->va, PGSIZE) && !L->occupy) {
      p->va = L->va;
      p->size = p->size + L->size;
      delete_segment_info(L);
    }
  }
  if (p->suc) {
    segment_info *R = (segment_info*) p->suc;
    if (ROUNDDOWN(R->va, PGSIZE) == ROUNDDOWN(p->va, PGSIZE) && !R->occupy) {
      p->size = p->size + R->size;
      delete_segment_info(R);
    }
  }
  if (p->size == PGSIZE) {
    free_page_by_va(p->va);
  }
}

void user_free_big(page_info *p) {
  for (page_info *q=p;q;) {
    page_info *nxt = q->next;
    free_page_by_va(q->va);
    delete_page_info(q);
    q = nxt;
  }
}

void user_free(uint64 va) {
  sprint(">>> iteration begin\n");
  for (segment_info *p=(segment_info*)valid_segment_info_head.suc;p;p=(segment_info*)p->suc) 
    sprint(">>> searching va=%p size=%d occupy=%d\n", p->va, p->size, p->occupy);
  sprint(">>> iteration end\n");

  for (segment_info *p=(segment_info*)valid_segment_info_head.suc;p;p=(segment_info*)p->suc) {
    if (p->va == va) {
      user_free_small(p);
      break;
    }
  }
  for (page_info *p=(page_info*)valid_page_info_head.suc;p;p=(page_info*)p->suc) {
    if (!p->next && p->va == va) user_free_big(p);
  }
}