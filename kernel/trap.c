#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)//��������Ȩ�����²ſ���ִ��
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  //�ڴ��ں˿ռ���뵽�û��ռ�֮ǰ���ں˻����ú�STVEC�Ĵ���ָ���ں�ϣ��trap�������е�λ�á�
  //��ʹtrampoline page�����û���ַ�ռ��user page table��ɵ�ӳ�䣬�û����벻��д����
  //��Ϊ��Щpage��Ӧ��PTE��û������PTE_u��־λ����Ҳ��Ϊʲôtrap�����ǰ�ȫ�ġ�
  w_stvec((uint64)kernelvec);//�ں˿ռ�trap��������λ��

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  // Ҫ�����û����������������Ȼ������SEPC�Ĵ����У�
  // ���ǿ��ܷ���������������������ں���ִ��ʱ��
  // ���ǿ����л�����һ�����̣������뵽�Ǹ�������û��ռ䣬Ȼ���Ǹ����̿����ٵ���һ��ϵͳ���ý�������SEPC�Ĵ��������ݱ����ǡ�
  // ���ԣ�������Ҫ���浱ǰ���̵�SEPC�Ĵ�����һ����ý��̹������ڴ��У�����������ݲŲ��ᱻ���ǡ���������ʹ��trapframe��������������������
  
  if(r_scause() == 8){//usertrap������ԭ�� 8����������������trap����������Ϊϵͳ����
    // system call

    if(p->killed)//����ǲ����������Ľ���ɱ���˵�ǰ����
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // ��RISC-V�У��洢��SEPC�Ĵ����еĳ�������������û������д���trap��ָ��ĵ�ַ��
    // ���ǵ����ǻָ��û�����ʱ������ϣ������һ��ָ��ָ���Ҳ����ecall֮���һ��ָ�
    // ���Զ���ϵͳ���ã����Ƕ��ڱ�����û������������4���������ǻ���ecall����һ��ָ��ָ�������������ִ��ecallָ�
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // XV6���ڴ���ϵͳ���õ�ʱ��ʹ���жϣ������жϿ��Ը���ķ�����Щϵͳ������Ҫ���ʱ�䴦��
    // �ж����ǻᱻRISC-V��trapӲ���رգ����������ʱ��㣬������Ҫ��ʽ�Ĵ��жϡ�
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)//�ٴμ�鵱ǰ�û������Ƿ�ɱ���ˣ���Ϊ���ǲ���ָ�һ����ɱ���Ľ���
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();//�����������˲��ַ�����C������ʵ�ֵķ��ص��û��ռ�Ĺ�����
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  // ����ر��ж�����Ϊ���ǽ�Ҫ����STVEC�Ĵ�����ָ���û��ռ��trap������룬
  // ��֮ǰ���ں��е�ʱ������ָ������ں˿ռ��trap������루6.6����
  // ���ǹر��ж���Ϊ�����ǽ�STVEC���µ�ָ���û��ռ��trap�������ʱ��������Ȼ���ں���ִ�д��롣
  // �����ʱ������һ���жϣ���ô����ִ�л������û��ռ��trap������룬��������������Ȼ���ں��У�
  // ���ڸ��ָ�������ϸ�ڵ�ԭ����ᵼ���ں˳���������������ر��жϡ�
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // ���������ջ�ִ��sretָ��ص��û��ռ䡣
  // λ��trampoline��������sretָ������´��жϡ�
  // ��������ʹ���Ǹոչر����жϣ���������ִ���û�����ʱ�ж��Ǵ򿪵ġ�
  w_stvec(TRAMPOLINE + (uservec - trampoline));//������STVEC�Ĵ���ָ��trampoline����

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  // ����SSTATUS�Ĵ���������һ�����ƼĴ�����
  // ����Ĵ�����SPP bitλ������sretָ�����Ϊ����bitΪ0��ʾ�´�ִ��sret��ʱ��������Ҫ����user mode������supervisor mode��
  // ����Ĵ�����SPIE bitλ�����ˣ���ִ����sret֮���Ƿ���жϡ�
  // ��Ϊ�����ڷ��ص��û��ռ�֮�����ǵ�ȷϣ�����жϣ�
  // �������ｫSPIE bitλ����Ϊ1���޸�����Щbitλ֮�����ǻ���µ�ֵд�ص�SSTATUS�Ĵ�����
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // ������trampoline��������ִ����sretָ�
  // ����ָ��Ὣ������������ó�SEPC�Ĵ�����ֵ�������������ǽ�SEPC�Ĵ�����ֵ���ó�֮ǰ������û������������ֵ��
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // �����������Ǹ���user page table��ַ������Ӧ��SATPֵ�����������ڷ��ص��û��ռ��ʱ��������page table���л���
  // ʵ���ϣ����ǻ��ڻ�����trampoline�����page table���л�������Ҳֻ����trampoline������л���
  // ��Ϊֻ��trampoline�д�����ͬʱ���û����ں˿ռ���ӳ�䡣
  // �����������ڻ�û����trampoline�����У��������ڻ���һ����ͨ��C�����У�
  // �����������ǽ�page tableָ��׼���ã��������ָ����Ϊ�ڶ����������ݸ������룬��������������a1�Ĵ�����
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);//��������ǽ�Ҫ��ת��������ĵ�ַ
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);//���ú�������һ�����������a0�������Ϊʲôa0�������ֵ��ָ��trapframe��ָ�롣
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

