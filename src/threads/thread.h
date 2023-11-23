#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "threads/fixed-point.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/spt.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* P1 priority donation */
#define MAX_DONATION_DEPTH 8 /* Based on the test requirment at priority-donate-chain.c */

#define NICE_MIN -20
#define NICE_DEFAULT 0
#define NICE_MAX 20

#define RECENT_CPU_INITIAL 0

#define LOAD_AVG_INITIAL 0

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread {
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */
  int nice;                  /* Niceness. */
  fixed_t recent_cpu;        /* Recent CPU Time. */
  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
  struct pcb *pcb;
#endif
#ifdef VM
  struct spt spt;
#endif

  int original_priority;          /* Original priority of the thread */
  struct lock *waiting_lock;      /* Lock that the thread is waiting for */
  struct list donations;          /* List of donations to handle multiple donations */
  struct list_elem donation_elem; /* List element for donation list */
  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

struct sleep_list_elem {
  struct list_elem elem;
  int64_t end_tick;
  struct semaphore semaphore;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void)
    NO_RETURN;
void thread_yield(void);

void clear_from_donations(struct lock *lock);
void update_donations(void);
void donate_priority(void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func(struct thread *t, void *aux);
void thread_foreach(thread_action_func *, void *);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

/* P1 alarm clock */
void thread_sleep(int64_t end_tick);
void thread_wakeup(int64_t current_tick);

/* P1 Advanced Scheduler */
void calculate_priority(struct thread *t);
void increase_recent_cpu(struct thread *t);
void calculate_recent_cpu(struct thread *t);
void calculate_load_avg(void);

void sort_ready_list(void);

struct thread *get_thread_by_tid(tid_t tid);
void sig_children_parent_exit(void);
void sig_child_can_exit(int);

#ifdef USERPROG
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)
#define FD_MAX (int) (PGSIZE / sizeof(struct file *))
pid_t allocate_pid(void);

struct thread *get_thread_by_pid(pid_t pid);

struct pcb {
  pid_t pid;
  tid_t parent_tid;
  struct file *file;
  struct file **fd_list;
  int exit_code;
  bool can_wait;
  struct semaphore wait_sema;
  struct semaphore load_sema;
  struct semaphore exit_sema;
};

struct pcb *init_pcb(void);
void free_pcb(struct pcb *);

void free_fd(int);
int allocate_fd(struct file *);
struct file *get_file_by_fd(int);
#endif

#endif /* threads/thread.h */
