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

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem KmemArray[NCPU];//给每个CPU分配一个kmem，也就是每个CPU拥有自己的freelist和对应的lock
char* KmemNames[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};
void
kinit()
{
  int i=0;
  for(i=0;i<NCPU;i++){
    initlock(&(KmemArray[i].lock), KmemNames[i]);//对每个cpu上的freelist的锁进行初始化
  }
  freerange(end, (void*)PHYSTOP);//释放所有页表
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);//释放内存页添加到每个cpu上的freelist上
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int id = cpuid();
  acquire(&(KmemArray[id].lock));
  r->next = KmemArray[id].freelist;
  KmemArray[id].freelist = r;
  release(&(KmemArray[id].lock));
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
  acquire(&(KmemArray[id].lock));
  r = KmemArray[id].freelist;
  if(r){//该空页链表还有空间
    KmemArray[id].freelist = r->next;
    release(&(KmemArray[id].lock));
    pop_off();
  }
  else{//没有空间需要steal
    release(&(KmemArray[id].lock));
    pop_off();
    //注意：需要在这里先释放锁，避免cpua申请cpub锁，cpub申请cpua锁的死锁现象
    int i = id, j = 0;
    for(j=0,i=(id+1)%NCPU;j<NCPU-1;j++,i=(i+1)%NCPU){//遍历其他cpu上的空闲链表有没有空闲页
        acquire(&(KmemArray[i].lock));
        if(KmemArray[i].freelist){
          r = KmemArray[i].freelist;
          KmemArray[i].freelist = r->next;
          release(&(KmemArray[i].lock));
          break;
        }
        release(&KmemArray[i].lock);
    }
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
