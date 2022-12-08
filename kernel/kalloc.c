// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// copy on write 
// ljx
#define IDX(addr) (((uint64)addr - KERNBASE) / PGSIZE)
#define PAGES_REFCOUNT_LENTH ((uint64)(PHYSTOP - KERNBASE) / PGSIZE + 1)


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct refcount {
  struct spinlock lock;
  int cnt;
} ;

struct refcount pages_refcount[PAGES_REFCOUNT_LENTH];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  for (int i = 0; i < PAGES_REFCOUNT_LENTH; i++){
    initlock(&pages_refcount[i].lock, "ref_count");
    // 初始化时pages_refcount应置为0，但kfree这里会减一，所以我们提前置1
    pages_refcount[i].cnt = 1;
  }

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  // 最后一次引用时才free
  if ((uint64)pa >= KERNBASE)
  {
    int cnt = update_refcount((uint64)pa, -1);
    if (cnt < 0)
      panic("kfree:pages_refcount");

    if (cnt)
    {
      return;
    }
  }
  // printf("free do\n");
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    reset_refcount((uint64)r);
  }

  return (void*)r;
}
// 为了方便更新pages_refcount, 声明了这个函数
// pa为被引用页面的物理地址，val为要增加的值（即引用次数,可为负数）,返回更新后的cnt
int update_refcount(uint64 pa, int val)
{
  if (pa < KERNBASE || pa >= PHYSTOP)
    panic("update_refcount\n");

  acquire(&pages_refcount[IDX(pa)].lock);
  
  pages_refcount[IDX(pa)].cnt += val;

  if (pages_refcount[IDX(pa)].cnt < 0)
    panic("update_refcount:invalid val!(refcount less than 0)\n");
  int ret = pages_refcount[IDX(pa)].cnt;
  release(&pages_refcount[IDX(pa)].lock);
  return ret;
}

int reset_refcount(uint64 pa)
{
  if (pa < KERNBASE || pa >= PHYSTOP)
    panic("reset_refcount\n");

  acquire(&pages_refcount[IDX(pa)].lock);
  pages_refcount[IDX(pa)].cnt = 1;

  release(&pages_refcount[IDX(pa)].lock);
  return 0;
}