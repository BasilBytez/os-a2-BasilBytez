// spin.c - a CPU hog for trying out the scheduler
// usage: spin <ticks>     burns CPU for about that many ticks, then exits
// try:   spin 300 &       then run pstat a few times and watch its level

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(2, "usage: spin <ticks>\n");
    exit(1);
  }

  int ticks = atoi(argv[1]);
  int start = uptime();

  // keep the CPU busy until enough ticks have gone by
  while (uptime() - start < ticks)
    ;

  exit(0);
}
