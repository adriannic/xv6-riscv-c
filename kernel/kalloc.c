// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "defs.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

#define MAXPAGES (PHYSTOP / PGSIZE)

void freerange(void *pa_start, void *pa_end);
void _freerange(void *pa_start, void *pa_end);
void _kfree(void *pa);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
  uint ref;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct run runs[MAXPAGES];
} kmem;

void kinit() {
  initlock(&kmem.lock, "kmem");
  _freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

void _freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    _kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = &kmem.runs[(uint64)pa / PGSIZE];
  if (r->ref < 1)
    panic("kfree: tried to free an unallocated page");
  if (r->ref > 1) {
    acquire(&kmem.lock);
    r->ref--;
    release(&kmem.lock);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  acquire(&kmem.lock);
  r->ref = 0;
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void _kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("_kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = &kmem.runs[(uint64)pa / PGSIZE];

  acquire(&kmem.lock);
  r->ref = 0;
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    r->ref = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if (r) {
    memset((char *)((r - kmem.runs) * PGSIZE), 5, PGSIZE); // fill with junk
    return (void *)((r - kmem.runs) * PGSIZE);
  }
  return (void *)0;
}

void kclone(uint64 pa) {
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kclone");

  struct run *r = &kmem.runs[(uint64)pa / PGSIZE];

  if (r->ref < 1)
    panic("kclone: can't clone unallocated page");

  acquire(&kmem.lock);
  r->ref++;
  release(&kmem.lock);
}
