// pstat.c - show every process with its scheduling stats (uses getpinfo)
// usage: pstat

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"   // NPROC
#include "kernel/pinfo.h"
#include "user/user.h"

// global, not a local: 64 entries is too big for the 4KB user stack
struct pinfo procs[NPROC];

// same order as enum procstate in kernel/proc.h
char *
state_name(int state)
{
  if (state == 1)
    return "used";
  if (state == 2)
    return "sleeping";
  if (state == 3)
    return "runnable";
  if (state == 4)
    return "running";
  if (state == 5)
    return "zombie";
  return "unknown";
}

int
main(int argc, char *argv[])
{
  int n = getpinfo(procs, NPROC);
  if (n < 0) {
    fprintf(2, "pstat: getpinfo failed\n");
    exit(1);
  }

  printf("PID\tLEVEL\tRUNTICKS\tNSCHED\tCTIME\tSTATE\t\tNAME\n");
  for (int i = 0; i < n; i++) {
    printf("%d\t%d\t%d\t\t%d\t%d\t%s\t%s\n", procs[i].pid, procs[i].level,
           procs[i].runticks, procs[i].nsched, procs[i].ctime,
           state_name(procs[i].state), procs[i].name);
  }
  exit(0);
}
