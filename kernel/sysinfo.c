#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sysinfo.h"
#include "proc.h"

uint64
sys_sysinfo(void)
{
  uint64 ptr;
  struct proc *p = myproc();
  struct sysinfo info;
  if (argaddr(0, &ptr) < 0)
    return -1;

  info.nproc   = countusedproc();
  info.freemem = countfreemem();

  if (copyout(p->pagetable, ptr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
