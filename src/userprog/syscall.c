#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/user-memory-access.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler(struct intr_frame *);

static void sys_exit(int);
static pid_t sys_exec(const char *);
static int sys_wait(pid_t);

static bool sys_create(const char *, unsigned initial_size);
static bool sys_remove(const char *);
static int sys_open(const char *);
static int sys_filesize(int);
static int sys_read(int, void *, unsigned);
static int sys_write(int, void *, unsigned);
static void sys_seek(int, unsigned);
static unsigned sys_tell(int);

static int get_from_user_stack(const int *, int);
static int get_syscall_n(void *);
static void get_syscall_args(void *, int, int *);

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
    f->eax = sys_open((const char *) syscall_arg[0]);
    break;
  case SYS_FILESIZE:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_filesize(syscall_arg[0]);
    break;
  case SYS_READ:
    get_syscall_args(f->esp, 3, syscall_arg);
    f->eax = sys_read(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    break;
  case SYS_WRITE:
    get_syscall_args(f->esp, 3, syscall_arg);
    f->eax = sys_write(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
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
    get_syscall_args(f->esp, 3, syscall_arg);
    // f->eax = sys_read(syscall_arg[0], (void *) syscall_arg[1], syscall_arg[2]);
    default:break;
  }
}

static void sys_exit(int status) {
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

static int sys_open(const char *file_name) {
  struct thread *cur = thread_current();
  char *file_name_copy = palloc_get_page(0);
  struct file *file;
  int fd;

  if (safe_strcpy_from_user(file_name_copy, file_name) == -1)
    return -1;

  file = filesys_open(file_name_copy);
  palloc_free_page(file_name_copy);

  if (file == NULL)
    return -1;

  fd = cur->pcb->file_cnt;
  cur->pcb->file_cnt++;
  cur->pcb->fd_list[fd] = file;
  return fd;
}

static int sys_filesize(int fd) {
  struct thread *cur = thread_current();
  struct file *file;

  if (fd == 0 || fd == 1 || fd >= cur->pcb->file_cnt)
    return 0;
  file = cur->pcb->fd_list[fd];

  return file_length(file);
}

static int sys_read(int fd, void *buffer, unsigned size) {
  struct thread *cur = thread_current();
  unsigned char *kbuffer;
  struct file *file;
  int read_bytes;

  if (fd == 1 || fd >= cur->pcb->file_cnt)
    return -1;

  kbuffer = palloc_get_page(0);
  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
      kbuffer[i] = input_getc();
    }
    read_bytes = (int) size;
  } else {
    file = cur->pcb->fd_list[fd];
    read_bytes = file_read(file, kbuffer, (off_t) size);
  }

  void *ptr = safe_memcpy_to_user(buffer, kbuffer, read_bytes);
  palloc_free_page(kbuffer);
  if (ptr == NULL)
    return -1;
  return read_bytes;
}

int sys_write(int fd, void *buffer, unsigned int size) {
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  return -1;
}