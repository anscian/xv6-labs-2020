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
void _kfree(void *pa, int hartidx);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(kmem.lock + i, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(int hartidx = 0; p + PGSIZE <= (char*)pa_end; p += PGSIZE, hartidx = (hartidx + 1) % NCPU)
    _kfree(p, hartidx);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
_kfree(void *pa, int hartidx)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(kmem.lock + hartidx);
  r->next = kmem.freelist[hartidx];
  kmem.freelist[hartidx] = r;
  release(kmem.lock + hartidx);
}
void
kfree(void *pa)
{
  int hartidx;

  push_off();
  hartidx = cpuid();
  pop_off();

  _kfree(pa, hartidx);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r, *rn;
  int hartidx;

  push_off();
  hartidx = cpuid();
  pop_off();

  r = kmem.freelist[hartidx];
  if(r)
    kmem.freelist[hartidx] = r->next;
  else{
    for(int i = 0; i < NCPU; i++){
      acquire(kmem.lock + i);
      if((r = kmem.freelist[i])){
        kmem.freelist[i] = r->next;

        kmem.freelist[hartidx] = rn = r->next;
        if(rn) rn = rn->next;
        for(int flag = 0; rn; rn = rn->next, flag = !flag)
          if(flag)
            kmem.freelist[hartidx] = kmem.freelist[hartidx]->next;

        rn = kmem.freelist[hartidx];
        if(rn)
          kmem.freelist[hartidx] = rn->next, rn->next = 0;

        release(kmem.lock + i);
        break;
      }
      release(kmem.lock + i);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
