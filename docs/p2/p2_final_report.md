# Project 2: User Programs

## Preliminaries

Team Number: 20

윤병준(20190766)

김치헌(20190806)

# User Process

## Saving user process context

유저 프로세스의 맥락을 저장하기 위해 `struct pcb`를 새로 정의하였고, 이를 `struct thread`에 포함시켰다.

### Data Structure

`struct pcb`의 정의는 다음과 같다.

```c
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
```

- `pid`: process id
- `parent_tid`: parent thread id
- `file`: process의 실행 파일
- `fd_list`: file descriptor 배열
- `exit_code`: exit code
- `can_wait`: 부모 프로세스가 자식 프로세스를 기다릴 수 있는지 여부
- `wait_sema`: wait을 위한 semaphore
- `load_sema`: exec시 load 완료 여부를 확인하기 위한 semaphore
- `exit_sema`: wait을 걸지 않은 부모가 exit을 하지 않은 경우 부모의 exit을 기다리기 위한 semaphore

### Algorithms and Implementation

`struct pcb`의 초기화는 `init_pcb()` 함수를 이용해 이루어진다.

```c
struct pcb *init_pcb(void) {
  struct pcb *pcb = palloc_get_page(PAL_ZERO);
  if (pcb == NULL)
    return NULL;
  pcb->fd_list = palloc_get_page(PAL_ZERO);
  if (pcb->fd_list == NULL) {
    palloc_free_page(pcb);
    return NULL;
  }
  pcb->pid = PID_ERROR;
  pcb->parent_tid = thread_current()->tid;
  pcb->file = NULL;
  pcb->exit_code = 0;
  pcb->can_wait = true;
  sema_init(&pcb->wait_sema, 0);
  sema_init(&pcb->load_sema, 0);
  sema_init(&pcb->exit_sema, 0);
  return pcb;
}
```

`init_pcb()`는 `thread_create()`에서 호출되며, 새로 만들어지는 스레드의 `pcb`를 초기화한다.
`thread_create()`를 통해 초기화되기 때문에, `thread_create()`를 통해 만들어지지 않는 "main" 스레드는 `pcb`를 가지고 있지 않다.
`init_pcb()` 도중 `palloc_get_page()`를 통해 할당받은 메모리가 부족한 경우 `NULL`을 반환한다.
`thread_create()`에서 `init_pcb()`가 `NULL`을 반환한 경우 `thread_create()`도 `TID_ERROR`을 반환하도록 구현하였다.

이렇게 할당된 `pcb`는 프로세스가 종료될 때 `free_pcb()`를 통해 해제된다. `free_pcb()`는 `process_exit()`에서 호출된다.

```c
void free_pcb(struct pcb *pcb) {
  palloc_free_page(pcb->fd_list);
  palloc_free_page(pcb);
}
```

최초의 pid는 PID_ERROR로 설정되는데, 이는 `start_process()`에서 `load()`가 성공한 경우 `allocate_pid()`를 통해 설정된다.
`allocate_pid()`는 `allocate_tid()`와 유사하게 다음과 같이 구현하였다.

```c
pid_t allocate_pid(void) {
  static pid_t next_pid = 1;
  pid_t pid;

  lock_acquire(&pid_lock);
  pid = next_pid++;
  lock_release(&pid_lock);

  return pid;
}
```

프로세스 간의 관계는 `parent_tid`를 통해 관리된다.
`parent_tid`는 `init_pcb()`에서 설정되는데 이때, `init_pcb()`를 호출하는 스레드가 새로 만들어지는 스레드의 부모 스레드이다.
따라서 `parent_tid`는 `current_thread()->tid`로 설정된다.

이후 구현될 시스템 콜에서 부모/자식 간 스레드 구조체를 찾기 위해 `get_thread_by_pid()`와 `get_thread_by_tid()`를 구현하였다.

```c
struct thread *get_thread_by_pid(pid_t pid) {
  struct thread *t;
  struct list_elem *e;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    t = list_entry(e, struct thread, allelem);
    if (t->pcb == NULL)
      continue;
    if (t->pcb->pid == pid)
      return t;
  }
  return NULL;
}

struct thread *get_thread_by_tid(tid_t tid) {
  struct thread *t;
  struct list_elem *e;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    t = list_entry(e, struct thread, allelem);
    if (t->tid == tid)
      return t;
  }
  return NULL;
}
```

두 함수 모두 `all_list`를 순회하며 `pid` 또는 `tid`가 일치하는 스레드를 찾는다. 찾을 수 없는 경우에 `NULL`을 반환한다.

pcb를 구성하는 이외의 멤버와 이를 사용하는 알고리즘 및 구현은 이후의 시스템 콜에서 설명한다.

## Argument Passing

## Process Termination Messages

# System Calls

## Accessing User Memory

