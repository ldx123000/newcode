/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "sched.h"
#include "file.h"
#include "spike_interface/spike_utils.h"

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

//
// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point
// of S-mode trap vector).
//
extern char trap_sec_start[];

//
// global variable that store the recorded "ticks" in strap.c
extern uint64 g_ticks;
// g_mem_size is defined in spike_interface/spike_memory.c, it indicates the size of our
// (emulated) spike machine.
extern uint64 g_mem_size;

// current points to the currently running user-mode application.
process* current = NULL;

// process pool
process procs[NPROC];

// start virtual address of our simple heap.
uint64 g_ufree_page = USER_FREE_ADDRESS_START;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  write_csr(stvec, (uint64)smode_trap_vector);
  // set up trapframe values that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // set up the registers that strap_vector.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  write_csr(sstatus, x);

  // set S Exception Program Counter to the saved user pc.
  write_csr(sepc, proc->trapframe->epc);

  //make user page table
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // switch to user mode with sret.
  return_to_user(proc->trapframe, user_satp);
}

//
// initialize process pool (the procs[] array)
//
void init_proc_pool() {
  memset( procs, 0, sizeof(struct process)*NPROC );

  for (int i = 0; i < NPROC; ++i) {
    procs[i].status = FREE;
    procs[i].pid = i;
    procs[i].tick_count = 0;
    procs[i].total_tick_count = 0;
    procs[i].total_mem_count = 0;
  }
}

//
// allocate an empty process, init its vm space. returns its pid
//
process* alloc_process() {
  // locate the first usable process structure
  int i;

  for( i=0; i<NPROC; i++ )
    if( procs[i].status == FREE ) break;

  if( i>=NPROC ){
    panic( "cannot find any free process structure.\n" );
    return 0;
  }

  procs[i].total_mem_count = 0;

  // init proc[i]'s vm space
  procs[i].trapframe = (trapframe *)alloc_page();  //trapframe, used to save context
  memset(procs[i].trapframe, 0, sizeof(trapframe));

  // page directory
  procs[i].pagetable = (pagetable_t)alloc_page();
  memset((void *)procs[i].pagetable, 0, PGSIZE);

  procs[i].kstack = (uint64)alloc_page() + PGSIZE;   //user kernel stack top
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  procs[i].trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  procs[i].mapped_info = (mapped_region*)alloc_page();
  memset( procs[i].mapped_info, 0, PGSIZE );

  // map user stack in userspace
  user_vm_map((pagetable_t)procs[i].pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  procs[i].mapped_info[0].va = USER_STACK_TOP - PGSIZE;
  procs[i].mapped_info[0].npages = 1;
  procs[i].mapped_info[0].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)procs[i].trapframe, PGSIZE,
    (uint64)procs[i].trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  procs[i].mapped_info[1].va = (uint64)procs[i].trapframe;
  procs[i].mapped_info[1].npages = 1;
  procs[i].mapped_info[1].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  procs[i].mapped_info[2].va = (uint64)trap_sec_start;
  procs[i].mapped_info[2].npages = 1;
  procs[i].mapped_info[2].seg_type = SYSTEM_SEGMENT;

  sprint("in alloc_proc. user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
    procs[i].trapframe, procs[i].trapframe->regs.sp, procs[i].kstack);

  procs[i].total_mapped_region = 3;

  procs[i].total_tick_count = 0;
  procs[i].tick_count = 0;

  // initialize files_struct
  procs[i].pfiles = files_create();
  sprint("in alloc_proc. build files_struct successfully.\n");
  
  // return after initialization.
  return &procs[i];
}

//
// reclaim a process
//
int free_process( process* proc ) {
  // we set the status to ZOMBIE, but cannot destruct its vm space immediately.
  // since proc can be current process, and its user kernel stack is currently in use!
  // but for proxy kernel, it (memory leaking) may NOT be a really serious issue,
  // as it is different from regular OS, which needs to run 7x24.
  proc->status = ZOMBIE;

  return 0;
}

