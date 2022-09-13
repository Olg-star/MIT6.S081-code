	#
        # code to switch between user and kernel space.
        #
        # this code is mapped at the same virtual address
        # (TRAMPOLINE) in user and kernel space so that
        # it continues to work when it switches page tables.
	#
	# kernel.ld causes this to be aligned
        # to a page boundary.
        #
        # .section指示把代码划分成若干个段（Section），程序被操作系统加载执行时，每个段被加载到不同的地址，
        # 操作系统对不同的页面设置不同的读、写、执行权限。

        # 一个符号（Symbol），符号在汇编程序中代表一个地址，可以用在指令中，汇编程序经过汇编器的处理之后，
        # 所有的符号都被替换成它所代表的地址值。在C语言中我们通过变量名访问一个变量，其实就是读写某个地址的内存单元，
        # 我们通过函数名调用一个函数，其实就是跳转到该函数第一条指令所在的地址，所以变量名和函数名都是符号，本质上是代表内存地址的。


	.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uservec
uservec:    
	# stvec: 指向了内核中处理trap的指令的起始地址。
        #
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #
        # sscratch points to where the process's p->trapframe is
        # mapped into user space, at TRAPFRAME.
        #
        
	# swap a0 and sscratch
        # so that a0 is TRAPFRAME 之后内核就可以任意的使用a0寄存器了
        # trap处理代码必须存在于每一个user page table中。
        # 因为ecall并不会切换page table，我们需要在user page 
        # 而这个trampoline page，是由内核小心的映射到每一个user page table中，
        # 以使得当我们仍然在使用user page table时，内核在一个地方能够执行trap机制的最开始的一些指令。
        csrrw a0, sscratch, a0

        # save the user registers in TRAPFRAME
        # 这些指令就是执行sd，将每个寄存器保存在trapframe的不同偏移位置。
        # 每个寄存器被保存在了偏移量+a0的位置。
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)

        # restore kernel stack pointer from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), p->trapframe->kernel_trap
        ld t0, 16(a0)

        # restore kernel page table from p->trapframe->kernel_satp
        # 实际上严格来说，t1的内容并不是kernel page table的地址，这是你需要向SATP寄存器写入的数据。
        # 它包含了kernel page table的地址，但是移位了（注，详见4.3），并且包含了各种标志位。
        ld t1, 0(a0)
        csrw satp, t1 # 这条指令执行完成之后，当前程序会从user page table切换到kernel page table。
        sfence.vma zero, zero

        # a0 is no longer valid, since the kernel page
        # table does not specially map p->tf.

        # trampoline page在user page table中的映射与kernel page table中的映射是完全一样的。
        # 这两个page table中其他所有的映射都是不同的，只有trampoline page的映射是一样的，
        # 因此我们在切换page table时，寻址的结果不会改变，我们实际上就可以继续在同一个代码序列中执行程序而不崩溃。
        # 这是trampoline page的特殊之处，它同时在user page table和kernel page table都有相同的映射关系。

        # jump to usertrap(), which does not return
        jr t0

.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # switch from kernel to user.
        # usertrapret() calls here.
        # a0: TRAPFRAME, in user page table.
        # a1: user page table, for satp.

        # switch to the user page table.
        csrw satp, a1
        sfence.vma zero, zero

        # put the saved user a0 in sscratch, so we
        # can swap it with our a0 (TRAPFRAME) in the last step.
        # 在uservec函数中，第一件事情就是交换SSRATCH和a0寄存器。
        # 而这里，我们将SSCRATCH寄存器恢复成保存好的用户的a0寄存器。
        # 在这里a0是trapframe的地址，因为C代码usertrapret函数中将trapframe地址作为第一个参数传递过来了。
        # 112是a0寄存器在trapframe中的位置。（注，这里有点绕，本质就是通过当前的a0寄存器找出存在trapframe中的a0寄存器）
        # 我们先将这个地址里的数值保存在t0寄存器中，之后再将t0寄存器的数值保存在SSCRATCH寄存器中。
        ld t0, 112(a0)
        csrw sscratch, t0

        # restore all but a0 from TRAPFRAME
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0, and save TRAPFRAME in sscratch
        # 接下来，在我们即将返回到用户空间之前，我们交换SSCRATCH寄存器和a0寄存器的值。
        # 前面我们看过了SSCRATCH现在的值是系统调用的返回值2，a0寄存器是trapframe的地址。
        # 交换完成之后，a0持有的是系统调用的返回值，SSCRATCH持有的是trapframe的地址。
        # 之后trapframe的地址会一直保存在SSCRATCH中，直到用户程序执行了另一次trap。现在我们还在kernel中。
        csrrw a0, sscratch, a0

        # 在内核前一次切换回用户空间时，内核会执行set sscratch指令，
        # 将这个寄存器的内容设置为0x3fffffe000，也就是trapframe page的虚拟地址。
        # 所以，当我们在运行用户代码，比如运行Shell时，SSCRATCH保存的就是指向trapframe的地址。
        # 之后，Shell执行了ecall指令，跳转到了trampoline page，这个page中的第一条指令会交换a0和SSCRATCH寄存器的内容。
        # 所以，SSCRATCH中的值，也就是指向trapframe的指针现在存储与a0寄存器中。
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        sret
        # sret是我们在kernel中的最后一条指令，当我执行完这条指令：
        # 程序会切换回user mode
        # SEPC寄存器的数值会被拷贝到PC寄存器（程序计数器）
        # 重新打开中断
