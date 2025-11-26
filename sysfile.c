//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

#define CHECKPOINT_HEADER_ID 0xDEADBEEF

struct trapframe {
  uint edi;
  uint esi;
  uint ebp;
  uint oesp;
  uint ebx;
  uint edx;
  uint ecx;
  uint eax;
  ushort gs;
  ushort padding1;
  ushort fs;
  ushort padding2;
  ushort es;
  ushort padding3;
  ushort ds;
  ushort padding4;
  uint trapno;
  uint err;
  uint eip;
  ushort cs;
  ushort padding5;
  uint eflags;
  uint esp;
  ushort ss;
  ushort padding6;
};

struct check_point_header {
  uint id;
  int pid;
  uint sz;
  char name[16];
  struct trapframe tf;
};

extern pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}

// this function copies data from the page table 
int
copy_from_pgdir(pde_t *pgdir, void *dst_k, uint src_va, uint len)
{
  uint va = src_va;
  uint end = src_va + len;
  char *d = (char*)dst_k;
  
  while(va < end){
    uint va0 = (uint)PGROUNDDOWN(va);
    pte_t *pte = walkpgdir(pgdir, (void*)va0, 0);
    
    if(pte == 0 || (*pte & PTE_P) == 0){
        memset(d, 0, end - va);
        break; 
    }
    
    uint pa = PTE_ADDR(*pte);
    uint offset = va - va0;
    uint chunk = PGSIZE - offset;
    if(chunk > (end - va)) chunk = end - va;
    
    char *src_k = (char*)P2V(pa);
    memmove(d, src_k + offset, chunk);
  
    va += chunk;
    d += chunk;
  }
  return 0;
}

int
sys_checkpoint(void)
{
  int pid;
  char *filename;
  struct proc *p;
  struct inode *ip;
  struct check_point_header hdr;
  
  if(argint(0, &pid) < 0 || argstr(1, &filename) < 0)
    return -1;

  p = findproc(pid);
  if(p == 0){
      cprintf("Checkpoint: PID %d not found.\n", pid);
      return -1;
  }

  hdr.id = CHECKPOINT_HEADER_ID;
  hdr.pid = p->pid;
  hdr.sz = p->sz;
  hdr.tf = *p->tf;
  safestrcpy(hdr.name, p->name, sizeof(hdr.name));


  begin_op();
  if((ip = create(filename, 2, 0, 0)) == 0){
    end_op();
    return -1;
  }
  if(writei(ip, (char*)&hdr, 0, sizeof(hdr)) != sizeof(hdr)){
    iunlockput(ip);
    end_op();
    return -1;
  }
  end_op(); 

  // save Memory
  int written = 0;
  uint addr = 0;
  uint size = p->sz;
  
  while(written < size){
    int n = size - written;
    if(n > 1024) n = 1024; 
    
    char kbuffer[1024]; 

    // CRITICAL CHANGE:
    // to save self use memmove else we use copy_from_pgdir helper function
    if(p == myproc()){
        memmove(kbuffer, (void*)addr, n);
    } else {
        copy_from_pgdir(p->pgdir, kbuffer, addr, n);
    }
    
    begin_op();
    if(writei(ip, kbuffer, sizeof(hdr) + written, n) != n){
         cprintf("Checkpoint: write failed\n");
         iunlockput(ip);
         end_op();
         return -1;
    }
    end_op();
    
    written += n;
    addr += n;
  }

  begin_op();
  iunlockput(ip);
  end_op();
  
  return 0;
}

int
sys_restart(void)
{
  char *path;
  struct inode *ip;
  struct check_point_header hdr;
  struct proc *p = myproc();
  
  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Read Header
  if(readi(ip, (char*)&hdr, 0, sizeof(hdr)) != sizeof(hdr)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(hdr.id != CHECKPOINT_HEADER_ID){
      cprintf("Restart Error: File is not a valid checkpoint image.\n");
      iunlockput(ip);
      end_op();
      return -1;
  }

  // Resize process memory to match saved state
  if(hdr.sz > p->sz){
      if((p->sz = allocuvm(p->pgdir, p->sz, hdr.sz)) == 0){
          iunlockput(ip);
          end_op();
          return -1;
      }
  } else if(hdr.sz < p->sz){
      p->sz = deallocuvm(p->pgdir, p->sz, hdr.sz);
  }
  
  int read_bytes = 0;
  uint addr = 0;
  uint size = hdr.sz;
  
  while(read_bytes < size){
      char kbuffer[1024];
      int n = size - read_bytes;
      if(n > sizeof(kbuffer)) n = sizeof(kbuffer);
      
      // Read from file to kernel buffer
      if(readi(ip, kbuffer, sizeof(hdr) + read_bytes, n) != n){
          cprintf("Restart: read failed\n");
          iunlockput(ip);
          end_op();
          return -1;
      }

      // Copy from kernel buffer to user memory
      memmove((void*)addr, kbuffer, n);
      
      read_bytes += n;
      addr += n;
  }

  // Restore CPU state
  *p->tf = hdr.tf;
  
  // Set return value of the syscall to 0 to indicate success
  p->tf->eax = 0; 

  iunlockput(ip);
  end_op();
  
  return 0;
}