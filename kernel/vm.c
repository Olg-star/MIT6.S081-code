#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.内核页表，一个存着PTE的数组
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code. 数据段起始地址

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();//从空闲链表分出一页4KB来给kpgtbl做根页表，注意指针类型转换
  memset(kpgtbl, 0, PGSIZE);

//QEMU 将设备接口作为 memory-mapped（内存映射）控制寄存器暴露给软件，这些寄存器位于物理地址空间的 0x80000000 以下。

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);//UART驱动映射，直接映射

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);//直接映射

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);//直接映射

  // map kernel text executable and read-only.是实际物理内存的范围，2^29个页
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);//映射内核test起始地址，直接映射

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);//直接映射

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);//建立每个进程栈，并建立地址映射
  
  return kpgtbl;

//可以看到，kvmmake建立起了内核虚拟地址空间到物理内存空间的映射，
//包括硬件和RAM，这里的映射都是恒等映射，保证了后面开启虚存机制后
//指令和数据的地址仍能够翻译到正确的位置。例外的是以trampoline地址
//起始的汇编代码映射了两次，一次是内核代码段的恒等映射，同时还把它
//映射到了内核虚拟地址空间的最高处。
//疑问，这里为什么没有建立CLINT硬件映射的代码？？怀疑它这里写漏了？
//在lab里面的xv6代码是有建立CLINT硬件映射的代码的。
}

// Initialize the one kernel_pagetable
void
kvminit(void)//建立了初始化了kernel_pagetable这个全局页表，建立了内核虚拟地址空间到物理空间的映射。
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()//将satp装入页表
{
  w_satp(MAKE_SATP(kernel_pagetable));//MAKE_SATP先将内核表右移十二位然后通过或运算设置高四位的mode，来构造satp
  sfence_vma();//清除与地址翻译有关的所有缓存（例如PLB中的条目）
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
//在 alloc=0 时，walk函数的作用是模仿mmu，利用给定的页表pagetable
//和虚拟地址va，返回该va在pagetable中对应的页表项指针。
//对应数据结构为pte_t。在 alloc=1 时，如果在翻译地址的过程中发现某级页表
//中应该对应va的表项不存在，则会调用kalloc.c中的kalloc函数分配一页，然后
//填写该表项使其指向分配出的页面（在alloc=0时会简单地报错）。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
/*
  pagetable 页表（PTE的数组）
  va 虚拟地址 alloc 失效时用不用分配
  1.三级映射，每次取对应级的PPN，有PX宏定义处理，得到在pagetable的偏移量，即该数组下对应目标的下标值；
  2.从该数组取出该PTE，如果该页有效，则右移去掉低十位的标志位，再左移十二位构造出下一级页表起始值（由此可见页表以4KB对齐）
  3.若无效，调用kalloc分配一个空页，将该空页的起始值右移12位再左移10位赋给pte，再设置标志位
  4.两轮后，返回第三级页表里的pte项，即映射到物理内存的页起始地址
*/
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {//查找第二级和第一级页表
    pte_t *pte = &pagetable[PX(level, va)];//取各级页表的页表项出来(54位)，PX用于取出偏移地址
    if(*pte & PTE_V) {//PTE_V表示页表是否有效
      pagetable = (pagetable_t)PTE2PA(*pte);//去掉低十位表示标志位，再左移十二位得到下一个页表的地址，可以去看图
    } else {//该页无效，则分配
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;//更新页表项,设置为新分配的页表的地址，并设置为有效
    }
  }
  return &pagetable[PX(0, va)];//现在的页表是第三级，返回内存物理地址的对应的页表地址，即PPN所在页表的地址
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
//该函数在内部调用了mappages函数，效果和mappages相同
//作为一层封装，多了一个错误检查（调用panic）
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
//该函数在内部调用了work函数，且alloc=1。
//该函数的作用是根据给出的pagetable，设置好从va开始的大小为size的区域
//对应的页表项，使得它们将会映射到从pa开始，大小为size的物理页面。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
/*
  1.先确定要映射的虚拟地址空间，是从一页到哪一页，方便后序遍历，是向下取整
  2.每遍历一页，调用walk函数，得到最终第三级页表项
  3.若该页被映射则不用处理，若没有映射，则建立映射关系到pa
*/
  uint64 a, last;//以页为单位，来进行遍历
  pte_t *pte;//页表项

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);//向下按4KB对齐
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)//返回第三级页表的页表项地址
      return -1;
    if(*pte & PTE_V)//有效，则无需再建立映射到物理空间
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;//将pa这个地址右移12位再左移10位来构造PTE，同时设置为有效位，perm是其他标志位
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
//从va开始的npages的映射关系取消掉（*pte=0），若do_free=1则释放内存空间
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)//必须对齐
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
    if((*pte & PTE_V) == 0)//有没有效
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)//获取标志位,判断是不是一个叶子
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);//转换成真实的物理地址
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){//没内存分
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){//建立映射失败
      kfree(mem);//将分配的物理页给释放掉
      uvmdealloc(pagetable, a, oldsz);//将建立的映射关系都删除掉
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){//假如在同一页则无需更改
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);//取消映射关系
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);//va0在物理内存对应的物理地址
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
