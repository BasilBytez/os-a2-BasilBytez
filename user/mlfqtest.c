// mlfqtest.c - checks part 2: the MLFQ scheduler rules
// usage: mlfqtest          rule checks. needs ONE cpu:   make CPUS=1 qemu
//        mlfqtest stress   lots of forking/sleeping/exiting at once.
//                          run this one with the default 3 cpus:  make qemu

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/pinfo.h"
#include "user/user.h"

struct pinfo buf[NPROC];   // global so it's not on the small stack
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

// the level a process should be at after using this many ticks since it
// started (or since the last boost): slices are 1, 2, 4, 8
int
level_for(int runticks)
{
  if (runticks < 1)
    return 0;
  if (runticks < 3)
    return 1;
  if (runticks < 7)
    return 2;
  return 3;
}

// wait until just after a boost AND until this test process is back at
// level 0. that gives the next ~55 ticks with no boost in them, and it means
// we always get the cpu at the next tick when we wake up from a sleep
void
quiet(void)
{
  struct pinfo me;
  for (;;) {
    int u = uptime() % 64;
    lookup(getpid(), &me);
    if (u >= 1 && u <= 4 && me.level == 0)
      break;
    pause(1);
  }
}

// start a child that burns cpu forever. returns its pid
int
start_hog(void)
{
  int pid = fork();
  if (pid < 0) {
    printf("mlfqtest: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    for (;;)
      ;
  }
  return pid;
}

// kill a child and clean it up
void
stop(int pid)
{
  kill(pid);
  wait(0);
}

// rules 3, 4, 5: a hog starts at level 0 and drops a level each time it
// uses up a slice
void
test_demotion(void)
{
  struct pinfo pi;
  printf("test 1: a cpu hog is demoted 0 -> 1 -> 2 -> 3\n");
  quiet();
  int pid = start_hog();
  int ok = 1;
  for (int i = 0; i < 12; i++) {
    pause(1);
    lookup(pid, &pi);
    printf("      runticks %d  level %d\n", pi.runticks, pi.level);
    if (pi.level != level_for(pi.runticks))
      ok = 0;
  }
  check(ok, "level matches ticks used (1 tick -> L1, 3 -> L2, 7 -> L3)");
  check(pi.level == 3, "the hog ends up at level 3 and stays there");
  stop(pid);
}

// rule 6: sleeping doesn't wipe out the time already used at a level
void
test_not_forgiven(void)
{
  struct pinfo pi;
  printf("test 2: sleeping before the slice ends doesn't avoid demotion\n");
  quiet();
  int pid = fork();
  if (pid == 0) {
    for (;;) {
      int t = uptime();
      while (uptime() == t)
        ;          // run until the next tick lands on us...
      pause(1);    // ...then give up the cpu for a tick
    }
  }
  int ok = 1;
  for (int i = 0; i < 30; i++) {
    pause(1);
    lookup(pid, &pi);
    if (pi.level != level_for(pi.runticks))
      ok = 0;
  }
  printf("      after 30 ticks: runticks %d  level %d\n", pi.runticks, pi.level);
  check(ok, "level still matches total ticks used, even with sleeps in between");
  check(pi.level == 3, "it still reaches level 3");
  stop(pid);
}

// rule 7 (second half): with nothing better to run, a process keeps the
// cpu for its whole slice instead of yielding every tick
void
test_keeps_running(void)
{
  int fds[2];
  int result[3];   // level, ticks charged, times switched to
  printf("test 3: a lone hog keeps the cpu for whole slices\n");
  quiet();
  pipe(fds);
  int pid = fork();
  if (pid == 0) {
    // the hog measures itself, so nothing else has to be awake
    struct pinfo a, b;
    int me = getpid();
    do {
      lookup(me, &a);   // burn cpu until we're down at level 3
    } while (a.level < 3);
    int t = uptime();
    while (uptime() - t < 32)
      ;                 // then burn 32 more ticks
    lookup(me, &b);
    result[0] = a.level;
    result[1] = b.runticks - a.runticks;
    result[2] = b.nsched - a.nsched;
    write(fds[1], result, sizeof(result));
    exit(0);
  }
  // wait by reading the pipe, NOT with pause(). a process in pause() wakes
  // up on every tick to check the time, and since we're at level 0 each of
  // those wake-ups would preempt the hog (rule 7)
  read(fds[0], result, sizeof(result));
  wait(0);
  close(fds[0]);
  close(fds[1]);
  printf("      in 32 ticks: charged %d, switched to %d times\n", result[1], result[2]);
  check(result[0] == 3, "hog is at level 3");
  check(result[1] >= 29 && result[1] <= 33, "it is charged about 32 of the 32 ticks");
  check(result[2] >= 3 && result[2] <= 6, "switched to about every 8 ticks, not every tick");
}

// rule 2: two processes at the same level take turns
void
test_round_robin(void)
{
  struct pinfo a1, a2, b1, b2;
  printf("test 4: two hogs at level 3 share the cpu\n");
  quiet();
  int p1 = start_hog();
  int p2 = start_hog();
  pause(18);   // each needs 7 ticks to reach level 3
  lookup(p1, &a1);
  lookup(p2, &a2);
  pause(30);
  lookup(p1, &b1);
  lookup(p2, &b2);
  int r1 = b1.runticks - a1.runticks;
  int r2 = b2.runticks - a2.runticks;
  printf("      in 30 ticks: hog A charged %d, hog B charged %d\n", r1, r2);
  check(a1.level == 3 && a2.level == 3, "both hogs are at level 3");
  check(r1 + r2 >= 27 && r1 + r2 <= 31, "together they use about all 30 ticks");
  check(r1 >= 8 && r2 >= 8, "each one gets a fair share");
  stop(p1);
  stop(p2);
}

// rules 1 and 7: a process at a higher level gets the cpu at the next tick
void
test_preempt(void)
{
  struct pinfo pi;
  printf("test 5: a level-0 process preempts a level-3 hog\n");
  quiet();
  int pid = start_hog();
  pause(10);
  lookup(pid, &pi);
  int t0 = uptime();
  for (int i = 0; i < 10; i++)
    pause(1);   // we wake up runnable at level 0 while the hog is running
  int took = uptime() - t0;
  printf("      10 one-tick sleeps took %d ticks\n", took);
  check(pi.level == 3, "hog is at level 3");
  check(took <= 12, "we get the cpu back within a tick each time");
  stop(pid);
}

// rule 8: every 64 ticks everything goes back to level 0
void
test_boost(void)
{
  struct pinfo a, b;
  printf("test 6: the boost every 64 ticks (this one waits for it)\n");
  quiet();
  int pid = start_hog();
  pause(10);
  int next = (uptime() / 64 + 1) * 64;   // the next boost tick
  pause(next - uptime() - 1);            // wake up one tick before it
  lookup(pid, &a);
  while (uptime() < next)
    pause(1);                            // sleep across the boost
  lookup(pid, &b);
  printf("      level before the boost %d, right after it %d\n", a.level, b.level);
  check(a.level == 3, "hog is at level 3 before the boost");
  check(b.level <= 1, "the boost sends it back to the top");
  stop(pid);
}

// rule 3: a new process starts at level 0, even if its parent is at level 3
void
test_child_level(void)
{
  struct pinfo par, kid;
  int fds[2];
  int g = 0;
  printf("test 7: a child of a level-3 process starts at level 0\n");
  quiet();
  pipe(fds);
  int pid = fork();
  if (pid == 0) {
    struct pinfo me;
    do {
      lookup(getpid(), &me);   // burn cpu until we're at the bottom
    } while (me.level < 3);
    g = fork();
    if (g == 0) {
      pause(30);
      exit(0);
    }
    write(fds[1], &g, sizeof(g));   // tell the test the grandchild's pid
    wait(0);
    exit(0);
  }
  read(fds[0], &g, sizeof(g));
  lookup(pid, &par);
  lookup(g, &kid);
  printf("      parent level %d, new child level %d runticks %d\n",
         par.level, kid.level, kid.runticks);
  check(par.level == 3, "the parent is at level 3");
  check(kid.level == 0 && kid.runticks <= 1, "its child starts fresh at level 0");
  kill(g);
  wait(0);
  close(fds[0]);
  close(fds[1]);
}

// lots of processes spinning, sleeping, forking and exiting at once.
// bad locking usually shows up here as a panic, a hang, or a lost process
void
stress(void)
{
  int nkids = 20;
  int status;
  int bad = 0;

  printf("stress: %d children spinning, sleeping, forking and exiting\n", nkids);
  int base = getpinfo(buf, NPROC);   // how many processes exist before we start

  for (int k = 0; k < nkids; k++) {
    int pid = fork();
    if (pid < 0) {
      printf("stress: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      for (int round = 0; round < 30; round++) {
        int kind = (getpid() + round) % 4;
        if (kind == 0) {
          int t = uptime();
          while (uptime() - t < 2)
            ;                        // spin for 2 ticks
        } else if (kind == 1) {
          pause(1);                  // sleep for a tick
        } else if (kind == 2) {
          int g = fork();            // make a grandchild that exits right away
          if (g == 0)
            exit(0);
          if (g > 0)
            wait(0);
        } else {
          getpinfo(buf, NPROC);      // read the table while it's changing
        }
      }
      exit(0);
    }
  }

  // one more child keeps reading the table and checks every entry is sane
  int watcher = fork();
  if (watcher == 0) {
    for (int i = 0; i < 300; i++) {
      int n = getpinfo(buf, NPROC);
      if (n < 1 || n > NPROC)
        exit(1);
      for (int j = 0; j < n; j++) {
        if (buf[j].level < 0 || buf[j].level > 3 || buf[j].state < 1 || buf[j].state > 5)
          exit(1);
      }
    }
    exit(0);
  }

  // collect all of them
  for (int k = 0; k < nkids + 1; k++) {
    if (wait(&status) < 0 || status != 0)
      bad = bad + 1;
  }

  check(bad == 0, "every child finished and exited cleanly");
  check(getpinfo(buf, NPROC) == base, "no lost or leftover processes");
}

int
main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "stress") == 0) {
    stress();
  } else {
    printf("mlfqtest: takes about a minute. needs one cpu (make CPUS=1 qemu)\n");
    test_demotion();
    test_not_forgiven();
    test_keeps_running();
    test_round_robin();
    test_preempt();
    test_boost();
    test_child_level();
  }

  if (failures == 0) {
    printf("mlfqtest: OK\n");
    exit(0);
  }
  printf("mlfqtest: %d FAILED\n", failures);
  exit(1);
}
