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

void trapinit(void) { initlock(&tickslock, "time"); }

// set up to take exceptions and traps while in the kernel.
void trapinithart(void) { w_stvec((uint64)kernelvec); }

void naughty_process(struct proc *p, const char *msg) {
  printf("usertrap(): %s %p pid=%d\n", msg, r_scause(), p->pid);
  printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
  setkilled(p);
}

void handle_cow(struct proc *p) {
  // Store page fault
  uint64 faulted_addr = r_stval();
  faulted_addr = PGROUNDDOWN(faulted_addr);

  // Check if accessing unmapped page
  uint64 pa = walkaddr(p->pagetable, faulted_addr);
  if (pa == 0) return naughty_process(p, "unexpected scause");

  pte_t *pte = walk(p->pagetable, faulted_addr, 0);
  if (pte == 0) panic("usertrap(): no pte");
  if ((*pte & PTE_V) == 0) panic("uvmcopy: page not present");

  uint flags = PTE_FLAGS(*pte);
  // Process tried to modified non-writable page, valid page fault
  if ((flags & PTE_P) == 0) {
    return naughty_process(p, "COW tried to modify non-writable page");
  } else {
    uint64 pa = PTE2PA(*pte);
    // Only create a copy if there is more than 1 process referring to the
    // page
    if (get_ref(pa) > 1) {
      char *mem;
      if ((mem = kalloc()) == 0) {
        return naughty_process(p, "COW ran out of memory");
      }
      memmove(mem, (char *)pa, PGSIZE);

      flags &= ~PTE_P;
      flags |= PTE_W;
      uvmunmap(p->pagetable, faulted_addr, 1, 1);
      if (mappages(p->pagetable, faulted_addr, PGSIZE, (uint64)mem, flags) ==
          -1) {
        kfree(mem);
        return naughty_process(p, "Could not map page for forked process");
      }
    } else {
      *pte &= ~PTE_P;
      *pte |= PTE_W;
    }
  }
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void) {
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  uint64 scause = r_scause();

  if (scause == 8) {
    // system call

    if (killed(p)) exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if (scause == 15) {
    handle_cow(p);
  } else if ((which_dev = devintr()) != 0) {
    // ok
  } else {
    backtrace();
    naughty_process(p, "unexpected scause");
  }

  if (killed(p)) exit(-1);

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2) {
    if (p->alarm_interval > 0 && p->alarm_returned == 1 &&
        ticks - p->alarm_called_at >= (uint)p->alarm_interval) {
      p->alarm_returned = 0;
      p->alarm_called_at = ticks;
      p->saved_trapframe = *p->trapframe;
      p->trapframe->epc = p->alarm_handler;
    }
    yield();
  }

  usertrapret();
}

//
// return to user space
//
void usertrapret(void) {
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();          // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE;  // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();  // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap() {
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0) panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0) {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr() {
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
int devintr() {
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ) {
      uartintr();
    } else if (irq == VIRTIO0_IRQ) {
      virtio_disk_intr();
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq) plic_complete(irq);

    return 1;
  } else if (scause == 0x8000000000000001L) {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0) {
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

uint64 sigalarm(int interval, uint64 handler_addr) {
  struct proc *p = myproc();
  if (p) {
    p->alarm_interval = interval;
    p->alarm_handler = handler_addr;
    p->alarm_called_at = 0;
  }
  return 0;
}

uint64 sigreturn(void) {
  struct proc *p = myproc();
  if (p) {
    p->alarm_returned = 1;
    *p->trapframe = p->saved_trapframe;
  }
  return p->trapframe->a0;
}
