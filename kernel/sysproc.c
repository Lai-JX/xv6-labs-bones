#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
  /*lab traps 👇*/
  backtrace();
  /*lab traps 👆*/
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

/* lab traps 👇 */
uint64
sys_sigalarm(void)
{
  // 在系统调用时，进行初始化
  int ticks;
  uint64 handler;
  struct proc *p = myproc();
  // 获取参数
  if (argint(0,&ticks) < 0)
    return -1;
  if (argaddr(1,&handler) < 0)
    return -1;
  acquire(&p->lock);
  p->ticks = ticks;       // 两次alarm间隔的时钟周期数
  p->handler = handler;   // handler处理函数的地址
  p->cur_ticks = 0;       // 初始化为0
  release(&p->lock);
  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  if (p->save_trapframe)
  {
    // 恢复之前保存的寄存器
    memmove(p->trapframe, p->save_trapframe, PGSIZE);
    kfree(p->save_trapframe);
    p->save_trapframe = 0;
  }
  release(&p->lock);
  return 0;
}
