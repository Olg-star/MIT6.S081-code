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

struct {
  struct spinlock lock;
  struct run *freelist;
  int refNum[(PHYSTOP-KERNBASE)/PGSIZE];//记录关联物理页的页表数量
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    acquire(&kmem.lock);//上锁
    kmem.refNum[((uint64)p-KERNBASE)/PGSIZE]=1;//初始化为1，因为后面kfree会减1
    release(&kmem.lock);//解锁
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");


  r = (struct run*)pa;
  acquire(&kmem.lock);
  if(--kmem.refNum[((uint64)pa-KERNBASE)/PGSIZE]==0){//引用数减一，如果引用数为0则释放该物理页
    memset(pa, 1, PGSIZE);  // Fill with junk to catch dangling refs.
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
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
  if(r){
    kmem.refNum[((uint64)r-KERNBASE)/PGSIZE] = 1;
    kmem.freelist = r->next;
  }    
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint64 is_Cow(pte_t *pte)
{ //判断该pte是不是标志了COW位
  uint64 flag = (*pte) & PTE_COW;
  if(flag == 0)
    return 0;
  else
    return 1;
}
int get_refNum(uint64 pa)
{ //得到某物理地址对应页表被引用的次数
  acquire(&kmem.lock);
  int count = kmem.refNum[(pa - KERNBASE) / PGSIZE];
  release(&kmem.lock);
  return count;
}
void increment(uint64 pa)
{ //将pa物理地址对应的页的refNum进行加一
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]++;
  release(&kmem.lock);
}
void decrement(uint64 pa)
{ //将pa物理地址对应的页的refNum进行减一
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]--;
  release(&kmem.lock);
}
uint64 cow_alloc(pagetable_t pagetable, uint64 va)
{
  va = PGROUNDDOWN(va);//由于是整个页进行拷贝处理，所以需要的该va所对应的页的起始地址
  pte_t* pte = walk(pagetable,va,0);//获取该页对应的第三级页表项
  uint64 pa = PTE2PA(*pte);//获取该虚拟地址对应的物理地址的页起始地址
  if (get_refNum(pa) == 1) //如果只有一个映射关系，则不用重新分配一页，且将PTE_COW位去掉，设置PTE_W位
  {
    *pte |= PTE_W;
    *pte &= ~PTE_COW;
    return pa;
  }
  else
  {
    decrement(pa); //让引用数减一

    char *mem; //新分配的物理页的地址
    uint64 flags;
    //将PTE_COW位去掉，设置PTE_W位
    *pte |= PTE_W;
    *pte &= ~PTE_COW;
    
    flags = PTE_FLAGS(*pte);

    if ((mem = kalloc()) == 0) //调用kalloc分配物理页
    {
      return 0;
    }
    *pte &= (~PTE_V); //新分配的物理页，该虚拟地址还未和它建立映射，所以暂时是无效的
    memmove(mem, (char *)(pa), PGSIZE); ////copy一份数据，父进程和子进程不是共享内存
    if (mappages(pagetable, va, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      return 0;
    }
    return (uint64)mem;
  }
}