시스템 콜을 구현하기에 앞서 유저 메모리에 안전하게 접근하기 위한 방법을 고안해야했다.
시스템 콜에서는 우선 시스템 콜 인자를 유저 스택에서 가져와야하고, 포인터를 인자로 받는 시스템 콜은 포인터를 이용해 유저 메모리를 참고해야한다.
핀토스 공식 문서의 3.1.5 Accessing User Memory에서 제안하는 두 가지 방법 중 두 번째 방법을 채택하여 구현하였다.

두 번째 방법은 유저 메모리에 접근하기 전에 PHYS_BASE보다 작다는 것만을 확인한 후 바로 접근을 하는 것이다.
이후, 접근에 문제가 생겨 page fault가 발생하면 이를 userprog/exception.c의 page_fault()에서 처리한다.

### Algorithms and Implementation

먼저, PHYS_BASE보다 큰 주소에 접근하는 것을 막기 위해 `validate_uaddr()`를 구현했다.

```c
bool validate_uaddr(const void *uaddr) {
  return uaddr < PHYS_BASE;
}
```

userprog/exception.c의 page_fault()에서는 kernel이 PHYS_BASE 이하의 주소를 참고하는 중에 page fault가 발생하는 경우를 처리하였다.
이외의 유저 영역에서 발생하는 page fault는 해당 프로세스의 exit_code를 -1로 설정하고 thread_exit()를 호출하여 프로세스를 종료한다.

```c
static void
page_fault(struct intr_frame *f) {
  bool not_present; /* True: not-present page, false: writing r/o page. */
  bool write;       /* True: access was write, false: access was read. */
  bool user;        /* True: access by user, false: access by kernel. */
  void *fault_addr; /* Fault address. */
  
  ...
  
  // fault under PHYS_BASE access by kernel
  // => fault while accessing user memory
  if (fault_addr < PHYS_BASE && !user) {
    f->eip = (void (*)(void))(f->eax);
    f->eax = 0xffffffff;
    return;
  }
  // other userspace page fault => exit(-1)
  else if (user) {
    thread_current()->pcb->exit_code = -1;
    thread_exit();
  }
  
  ...
  
}
```

위와 같이 page fault에 대한 처리가 된다면 핀토스 공식 문서에서 제공한 다음과 같은 예제 코드를 이용하여 유저 메모리에 안전하게 접근할 수 있다.

```c
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int get_user(const uint8_t *uaddr) {
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a"(result)
      : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool put_user(uint8_t *udst, uint8_t byte) {
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}
```

`get_user()`, `put_user()` 모두 하나의 바이트에 대해 동작하기 떄문에, 좀 더 편하게 이용하기 위해 다음과 같은 함수를 추가로 구현하였다.
`get_user()`와 `put_user()` 모두 각자의 방법으로 `page_fault()`에서 설정한 값을 이용해 에러 여부를 반환하는데 이를 적절하게 처리해주어야 한다.

```c
void *safe_memcpy_from_user(void *kdst, const void *usrc, size_t n) {
  uint8_t *dst = kdst;
  const uint8_t *src = usrc;
  int byte;

  ASSERT(kdst != NULL)

  if (!validate_uaddr(usrc) || !validate_uaddr(usrc + n - 1))
    return NULL;

  for (size_t i = 0; i < n; i++) {
    byte = get_user(src + i);
    if (byte == -1)
      return NULL;
    dst[i] = byte;
  }
  return kdst;
}

void *safe_memcpy_to_user(void *udst, const void *ksrc, size_t n) {
  uint8_t *dst = udst;
  const uint8_t *src = ksrc;
  int byte;

  if (!validate_uaddr(udst) || !validate_uaddr(udst + n - 1))
    return NULL;

  for (size_t i = 0; i < n; i++) {
    byte = src[i];
    if (!put_user(dst + i, byte))
      return NULL;
  }
  return udst;
}

int safe_strcpy_from_user(char *kdst, const char *usrc) {
  int byte;

  ASSERT(kdst != NULL)

  for (int i = 0;; i++) {
    if (!validate_uaddr(usrc + i))
      return -1;

    byte = get_user((const unsigned char *) usrc + i);
    if (byte == -1)
      return -1;

    kdst[i] = (char) byte;

    if (byte == '\0')
      return i;
  }
}
```

커널이 유저 메모리에 값을 작성하는 경우와 커널이 유저 메모리에서 값을 읽어오는 경우에 대해 각각 `safe_memcpy_to_user()`, `safe_memcpy_from_user()`를 구현하였다.
이때 접근하게 되는 유저 메모리의 시작과 끝 모두 `validate_uaddr()`를 통해 PHYS_BASE보다 작은지 확인한다.
이후 반복문으로 유저 메모리의 값을 하나씩 읽어오거나 작성한다.

`safe_strcpy_from_user()`는 null이 나올 때까지 유저 메모리에서 값을 읽어와 커널 메모리에 작성한다.

세 함수 모두 에러가 발생한 경우, 에러에 대응하는 값을 반환하여 호출자가 할당받은 메모리 해제 등의 에러에 대한 처리를 하도록 구현하였다.

## System Call Handlers

## User Process Manipulation

## File Manipulation

# Denying Writes to Executables
