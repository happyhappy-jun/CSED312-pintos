#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "pagedir.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "user/syscall.h"
#include "userprog/user-memory-access.h"
#include "vm/frame.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock file_lock;

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
static void sys_close(int);

#ifdef VM
static mmapid_t sys_mmap(int, void *);
static bool sys_munmap(mapid_t);
static struct mmap_entry *get_mmap_entry_by_id(mmapid_t);
#endif

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
  case SYS_MMAP:
    get_syscall_args(f->esp, 2, syscall_arg);
    f->eax = sys_mmap(syscall_arg[0], (void *) syscall_arg[1]);
    break;
  case SYS_MUNMAP:
    get_syscall_args(f->esp, 1, syscall_arg);
    f->eax = sys_munmap(syscall_arg[0]);
    break;
  default: break;
  }
}

static void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->pcb->exit_code = status;
  thread_exit();
}

static pid_t sys_exec(const char *cmd_line) {
  char *cmd_line_copy = palloc_get_page(PAL_ZERO);
  if (cmd_line_copy == NULL)
    return PID_ERROR;

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
  int result;
  if (child == NULL)
    return -1;
  if (child->pcb->can_wait)
    child->pcb->can_wait = false;
  else
    return -1;
  result = process_wait(child->tid);
  sig_child_can_exit(pid);
  return result;
}

static int sys_open(const char *file_name) {
  struct file *file;

  char *kfile = palloc_get_page(PAL_ZERO);
  if (kfile == NULL)
    return -1;
  if (safe_strcpy_from_user(kfile, file_name) == -1) {
    palloc_free_page(kfile);
    sys_exit(-1);
  }
  file = filesys_open(kfile);
  palloc_free_page(kfile);

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
  int page_cnt = (int) size / PGSIZE + 1;

  file = get_file_by_fd(fd);
  if (file == NULL && fd != STDIN_FILENO)
    return -1;

  kbuffer = palloc_get_multiple(PAL_ZERO, page_cnt);
  if (kbuffer == NULL)
    return -1;
  if (fd == STDIN_FILENO) {
    for (unsigned i = 0; i < size; i++) {
      kbuffer[i] = input_getc();
    }
    read_bytes = (int) size;
  } else {
    read_bytes = file_read(file, kbuffer, (off_t) size);
  }

  void *ptr = safe_memcpy_to_user(buffer, kbuffer, read_bytes);
  palloc_free_multiple(kbuffer, page_cnt);
  if (ptr == NULL) {
    sys_exit(-1);
  }
  return read_bytes;
}

static int sys_write(int fd, void *buffer, unsigned int size) {
  unsigned char *kbuffer;
  struct file *file;
  int write_bytes;
  int page_cnt = (int) size / PGSIZE + 1;

  file = get_file_by_fd(fd);
  if (file == NULL && fd != STDOUT_FILENO)
    return -1;

  kbuffer = palloc_get_multiple(PAL_ZERO, page_cnt);
  if (kbuffer == NULL)
    return -1;
  void *ptr = safe_memcpy_from_user(kbuffer, buffer, size);
  if (ptr == NULL) {
    palloc_free_multiple(kbuffer, page_cnt);
    sys_exit(-1);
  }

  if (fd == STDOUT_FILENO) {
    putbuf((const char *) kbuffer, size);
    write_bytes = (int) size;
  } else {
    write_bytes = file_write(file, kbuffer, (off_t) size);
  }

  palloc_free_multiple(kbuffer, page_cnt);
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

static void sys_close(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return;

  file_close(file);
  free_fd(fd);
}

static bool sys_create(const char *file, unsigned initial_size) {
  char *kfile = palloc_get_page(PAL_ZERO);
  if (kfile == NULL)
    return false;

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
  char *kfile = palloc_get_page(PAL_ZERO);
  if (kfile == NULL)
    return false;

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

#ifdef VM
mmapid_t sys_mmap(int fd, void *upage) {
  lock_acquire(&file_lock);

  if (pg_ofs(upage) != 0 || upage == NULL || fd == STDIN_FILENO || fd == STDOUT_FILENO) {
    lock_release(&file_lock);
    return MAP_FAILED;
  }

  struct thread *curr = thread_current();

  struct file *_file = get_file_by_fd(fd);
  struct file *f = file_reopen(_file);

  if (f == NULL) {
    lock_release(&file_lock);
    return MAP_FAILED;
  }

  size_t file_size = file_length(f);
  if (file_size == 0) {
    lock_release(&file_lock);
    return MAP_FAILED;
  }

  for (size_t offset = 0; offset < file_size; offset += PGSIZE) {
    struct spt_entry *spte = spt_get_entry(&curr->spt, upage + offset);
    if (spte != NULL) {
      lock_release(&file_lock);
      return MAP_FAILED;
    }
  }

  for (size_t offset = 0; offset < file_size; offset += PGSIZE) {
    size_t read_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;

    struct spt_entry *spte = spt_add_file(&curr->spt, upage + offset, true, f, (off_t) offset, read_bytes, zero_bytes);
    if (spte == NULL) {
      lock_release(&file_lock);
      return MAP_FAILED;
    }
  }

  mmapid_t id;
  if (!list_empty(&curr->mmap_list))
    id = (mmapid_t) list_entry(list_back(&curr->mmap_list), struct mmap_entry, elem)->id + 1;
  else
    id = 0;

  struct mmap_entry *mmap_entry = malloc(sizeof(struct mmap_entry));
  mmap_entry->id = id;
  mmap_entry->file = f;
  mmap_entry->addr = upage;
  mmap_entry->size = file_size;
  list_push_back(&curr->mmap_list, &mmap_entry->elem);

  lock_release(&file_lock);
  return id;
}

bool sys_munmap(mmapid_t id) {
  struct thread *curr = thread_current();
  struct mmap_entry *mmap_entry = get_mmap_entry_by_id(id);

  if (mmap_entry == NULL)
    return false;

  size_t offset;
  size_t file_size = mmap_entry->size;
  for (offset = 0; offset < file_size; offset += PGSIZE) {
    struct spt_entry *spte = spt_get_entry(&curr->spt, mmap_entry->addr + offset);
    if (pagedir_is_dirty(curr->pagedir, spte->upage + offset)) {
      lock_acquire(&file_lock);
      file_write_at(spte->file_info->file, spte->upage, spte->file_info->read_bytes, spte->file_info->ofs);
      lock_release(&file_lock);
    }
    spt_remove_by_entry(&curr->spt, spte);
  }
  list_remove(&mmap_entry->elem);
}

static struct mmap_entry *get_mmap_entry_by_id(mmapid_t id) {
  struct thread *curr = thread_current();
  struct list_elem *e;
  for (e = list_begin(&curr->mmap_list); e != list_end(&curr->mmap_list); e = list_next(e)) {
    struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
    if (mmap_entry->id == id)
      return mmap_entry;
  }
  return NULL;
}

#endif
