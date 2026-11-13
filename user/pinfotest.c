// pinfotest.c - checks part 1: the getpinfo system call and the counters
// (runticks, nsched, ctime). run it with one CPU:  make CPUS=1 qemu

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/pinfo.h"
#include "user/user.h"

struct pinfo buf[NPROC + 1];   // global so it's not on the small stack
int failures = 0;

// print ok/FAIL for one check and count the failures
void
check(int passed, char *what)
{
  if (passed) {
    printf("ok:   %s\n", what);
  } else {
    printf("FAIL: %s\n", what);
    failures = failures + 1;
  }
}

// look up one process by pid. returns 0 and fills *out, or -1 if not found
int
lookup(int pid, struct pinfo *out)
{
  int n = getpinfo(buf, NPROC);
  for (int i = 0; i < n; i++) {
    if (buf[i].pid == pid) {
      *out = buf[i];
      return 0;
    }
  }
  return -1;
}

int
main(int argc, char *argv[])
{
  struct pinfo before, after, kid;
  int me = getpid();

  // 1. the call itself
  int n = getpinfo(buf, NPROC);
  check(n >= 3, "getpinfo finds at least 3 processes");
  check(buf[0].pid == 1, "first entry is init (proc[] order)");
  check(lookup(me, &before) == 0, "our own pid is in the list");
  check(before.state == 4, "we show up as running");
  check(strcmp(before.name, "pinfotest") == 0, "our name is right");
  check(before.level >= 0 && before.level <= 3, "level is between 0 and 3");
  check(before.ctime >= 0 && before.ctime <= uptime(), "ctime is not in the future");

  // 2. max and bad arguments
  buf[1].pid = 12345;   // marker, should survive a call with max = 1
  check(getpinfo(buf, 1) == 1, "max = 1 returns 1");
  check(buf[1].pid == 12345, "max = 1 doesn't write a second entry");
  check(getpinfo(buf, 0) == 0, "max = 0 returns 0");
  check(getpinfo(buf, -1) == -1, "max < 0 returns -1");
  check(getpinfo((struct pinfo *)0xffffffffffUL, NPROC) == -1, "bad pointer returns -1");

  // 3. spin for 20 ticks: we should get charged about 20 ticks,
  //    and the scheduler should have switched to us at least once more
  lookup(me, &before);
  int start = uptime();
  while (uptime() - start < 20)
    ;
  lookup(me, &after);
  int charged = after.runticks - before.runticks;
  printf("      spun for 20 ticks, charged %d, nsched went %d -> %d\n",
         charged, before.nsched, after.nsched);
  check(charged >= 16 && charged <= 24, "spinning 20 ticks is charged about 20 runticks");
  check(after.nsched > before.nsched, "nsched grows");

  // 4. a child starts with fresh counters, not a copy of ours
  int t0 = uptime();
  int pid = fork();
  if (pid == 0) {
    pause(20);   // child: just stay alive long enough to be looked at
    exit(0);
  }
  check(lookup(pid, &kid) == 0, "child is in the list");
  lookup(me, &after);
  printf("      parent runticks %d, child runticks %d nsched %d level %d ctime %d\n",
         after.runticks, kid.runticks, kid.nsched, kid.level, kid.ctime);
  check(kid.runticks <= 1, "child's runticks start at 0");
  check(kid.nsched <= 2, "child's nsched starts at 0");
  check(kid.level == 0, "child starts at level 0");
  check(kid.ctime >= t0 && kid.ctime <= uptime(), "child's ctime is when it was created");
  kill(pid);
  wait(0);

  if (failures == 0) {
    printf("pinfotest: OK\n");
    exit(0);
  }
  printf("pinfotest: %d FAILED\n", failures);
  exit(1);
}