//
// reallocate a process
// 
void realloc_process(int i) {
  // 1. free procs[i]
  for( int j=0; j<procs[i].total_mapped_region; ++ j ){
    switch( procs[i].mapped_info[j].seg_type ){
      case STACK_SEGMENT:   // free user stack
      case CONTEXT_SEGMENT: // free trapframe
      case DATA_SEGMENT:    // free data segment
        user_vm_unmap(procs[i].pagetable, 
                      procs[i].mapped_info[j].va, 
                      procs[i].mapped_info[j].npages*PGSIZE, 
                      1);   // 取消映射并释放物理页
        break;
      case CODE_SEGMENT:
        user_vm_unmap(procs[i].pagetable, 
                      procs[i].mapped_info[j].va,
                      procs[i].mapped_info[j].npages*PGSIZE,
                      0);   // 只取消映射，不释放代码段物理页
        break;
    }
  }
  free_page(procs[i].pagetable);

  // 2. alloc proc[i]
  // init proc[i]'s vm space
  procs[i].trapframe = (trapframe *)alloc_page();  //trapframe, used to save context
  memset(procs[i].trapframe, 0, sizeof(trapframe));

  // page directory
  procs[i].pagetable = (pagetable_t)alloc_page();
  memset((void *)procs[i].pagetable, 0, PGSIZE);

  // procs[i].kstack = (uint64)alloc_page() + PGSIZE;   //user kernel stack top
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  procs[i].trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  // procs[i].mapped_info = (mapped_region*)alloc_page();
  memset( procs[i].mapped_info, 0, PGSIZE );

  // map user stack in userspace
  user_vm_map((pagetable_t)procs[i].pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  procs[i].mapped_info[0].va = USER_STACK_TOP - PGSIZE;
  procs[i].mapped_info[0].npages = 1;
  procs[i].mapped_info[0].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)procs[i].trapframe, PGSIZE,
    (uint64)procs[i].trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  procs[i].mapped_info[1].va = (uint64)procs[i].trapframe;
  procs[i].mapped_info[1].npages = 1;
  procs[i].mapped_info[1].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  procs[i].mapped_info[2].va = (uint64)trap_sec_start;
  procs[i].mapped_info[2].npages = 1;
  procs[i].mapped_info[2].seg_type = SYSTEM_SEGMENT;

  sprint("in alloc_proc. user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
    procs[i].trapframe, procs[i].trapframe->regs.sp, procs[i].kstack);

  procs[i].total_mapped_region = 3;
  procs[i].tick_count = 0;
  procs[i].total_tick_count = 0;
  procs[i].total_mem_count = 3;
  return;
}

