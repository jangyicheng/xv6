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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];
char kname[NCPU][8];
void kinit()
{

  for (int i = 0; i < NCPU; i++)
  {
    snprintf(kname[i], sizeof(kname[i]), "kmem-%d", i);
    initlock(&kmems[i].lock, kname[i]);
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  push_off();
  int id = cpuid();


  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist; // 当前指针指向 freelist
  kmems[id].freelist = r;       // freelist重新赋值为当前指针
  release(&kmems[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();


  acquire(&kmems[id].lock);//锁定当前cpu
  r = kmems[id].freelist;

  if (r)//当前cpu对应页表不空
    kmems[id].freelist = r->next;//占用一页
  else//当前cpu对应页表为空
  {
    for (int j = 1; j <= NCPU; j++)
    {
      int i = (j + id) % NCPU;
      if (i == id)
      {
          continue;
      }
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if (r)
      {
        // kmems[id].freelist = r->next;
        kmems[i].freelist = r->next; // mem-i让出一页
      }
      release(&kmems[i].lock);
      if (r)
        break;
    }
  }
  release(&kmems[id].lock);
  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  pop_off();
  return (void *)r;
}
