#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//Add a sys_trace() function in kernel/sysproc.c
//remember its argument in a new variable in the proc structure
//retrieve system call arguments from user space are in kernel/syscall.c
uint64
sys_trace(void){
  int mask;
  if(argint(0,&mask)<0){//将用户态的参数传给内核，因为只有一个参数，所以是第一个（0开始索引）
    return -1;
  }
  myproc()->mask = mask;//将参数传给内核态的进程
  return 0;
}

//add a system call, sysinfo, that collects information about the running system. 
uint64 
sys_sysinfo(void){
  uint64 sf;//pointer to sysinfo
  if(argaddr(0,&sf)<0){
    return -1;
  }

  struct proc *p = myproc();
  struct sysinfo sysf;
  sysf.freemem = amount_of_free_memory();
  sysf.nproc = cnt_processes();
  if(copyout(p->pagetable,sf,(char*)&sysf,sizeof(sysf))<0)
    return -1;
  return 0;
}
