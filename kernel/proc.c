#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct task thread[NTASK];

struct task *initproc;

int nexttid = 1;
struct spinlock tid_lock;

extern void forkret(void);
static void freetask(struct task *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using t->parent.
// must be acquired before any t->thread_lock.
struct spinlock wait_lock;

// Allocate a page for each thread's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
task_mapstacks(pagetable_t kpgtbl)
{
  struct task *t;
  
  for(t = thread; t < &thread[NTASK]; t++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (t - thread));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the thread table.
void
threadinit(void)
{
  struct task *p;
  
  initlock(&tid_lock, "nexttid");
  initlock(&wait_lock, "wait_lock");
  for(p = thread; p < &thread[NTASK]; p++) {
      initlock(&p->thread_lock, "thread");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - thread));
  }
}

// Must be called with interrupts disabled,
// to prevent race with thread being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct task *, or zero if none.
struct task*
mytask(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct task *p = c->thread;
  pop_off();
  return p;
}

int
alloctid()
{
  int tid;
  
  acquire(&tid_lock);
  tid = nexttid;
  nexttid = nexttid + 1;
  release(&tid_lock);

  return tid;
}

// Look in the thread table for an UNUSED thread.
// If found, initialize state required to run in the kernel,
// and return with t->thread_lock held.
// If there are no free threads, or a memory allocation fails, return 0.
static struct task*
alloctask(void)
{
  struct task *t;

  for(t = thread; t < &thread[NTASK]; t++) {
    acquire(&t->thread_lock);
    if(t->state == UNUSED) {
      goto found;
    } else {
      release(&t->thread_lock);
    }
  }
  return 0;

found:
  t->tid = alloctid();
  t->pid = t->tid;
  t->state = USED;

  // Allocate a trapframe page.
  if((t->trapframe = (struct trapframe *)kalloc()) == 0){
    freetask(t);
    release(&t->thread_lock);
    return 0;
  }

  // An empty user page table.
  t->pagetable = thread_pagetable(t);
  if(t->pagetable == 0){
    freetask(t);
    release(&t->thread_lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = t->kstack + PGSIZE;

  return t;
}

// free a task structure and the data hanging from it,
// including user pages.
// t->thread_lock must be held.
static void
freetask(struct task *t)
{
  if(t->trapframe)
    kfree((void*)t->trapframe);
  t->trapframe = 0;
  if(t->pagetable)
    thread_freepagetable(t->pagetable, t->sz);
  t->pagetable = 0;
  t->sz = 0;
  t->pid = 0;
  t->tid = 0;
  t->parent = 0;
  t->name[0] = 0;
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->state = UNUSED;
}

// Create a user page table for a given thread, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
thread_pagetable(struct task *t)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(t->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a thread's page table, and free the
// physical memory it refers to.
void
thread_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct task *t;

  t = alloctask();
  initproc = t;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(t->pagetable, initcode, sizeof(initcode));
  t->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  t->trapframe->epc = 0;      // user program counter
  t->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(t->name, "initcode", sizeof(t->name));
  t->cwd = namei("/");

  t->state = RUNNABLE;

  release(&t->thread_lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct task *t = mytask();

  sz = t->sz;
  if(n > 0){
    if((sz = uvmalloc(t->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(t->pagetable, sz, sz + n);
  }
  t->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct task *np;
  struct task *p = mytask();

  // Allocate process.
  if((np = alloctask()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freetask(np);
    release(&np->thread_lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->thread_lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->thread_lock);
  np->state = RUNNABLE;
  release(&np->thread_lock);

  return pid;
}

// Create a new thread, copying the parent.
// Child starts execution by calling the function passed.
int
clone(void)
{
  int i, tid;
  struct task *nt;
  struct task *t = mytask();

  // Allocate thread.
  if((nt = alloctask()) == 0){
    return -1;
  }

  // Clone user memory from parent to child.
  if(uvmclone(t->pagetable, nt->pagetable, 0, t->sz) < 0){
    freetask(nt);
    release(&nt->thread_lock);
    return -1;
  }

  nt->sz = t->sz;
  nt->pid = t->pid;

  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  // Cause clone to return 0 in the child.
  nt->trapframe->a0 = 0;

  // Allocate a page for the child's stack
  pte_t *pte;
  uint64 sp = nt->trapframe->sp;
  if ((pte = walk(nt->pagetable, PGROUNDDOWN(sp), 0)) == 0)
    panic("clone: stack pte should exist");
  if ((*pte & PTE_V) == 0)
    panic("clone: stack page not present");
  uint64 pa = PTE2PA(*pte);
  uint flags = PTE_FLAGS(*pte);
  char *mem;
  if ((mem = kalloc()) == 0) {
    freetask(nt);
    release(&nt->thread_lock);
    return -1;
  }
  memmove(mem, (char *)pa, PGSIZE);
  kfree((void *)pa);
  *pte = PA2PTE(mem) | flags | PTE_V;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(t->ofile[i])
      nt->ofile[i] = filedup(t->ofile[i]);
  nt->cwd = idup(t->cwd);

  safestrcpy(nt->name, t->name, sizeof(t->name));

  tid = nt->tid;

  release(&nt->thread_lock);

  acquire(&wait_lock);
  nt->parent = t;
  release(&wait_lock);

  acquire(&nt->thread_lock);
  nt->state = RUNNABLE;
  release(&nt->thread_lock);

  return tid;
}

// Pass t's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct task *t)
{
  struct task *tt;

  for(tt = thread; tt < &thread[NTASK]; tt++){
    if(tt->parent == t){
      tt->parent = initproc;
      tt->pid = initproc->pid;
      wakeup(initproc);
    }
  }
}

// Exit the current thread.  Does not return.
// An exited thread remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct task *t = mytask();

  if(t == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(t->ofile[fd]){
      struct file *f = t->ofile[fd];
      fileclose(f);
      t->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(t->cwd);
  end_op();
  t->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(t);

  // Parent might be sleeping in wait().
  wakeup(t->parent);
  
  acquire(&t->thread_lock);

  t->xstate = status;
  t->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct task *pp;
  int havekids, pid;
  struct task *p = mytask();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = thread; pp < &thread[NTASK]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->thread_lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->thread_lock);
            release(&wait_lock);
            return -1;
          }
          freetask(pp);
          release(&pp->thread_lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->thread_lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU thread scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a thread to run.
//  - swtch to start running that thread.
//  - eventually that thread transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct task *t;
  struct cpu *c = mycpu();
  
  c->thread = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(t = thread; t < &thread[NTASK]; t++) {
      acquire(&t->thread_lock);
      if(t->state == RUNNABLE) {
        // Switch to chosen thread.  It is the thread's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        t->state = RUNNING;
        c->thread = t;
        swtch(&c->context, &t->context);

        // Thread is done running for now.
        // It should have changed its t->state before coming back.
        c->thread = 0;
      }
      release(&t->thread_lock);
    }
  }
}

// Switch to scheduler.  Must hold only t->thread_lock
// and have changed t->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be task->intena and task->noff, but that would
// break in the few places where a lock is held but
// there's no task.
void
sched(void)
{
  int intena;
  struct task *t = mytask();

  if(!holding(&t->thread_lock))
    panic("sched t->thread_lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(t->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct task *t = mytask();
  acquire(&t->thread_lock);
  t->state = RUNNABLE;
  sched();
  release(&t->thread_lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->thread_lock from scheduler.
  release(&mytask()->thread_lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct task *t = mytask();
  
  // Must acquire t->thread_lock in order to
  // change t->state and then call sched.
  // Once we hold t->thread_lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks t->thread_lock),
  // so it's okay to release lk.

  acquire(&t->thread_lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->thread_lock);
  acquire(lk);
}

// Wake up all threads sleeping on chan.
// Must be called without any t->thread_lock.
void
wakeup(void *chan)
{
  struct task *t;

  for(t = thread; t < &thread[NTASK]; t++) {
    if(t != mytask()){
      acquire(&t->thread_lock);
      if(t->state == SLEEPING && t->chan == chan) {
        t->state = RUNNABLE;
      }
      release(&t->thread_lock);
    }
  }
}

// Kill the thread with the given tid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
// TODO: replace passed pid to tid
int
kill(int tid)
{
  struct task *p;

  for(p = thread; p < &thread[NTASK]; p++){
    acquire(&p->thread_lock);
    if(p->tid == tid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->thread_lock);
      return 0;
    }
    release(&p->thread_lock);
  }
  return -1;
}

void
setkilled(struct task *t)
{
  acquire(&t->thread_lock);
  t->killed = 1;
  release(&t->thread_lock);
}

int
killed(struct task *t)
{
  int k;
  
  acquire(&t->thread_lock);
  k = t->killed;
  release(&t->thread_lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct task *t = mytask();
  if(user_dst){
    return copyout(t->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct task *t = mytask();
  if(user_src){
    return copyin(t->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
// TODO: print processes only
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct task *p;
  char *state;

  printf("\n");
  for(p = thread; p < &thread[NTASK]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
