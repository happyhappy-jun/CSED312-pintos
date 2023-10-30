#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/user-memory-access.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler(struct intr_frame *);

int get_from_user_stack(const int *, int);
int get_syscall_n(void *);
void get_syscall_args(void *, int, int *);

int get_from_user_stack(const int *esp, int offset) {
  int value;
  safe_memcpy_from_user(&value, esp + offset, sizeof(int));
  return value;
}

int get_syscall_n(void *esp) {
  return get_from_user_stack(esp, 1);
}

void get_syscall_args(void *esp, int n, int *syscall_args) {
  for (int i = 0; i < n; i++)
    syscall_args[i] = get_from_user_stack(esp, 1 + i);
}

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f) {
  int syscall_n;
  int syscall_arg[3];

  syscall_n = get_syscall_n(f->esp);
  switch (syscall_n) {
  case SYS_HALT:
    shutdown_power_off();
  case SYS_EXIT:
    get_syscall_args(f->esp, 1, syscall_arg);
    sys_exit(syscall_arg[0]);
    break;
  case SYS_EXEC:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_exec((const char *) syscall_arg[0]);
    break;
  case SYS_WAIT:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_wait(syscall_arg[0]);
    break;
  case SYS_CREATE:
    get_syscall_args(f->esp, 2, syscall_arg);
    // f->eax = sys_create((const char *) syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_REMOVE:
    get_syscall_args(f->esp, 1, syscall_arg);
    // f->eax = sys_remove((const char *) syscall_arg[0]);
    break;
  case SYS_OPEN:
    get_syscall_args(f->esp, 1, syscall_arg);
    // f->eax = sys_open((const char *) syscall_arg[0]);
    break;
  case SYS_FILESIZE:
    get_syscall_args(f->esp, 1, syscall_arg);
    // f->eax = sys_filesize(syscall_arg[0]);
    break;
  case SYS_READ:
    get_syscall_args(f->esp, 3, syscall_arg);
    // f->eax = sys_read(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    break;
  case SYS_WRITE:
    get_syscall_args(f->esp, 3, syscall_arg);
    // f->eax = sys_write(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    break;
  case SYS_SEEK:
    get_syscall_args(f->esp, 2, syscall_arg);
    // sys_seek(syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_TELL:
    get_syscall_args(f->esp, 1, syscall_arg);
    // f->eax = sys_tell(syscall_arg[0]);
    break;
  case SYS_CLOSE:
    get_syscall_args(f->esp, 1, syscall_arg);
    // sys_close(syscall_arg[0]);
    break;
  default:
    break;
  }
}

void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->pcb->exit_code = status;

  // exit message here

  thread_exit();
}

pid_t sys_exec(const char *cmd_line) {
  struct thread *new_process;
  char *cmd_line_copy = palloc_get_page(0);

  safe_strcpy_from_user(cmd_line_copy, cmd_line);
  tid_t tid = process_execute(cmd_line_copy);
  palloc_free_page(cmd_line_copy);
  if (tid == TID_ERROR)
    return PID_ERROR;
  pid_t pid = allocate_pid();
  new_process->pcb->pid = pid;
  return pid;
}

int sys_wait(pid_t pid) {
  struct thread *t = get_thread_by_pid(pid);
  if (t == NULL)
    return -1;
  return process_wait(t->tid);
}