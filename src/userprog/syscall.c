#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "pagedir.h"
#include "threads/palloc.h"
#include "userprog/user-memory-access.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock file_lock;

void syscall_init(void) {
  lock_init(&file_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int get_from_user_stack(const int *esp, int offset) {
  int value;
  if (!safe_memcpy_from_user(&value, esp + offset, sizeof(int)))
    sys_exit(-1);
  return value;
}

static int get_syscall_n(void *esp) {
  return get_from_user_stack(esp, 0);
}

static void get_syscall_args(void *esp, int n, int *syscall_args) {
  for (int i = 0; i < n; i++)
    syscall_args[i] = get_from_user_stack(esp, 1 + i);
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
    f->eax = sys_create((const char *) syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_REMOVE:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_remove((const char *) syscall_arg[0]);
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
    sys_seek(syscall_arg[0], syscall_arg[1]);
    break;
  case SYS_TELL:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_tell(syscall_arg[0]);
    break;
  case SYS_CLOSE:
    get_syscall_args(f->esp, 1, syscall_arg);
    sys_close(syscall_arg[0]);
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
  char *cmd_line_copy = palloc_get_multiple(0, 10);

  if (safe_strcpy_from_user(cmd_line_copy, cmd_line) == -1) {
    palloc_free_page(cmd_line_copy);
    sys_exit(-1);
  }

  tid_t tid = process_execute(cmd_line_copy);
  palloc_free_page(cmd_line_copy);
  if (tid == TID_ERROR)
    return PID_ERROR;
  // pid is PID_ERROR, unless load() has succeed and start_process() has allocated pid
  return get_thread_by_tid(tid)->pcb->pid;
}

static int sys_wait(pid_t pid) {
  struct thread *child = get_thread_by_pid(pid);
  if (child == NULL)
    return -1;
  if (child->pcb->can_wait)
    child->pcb->can_wait = false;
  else
    return -1;
  return process_wait(child->tid);
}

static int sys_open(const char *file_name) {
  struct thread *cur = thread_current();
  struct file *file;

  void *kernel_ptr = pagedir_get_page(cur->pagedir, file_name);
  if (kernel_ptr == NULL)
    sys_exit(-1);

  file = filesys_open(file_name);

  if (file == NULL)
    return -1;

  return allocate_fd(file);
}

static int sys_filesize(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return 0;

  return file_length(file);
}

static int sys_read(int fd, void *buffer, unsigned size) {
  unsigned char *kbuffer;
  struct file *file;
  int read_bytes;

  file = get_file_by_fd(fd);
  if (file == NULL && fd != STDIN_FILENO)
    return -1;

  kbuffer = palloc_get_page(0);
  if (fd == STDIN_FILENO) {
    for (unsigned i = 0; i < size; i++) {
      kbuffer[i] = input_getc();
    }
    read_bytes = (int) size;
  } else {
    read_bytes = file_read(file, kbuffer, (off_t) size);
  }

  void *ptr = safe_memcpy_to_user(buffer, kbuffer, read_bytes);
  palloc_free_page(kbuffer);
  if (ptr == NULL) {
    sys_exit(-1);
  }
  return read_bytes;
}

int sys_write(int fd, void *buffer, unsigned int size) {
  unsigned char *kbuffer;
  struct file *file;
  int write_bytes;

  file = get_file_by_fd(fd);
  if (file == NULL && fd != STDOUT_FILENO)
    return -1;

  kbuffer = palloc_get_page(0);
  void *ptr = safe_memcpy_from_user(kbuffer, buffer, size);
  if (ptr == NULL) {
    palloc_free_page(kbuffer);
    sys_exit(-1);
  }

  if (fd == STDOUT_FILENO) {
    putbuf((const char *) kbuffer, size);
    write_bytes = (int) size;
  } else {
    write_bytes = file_write(file, kbuffer, (off_t) size);
  }

  palloc_free_page(kbuffer);
  return write_bytes;
}

static void sys_seek(int fd, unsigned position) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return;

  file_seek(file, (int) position);
}

static unsigned sys_tell(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return 0;

  return (unsigned) file_tell(file);
}

void sys_close(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return;

  file_close(file);
  free_fd(fd);
}

static bool sys_create(const char *file, unsigned initial_size) {
  char *kfile = palloc_get_page(0);
  if (safe_strcpy_from_user(kfile, file) == -1) {
    palloc_free_page(kfile);
    sys_exit(-1);
  }

  lock_acquire(&file_lock);
  bool success = filesys_create(kfile, (off_t) initial_size);
  lock_release(&file_lock);

  palloc_free_page(kfile);
  return success;
}

static bool sys_remove(const char *file) {
  char *kfile = palloc_get_page(0);
  if (safe_strcpy_from_user(kfile, file) == -1) {
    palloc_free_page(kfile);
    sys_exit(-1);
  }

  lock_acquire(&file_lock);
  bool success = filesys_remove(kfile);
  lock_release(&file_lock);

  palloc_free_page(kfile);
  return success;
}