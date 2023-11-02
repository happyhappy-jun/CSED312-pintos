#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"
#include "userprog/process.h"

enum fd_check_mode {
  FD_CHECK_READ,
  FD_CHECK_WRITE,
  FD_CHECK_DEFAULT
};

static bool valid_fd(int, enum fd_check_mode);

void syscall_init(void);
static void syscall_handler(struct intr_frame *);

void sys_exit(int);
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
void sys_close(int);

static int get_from_user_stack(const int *, int);
static int get_syscall_n(void *);
static void get_syscall_args(void *, int, int *);

#endif /* userprog/syscall.h */