//
// implements fork syscal in kernel.
// basic idea here is to first allocate an empty process (child), then duplicate the
// context and data segments of parent process to the child, and lastly, map other
// segments (code, system) of the parent to child. the stack segment remains unchanged
// for the child.
//
int do_fork( process* parent)
{
  sprint( "will fork a child from parent %d.\n", parent->pid );
  process* child = alloc_process();

  for( int i=0; i<parent->total_mapped_region; i++ ){
    // browse parent's vm space, and copy its trapframe and data segments,
    // map its code segment.
    switch( parent->mapped_info[i].seg_type ){
      case CONTEXT_SEGMENT:
        *child->trapframe = *parent->trapframe;
        break;
      case STACK_SEGMENT:
        memcpy( (void*)lookup_pa(child->pagetable, child->mapped_info[0].va),
          (void*)lookup_pa(parent->pagetable, parent->mapped_info[i].va), PGSIZE );
        break;
      case CODE_SEGMENT:
        for( int j=0; j<parent->mapped_info[i].npages; j++ ){
          uint64 addr = lookup_pa(parent->pagetable, parent->mapped_info[i].va+j*PGSIZE);

          map_pages(child->pagetable, parent->mapped_info[i].va+j*PGSIZE, PGSIZE,
            addr, prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

          sprint( "do_fork map code segment at pa:%lx of parent to child at va:%lx.\n",
            addr, parent->mapped_info[i].va+j*PGSIZE );
        }
        // after mapping, register the vm region (do not delete codes below!)
        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages = 
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = CODE_SEGMENT;
        child->total_mapped_region++;
        break;
      case DATA_SEGMENT:
        for ( int j = 0; j < parent->mapped_info[i].npages; ++ j ){
          // 1. alloc pages for data segment
          uint64 pa = (uint64)alloc_page();
          uint64 addr = lookup_pa(parent->pagetable, parent->mapped_info[i].va+j*PGSIZE);
          memcpy((void *)pa, (void *)addr, PGSIZE);
          // 2. map the va -> pa
          map_pages(child->pagetable, 
                    parent->mapped_info[i].va+j*PGSIZE, 
                    PGSIZE,
                    pa, 
                    prot_to_type(PROT_WRITE | PROT_READ, 1));
        }
        // 3. copy the data segment info
        child->mapped_info[child->total_mapped_region].va = 
          parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages = 
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = DATA_SEGMENT;
        ++ child->total_mapped_region;
        break;
    }
  }

  child->status = READY;
  child->trapframe->regs.a0 = 0;
  child->parent = parent;

  child->tick_count = 0;
  child->total_tick_count = 0;
  child->total_mem_count = child->total_mapped_region;
  insert_to_ready_queue( child );

  return child->pid;
}

int do_wait(int pid){
  int havekids, child_pid;
  havekids = 0;
  // Scan through table looking for zombie children.
  for ( int i = 0; i < NPROC; ++ i ){
    if ( procs[i].parent != current ) // not my children
      continue;
    // my child but not that wanted
    if ( pid > 0 && pid != procs[i].pid )
      continue;
    // find a valid child
    havekids = 1;
    if ( procs[i].status == ZOMBIE ){

      child_pid = procs[i].pid;

      free_page((void*)procs[i].kstack-PGSIZE);  // 释放 kstack

      for( int j=0; j<procs[i].total_mapped_region; ++ j ){
        switch( procs[i].mapped_info[j].seg_type ){
          case STACK_SEGMENT:   // free user stack
          case CONTEXT_SEGMENT: // free trapframe
          case DATA_SEGMENT:    // free data segment
            user_vm_unmap(procs[i].pagetable, 
                          procs[i].mapped_info[j].va, 
                          procs[i].mapped_info[j].npages*PGSIZE, 
                          1);   // 取消映射并释放物理页
            break;
          case CODE_SEGMENT:
            user_vm_unmap(procs[i].pagetable, 
                          procs[i].mapped_info[j].va,
                          procs[i].mapped_info[j].npages*PGSIZE,
                          0);   // 只取消映射，不释放代码段物理页
            break;
        }
      }
      free_page(procs[i].mapped_info);
      free_page(procs[i].pagetable);
      procs[i].status = FREE;
      procs[i].parent = NULL;
      procs[i].queue_next = NULL;
      procs[i].tick_count = 0;
      procs[i].total_mem_count = 0;
      procs[i].total_tick_count = 0;
      return child_pid;
    }
  }

  if ( ! havekids ){
    return -1;
  }

  return -2;
}

int do_exec(char * path, char ** argv){
  load_shell_bincode_from_host_elf(user_va_to_pa(current->pagetable, argv));
  return 1; 
}

int do_getinfo(){
  sprint("top - \n");
  // process status
  int nprocs[5] = {0};
  for ( int i = 0; i < NPROC; ++ i ){
    if ( procs[i].status != FREE ){
      ++ nprocs[0];
      ++ nprocs[procs[i].status];
    }
  }
  sprint("Tasks: %d total, %d ready, %d running, %d blocked, %d zombie\n", 
    nprocs[0], nprocs[READY], nprocs[RUNNING], nprocs[BLOCKED], nprocs[ZOMBIE]);

  sprint("Cpu(s): %d ticks\n", g_ticks);

  sprint("KiB Mem: %d\n", (g_mem_size >> 10));

  sprint("\nPID\tS\tMEM\tTICK\n");
  int pid, tick, mem;
  char stat;
  for ( int i = 0; i < NPROC; ++ i ){
    if ( procs[i].status != FREE ){
      pid = procs[i].pid;
      stat = '/';
      switch ( procs[i].status ){
        case READY:   stat = 'S'; break;
        case RUNNING: stat = 'R'; break;
        case BLOCKED: stat = 'B'; break;
        case ZOMBIE:  stat = 'Z'; break;
      }
      mem = procs[i].total_mem_count * 4;
      tick = procs[i].total_tick_count;
      sprint("%d\t%c\t%d\t%d\n", pid, stat, mem, tick);
    }
  }
  return 1;
}