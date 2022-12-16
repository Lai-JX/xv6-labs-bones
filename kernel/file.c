//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

struct {
  struct spinlock lock;
  struct VMA vmas[NVMA];
} vmatable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

void
vmainit(void)
{
  initlock(&vmatable.lock, "vmatable");
}

// Allocate a vma structure.
struct VMA*
vmaalloc(void)
{
  int i;
  acquire(&vmatable.lock);
  for (i = 0; i < NVMA; i++)
  {
    if (vmatable.vmas[i].valid == 1)
      continue;
    break;
  }
  if (i == NVMA)
    panic("vmaalloc!\n");

  vmatable.vmas[i].valid = 1;
  release(&vmatable.lock);

  return vmatable.vmas + i;
}

void
deallocvma(struct VMA* vma)
{
  acquire(&vmatable.lock);
  vma->valid = 0;
  release(&vmatable.lock);
}

// 处理缺页错误，为映射文件空间分配物理页
int mmaplazy(uint64 va, uint64 cause)
{
  struct proc *p = myproc();
  struct VMA *vma = 0;
  // printf("va:%p\n", va);
  // 寻址对应的vma
  for (vma = p->vmalist; vma; vma = vma->next)
  {
    // printf("vmstart:%p\n", vma->vmstart);
    // printf("vmend:%p\n", vma->vmend);
    if (vma->valid && va >= vma->vmstart && va < vma->vmend)
      break;
  }

  if (!vma)
    return -1;

  // 读缺页
  if (cause == 13 && !(vma->perm & PTE_R))
    return -1;

  // 写缺页
  if (cause == 15 && !(vma->perm & PTE_W))
    return -1;

  // 分配一页物理内存
  char *mem = kalloc();   
  if(!mem)
    return -1;

  memset(mem, 0, PGSIZE);

  // 建立映射
  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, vma->perm|PTE_X|PTE_U) != 0){
    kfree(mem);
    uvmunmap(p->pagetable, PGROUNDDOWN(va), 1, 1); // 第一个1表示解除1页的映射，第2个1表示清除对应空间
    return -1;
  }

  // 读取文件到物理内存 调用readi之前需要先上锁 (mem是内核地址，同时是物理地址；vma->vmstart是用户地址，同时是虚拟地址)
  ilock(vma->file->ip);
  readi(vma->file->ip, 0, (uint64)mem, PGROUNDDOWN(va)-(vma->vmstart), PGSIZE); // 倒数第二个参数应为要读取的数据所在块的起始地址在文件的偏移量
  iunlock(vma->file->ip);
 
  return 0;
}