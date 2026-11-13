// settings for the multi-level feedback queue scheduler

#define MLFQ_LEVELS   4               // level 0 is the highest priority
#define SLICE(level)  (1 << (level))  // time slice in ticks: 1, 2, 4, 8
#define BOOST_TICKS   64              // how often everyone goes back to level 0
