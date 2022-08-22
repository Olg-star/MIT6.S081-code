#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.�ں�ҳ��һ������PTE������
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code. ���ݶ���ʼ��ַ

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();//�ӿ�������ֳ�һҳ4KB����kpgtbl����ҳ��ע��ָ������ת��
  memset(kpgtbl, 0, PGSIZE);

//QEMU ���豸�ӿ���Ϊ memory-mapped���ڴ�ӳ�䣩���ƼĴ�����¶���������Щ�Ĵ���λ�������ַ�ռ�� 0x80000000 ���¡�

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);//UART����ӳ�䣬ֱ��ӳ��

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);//ֱ��ӳ��

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);//ֱ��ӳ��

  // map kernel text executable and read-only.��ʵ�������ڴ�ķ�Χ��2^29��ҳ
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);//ӳ���ں�test��ʼ��ַ��ֱ��ӳ��

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);//ֱ��ӳ��

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);//����ÿ������ջ����������ַӳ��
  
  return kpgtbl;

//���Կ�����kvmmake���������ں������ַ�ռ䵽�����ڴ�ռ��ӳ�䣬
//����Ӳ����RAM�������ӳ�䶼�Ǻ��ӳ�䣬��֤�˺��濪�������ƺ�
//ָ������ݵĵ�ַ���ܹ����뵽��ȷ��λ�á����������trampoline��ַ
//��ʼ�Ļ�����ӳ�������Σ�һ�����ں˴���εĺ��ӳ�䣬ͬʱ������
//ӳ�䵽���ں������ַ�ռ����ߴ���
//���ʣ�����Ϊʲôû�н���CLINTӲ��ӳ��Ĵ��룿������������д©�ˣ�
//��lab�����xv6�������н���CLINTӲ��ӳ��Ĵ���ġ�
}

// Initialize the one kernel_pagetable
void
kvminit(void)//�����˳�ʼ����kernel_pagetable���ȫ��ҳ���������ں������ַ�ռ䵽����ռ��ӳ�䡣
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()//��satpװ��ҳ��
{
  w_satp(MAKE_SATP(kernel_pagetable));//MAKE_SATP�Ƚ��ں˱�����ʮ��λȻ��ͨ�����������ø���λ��mode��������satp
  sfence_vma();//������ַ�����йص����л��棨����PLB�е���Ŀ��
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
//�� alloc=0 ʱ��walk������������ģ��mmu�����ø�����ҳ��pagetable
//�������ַva�����ظ�va��pagetable�ж�Ӧ��ҳ����ָ�롣
//��Ӧ���ݽṹΪpte_t���� alloc=1 ʱ������ڷ����ַ�Ĺ����з���ĳ��ҳ��
//��Ӧ�ö�Ӧva�ı�����ڣ�������kalloc.c�е�kalloc��������һҳ��Ȼ��
//��д�ñ���ʹ��ָ��������ҳ�棨��alloc=0ʱ��򵥵ر�����
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
/*
  pagetable ҳ��PTE�����飩
  va �����ַ alloc ʧЧʱ�ò��÷���
  1.����ӳ�䣬ÿ��ȡ��Ӧ����PPN����PX�궨�崦���õ���pagetable��ƫ���������������¶�ӦĿ����±�ֵ��
  2.�Ӹ�����ȡ����PTE�������ҳ��Ч��������ȥ����ʮλ�ı�־λ��������ʮ��λ�������һ��ҳ����ʼֵ���ɴ˿ɼ�ҳ����4KB���룩
  3.����Ч������kalloc����һ����ҳ�����ÿ�ҳ����ʼֵ����12λ������10λ����pte�������ñ�־λ
  4.���ֺ󣬷��ص�����ҳ�����pte���ӳ�䵽�����ڴ��ҳ��ʼ��ַ
*/
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {//���ҵڶ����͵�һ��ҳ��
    pte_t *pte = &pagetable[PX(level, va)];//ȡ����ҳ���ҳ�������(54λ)��PX����ȡ��ƫ�Ƶ�ַ
    if(*pte & PTE_V) {//PTE_V��ʾҳ���Ƿ���Ч
      pagetable = (pagetable_t)PTE2PA(*pte);//ȥ����ʮλ��ʾ��־λ��������ʮ��λ�õ���һ��ҳ��ĵ�ַ������ȥ��ͼ
    } else {//��ҳ��Ч�������
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;//����ҳ����,����Ϊ�·����ҳ��ĵ�ַ��������Ϊ��Ч
    }
  }
  return &pagetable[PX(0, va)];//���ڵ�ҳ���ǵ������������ڴ������ַ�Ķ�Ӧ��ҳ���ַ����PPN����ҳ��ĵ�ַ
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
//�ú������ڲ�������mappages������Ч����mappages��ͬ
//��Ϊһ���װ������һ�������飨����panic��
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
//�ú������ڲ�������work��������alloc=1��
//�ú����������Ǹ��ݸ�����pagetable�����úô�va��ʼ�Ĵ�СΪsize������
//��Ӧ��ҳ���ʹ�����ǽ���ӳ�䵽��pa��ʼ����СΪsize������ҳ�档
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
/*
  1.��ȷ��Ҫӳ��������ַ�ռ䣬�Ǵ�һҳ����һҳ��������������������ȡ��
  2.ÿ����һҳ������walk�������õ����յ�����ҳ����
  3.����ҳ��ӳ�����ô�����û��ӳ�䣬����ӳ���ϵ��pa
*/
  uint64 a, last;//��ҳΪ��λ�������б���
  pte_t *pte;//ҳ����

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);//���°�4KB����
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)//���ص�����ҳ���ҳ�����ַ
      return -1;
    if(*pte & PTE_V)//��Ч���������ٽ���ӳ�䵽����ռ�
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;//��pa�����ַ����12λ������10λ������PTE��ͬʱ����Ϊ��Чλ��perm��������־λ
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
//��va��ʼ��npages��ӳ���ϵȡ������*pte=0������do_free=1���ͷ��ڴ�ռ�
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)//�������
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
    if((*pte & PTE_V) == 0)//��û��Ч
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)//��ȡ��־λ,�ж��ǲ���һ��Ҷ��
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);//ת������ʵ�������ַ
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
    if(mem == 0){//û�ڴ��
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){//����ӳ��ʧ��
      kfree(mem);//�����������ҳ���ͷŵ�
      uvmdealloc(pagetable, a, oldsz);//��������ӳ���ϵ��ɾ����
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

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){//������ͬһҳ���������
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);//ȡ��ӳ���ϵ
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
    pa0 = walkaddr(pagetable, va0);//va0�������ڴ��Ӧ�������ַ
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
