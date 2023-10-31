#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/user-memory-access.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock file_lock;

static int get_from_user_stack(const int *esp, int offset) {
  int value;
  safe_memcpy_from_user(&value, esp + offset, sizeof(int));
  return value;
}

static int get_syscall_n(void *esp) {
  return get_from_user_stack(esp, 0);
}

static void get_syscall_args(void *esp, int n, int *syscall_args) {
  for (int i = 0; i < n; i++)
    syscall_args[i] = get_from_user_stack(esp, 1 + i);
}

void syscall_init(void) {
  lock_init(&file_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f) {
  int syscall_n;
  int syscall_arg[3];

  if (!validate_uaddr(f->esp))
    sys_exit(-1);

  syscall_n = get_syscall_n(f->esp);
  switch (syscall_n) {
  case SYS_HALT: shutdown_power_off();
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
    f->eax = sys_create((const char *) syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_REMOVE:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_remove((const char *) syscall_arg[0]);
    break;
  case SYS_OPEN:
    get_syscall_args(f->esp, 1, syscall_arg);
    // TODO: f->eax = sys_open((const char *) syscall_arg[0]);
    break;
  case SYS_FILESIZE:
    get_syscall_args(f->esp, 1, syscall_arg);
    // TODO: f->eax = sys_filesize(syscall_arg[0]);
    break;
  case SYS_READ:
    get_syscall_args(f->esp, 3, syscall_arg);
    // TODO: f->eax = sys_read(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    break;
  case SYS_WRITE:
    get_syscall_args(f->esp, 3, syscall_arg);
    f->eax = sys_write(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    break;
  case SYS_SEEK:
    get_syscall_args(f->esp, 2, syscall_arg);
    // TODO: sys_seek(syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_TELL:
    get_syscall_args(f->esp, 1, syscall_arg);
    // TODO: f->eax = sys_tell(syscall_arg[0]);
    break;
  case SYS_CLOSE:
    get_syscall_args(f->esp, 3, syscall_arg);
    // TODO: f->eax = sys_read(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
  default: break;
  }
}

void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->pcb->exit_code = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

static pid_t sys_exec(const char *cmd_line) {
  char *cmd_line_copy = palloc_get_page(0);

  safe_strcpy_from_user(cmd_line_copy, cmd_line);
  tid_t tid = process_execute(cmd_line_copy);
  palloc_free_page(cmd_line_copy);
  if (tid == TID_ERROR)
    return PID_ERROR;
  // pid is PID_ERROR, unless load() has succeed and start_process() has allocated pid
  return get_thread_by_tid(tid)->pcb->pid;
}

static int sys_wait(pid_t pid) {
  struct thread *t = get_thread_by_pid(pid);
  if (t == NULL)
    return -1;
  return process_wait(t->tid);
}

int sys_write(int fd, void *buffer, unsigned int size) {
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  return -1;
}

static bool sys_create(const char *file, unsigned initial_size) {
  if (file == NULL || !validate_uaddr(file)) {
    sys_exit(-1);
  }

  lock_acquire(&file_lock);
  bool success = filesys_create(file, (off_t) initial_size);
  lock_release(&file_lock);

  return success;
}

static bool sys_remove(const char *file) {
  if (file == NULL || !validate_uaddr(file)) {
    sys_exit(-1);
  }

  lock_acquire(&file_lock);
  bool success = filesys_remove(file);
  lock_release(&file_lock);

  return success;
}