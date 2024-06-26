// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

int ref_counts[PHYSTOP / PGSIZE] = {0};
struct spinlock ref_lock;

int get_ref(uint64 pa) {
  acquire(&ref_lock);
  int c = ref_counts[pa / PGSIZE];
  release(&ref_lock);
  return c;
}

void inc_ref(uint64 pa) {
  acquire(&ref_lock);
  ref_counts[pa / PGSIZE]++;
  release(&ref_lock);
}

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void kinit() {
  initlock(&ref_lock, "ref_lock");
  initlock(&kmem.lock, "kmem");
  for (int i = 0; i < PHYSTOP / PGSIZE; ++i) {
    ref_counts[i] = 1;
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref_lock);
  ref_counts[(uint64)pa / PGSIZE] -= 1;
  int c = ref_counts[(uint64)pa / PGSIZE];
  release(&ref_lock);

  if (c > 0) return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
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
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if (r) inc_ref((uint64)r);
  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk

  return (void *)r;
}

uint64 freemem(void) {
  uint64 free = 0;
  acquire(&kmem.lock);
  struct run *r = kmem.freelist;
  while (r != 0) {
    free += PGSIZE;
    r = r->next;
  }
  release(&kmem.lock);
  return free;
}
