#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/user-memory-access.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock file_lock;

static bool valid_fd(int fd, enum fd_check_mode mode) {
  struct thread *cur = thread_current();
  switch (mode) {
  case FD_CHECK_READ:
    return fd >= 0 && fd < cur->pcb->file_cnt && fd != 1;
  case FD_CHECK_WRITE:
    return fd >= 0 && fd < cur->pcb->file_cnt && fd != 0;
  case FD_CHECK_DEFAULT:
    return fd >= 2 && fd < cur->pcb->file_cnt;
  }
}

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

  if (safe_strcpy_from_user(file_name_copy, file_name) == -1) {
    sys_exit(-1);
  }

  lock_acquire(&file_lock);
  file = filesys_open(file_name_copy);
  lock_release(&file_lock);
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

  if (!valid_fd(fd, FD_CHECK_DEFAULT))
    return 0;
  file = cur->pcb->fd_list[fd];

  return file_length(file);
}

static int sys_read(int fd, void *buffer, unsigned size) {
  struct thread *cur = thread_current();
  unsigned char *kbuffer;
  struct file *file;
  int read_bytes;

  if (!valid_fd(fd, FD_CHECK_READ))
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
  if (ptr == NULL) {
    sys_exit(-1);
  }
  return read_bytes;
}

int sys_write(int fd, void *buffer, unsigned int size) {
  struct thread *cur = thread_current();
  unsigned char *kbuffer;
  struct file *file;
  int write_bytes;

  if (!valid_fd(fd, FD_CHECK_WRITE))
    return -1;

  kbuffer = palloc_get_page(0);
  void *ptr = safe_memcpy_from_user(kbuffer, buffer, size);
  if (ptr == NULL) {
    palloc_free_page(kbuffer);
    sys_exit(-1);
  }

  if (fd == 1) {
    lock_acquire(&file_lock);
    putbuf((const char *) kbuffer, size);
    lock_release(&file_lock);
    write_bytes = (int) size;
  } else {
    file = cur->pcb->fd_list[fd];
    lock_acquire(&file_lock);
    write_bytes = file_write(file, kbuffer, (off_t) size);
    lock_release(&file_lock);
  }

  palloc_free_page(kbuffer);
  return write_bytes;
}

static void sys_seek(int fd, unsigned position) {
  struct thread *cur = thread_current();
  struct file *file;

  if (!valid_fd(fd, FD_CHECK_DEFAULT))
    return;

  file = cur->pcb->fd_list[fd];
  file_seek(file, (int) position);
}

static unsigned sys_tell(int fd) {
  struct thread *cur = thread_current();
  struct file *file;
  if (!valid_fd(fd, FD_CHECK_DEFAULT))
    return 0;

  file = cur->pcb->fd_list[fd];
  return (unsigned) file_tell(file);
}

static void sys_close(int fd) {
  struct thread *cur = thread_current();
  struct file *file;

  if (!valid_fd(fd, FD_CHECK_DEFAULT))
    return;

  file = cur->pcb->fd_list[fd];
  file_close(file);
  cur->pcb->fd_list[fd] = NULL;
  cur->pcb->file_cnt--;
}

static bool sys_create(const char *file, unsigned initial_size) {
  char *kfile = palloc_get_page(0);
  safe_strcpy_from_user(kfile, file);

  lock_acquire(&file_lock);
  bool success = filesys_create(kfile, (off_t) initial_size);
  lock_release(&file_lock);

  palloc_free_page(kfile);
  return success;
}

static bool sys_remove(const char *file) {
  char *kfile = palloc_get_page(0);
  safe_strcpy_from_user(kfile, file);

  lock_acquire(&file_lock);
  bool success = filesys_remove(kfile);
  lock_release(&file_lock);

  palloc_free_page(kfile);
  return success;
}