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
  int refNum[(PHYSTOP-KERNBASE)/PGSIZE];//��¼��������ҳ��ҳ������
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
    acquire(&kmem.lock);//����
    kmem.refNum[((uint64)p-KERNBASE)/PGSIZE]=1;//��ʼ��Ϊ1����Ϊ����kfree���1
    release(&kmem.lock);//����
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
  if(--kmem.refNum[((uint64)pa-KERNBASE)/PGSIZE]==0){//��������һ�����������Ϊ0���ͷŸ�����ҳ
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
{ //�жϸ�pte�ǲ��Ǳ�־��COWλ
  uint64 flag = (*pte) & PTE_COW;
  if(flag == 0)
    return 0;
  else
    return 1;
}
int get_refNum(uint64 pa)
{ //�õ�ĳ�����ַ��Ӧҳ�����õĴ���
  acquire(&kmem.lock);
  int count = kmem.refNum[(pa - KERNBASE) / PGSIZE];
  release(&kmem.lock);
  return count;
}
void increment(uint64 pa)
{ //��pa�����ַ��Ӧ��ҳ��refNum���м�һ
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]++;
  release(&kmem.lock);
}
void decrement(uint64 pa)
{ //��pa�����ַ��Ӧ��ҳ��refNum���м�һ
  acquire(&kmem.lock);
  kmem.refNum[(pa - KERNBASE) / PGSIZE]--;
  release(&kmem.lock);
}
uint64 cow_alloc(pagetable_t pagetable, uint64 va)
{
  va = PGROUNDDOWN(va);//����������ҳ���п�������������Ҫ�ĸ�va����Ӧ��ҳ����ʼ��ַ
  pte_t* pte = walk(pagetable,va,0);//��ȡ��ҳ��Ӧ�ĵ�����ҳ����
  uint64 pa = PTE2PA(*pte);//��ȡ�������ַ��Ӧ�������ַ��ҳ��ʼ��ַ
  if (get_refNum(pa) == 1) //���ֻ��һ��ӳ���ϵ���������·���һҳ���ҽ�PTE_COWλȥ��������PTE_Wλ
  {
    *pte |= PTE_W;
    *pte &= ~PTE_COW;
    return pa;
  }
  else
  {
    decrement(pa); //����������һ

    char *mem; //�·��������ҳ�ĵ�ַ
    uint64 flags;
    //��PTE_COWλȥ��������PTE_Wλ
    *pte |= PTE_W;
    *pte &= ~PTE_COW;
    
    flags = PTE_FLAGS(*pte);

    if ((mem = kalloc()) == 0) //����kalloc��������ҳ
    {
      return 0;
    }
    *pte &= (~PTE_V); //�·��������ҳ���������ַ��δ��������ӳ�䣬������ʱ����Ч��
    memmove(mem, (char *)(pa), PGSIZE); ////copyһ�����ݣ������̺��ӽ��̲��ǹ����ڴ�
    if (mappages(pagetable, va, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      return 0;
    }
    return (uint64)mem;
  }
}
