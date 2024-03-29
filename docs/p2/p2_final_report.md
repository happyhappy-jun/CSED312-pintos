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

### Parsing arguments

```c
  const char *argv[64];
  char *temp_ptr;
  int argc = 0;
  char *token = strtok_r(file_name, " ", &temp_ptr);
  while (token != NULL) {
    argv[argc] = token;
    argc++;
    token = strtok_r(NULL, " ", &temp_ptr);
  }
```

argument 의 파싱은 한 번밖에 안하므로 따로 함수로 분리하지 않고, `load()` 내부에 구현을 진행했다. `strtok_r()`을 이용해 공백을 기준으로 문자열을
자르고, `argv`에 저장한다. `argc`는 `argv`의 인덱스를 나타내는 변수로, `argv`에 저장된 인자의 개수를 나타낸다.

### Parsing filename

```c
static char *parse_command(char *input) {
  char *token;
  char *save_ptr;

  token = strtok_r(input, " ", &save_ptr);

  if (token != NULL) {
    return token;
  } else {
    return NULL;
  }
}
```

첫번째의 인자를 파싱하는 경우는, syscall 에서 사용하는 `process_execute` 등에서 사용하는 경우가 있어 따로 함수로써 분리한다.
방식은 [Parsing arguments](###Parsing arguments) 와 동일하다.

### Push argument to stack

핀토스 문서에 따라 아래와 같은 형태로 스택에 인자를 저장하도록 로직을 구성한다.

```
Address	Name	Data	Type
0xbffffffc	argv[3][...]	bar\0	char[4]
0xbffffff8	argv[2][...]	foo\0	char[4]
0xbffffff5	argv[1][...]	-l\0	char[3]
0xbfffffed	argv[0][...]	/bin/ls\0	char[8]
0xbfffffec	word-align	0	uint8_t
0xbfffffe8	argv[4]	0	char *
0xbfffffe4	argv[3]	0xbffffffc	char *
0xbfffffe0	argv[2]	0xbffffff8	char *
0xbfffffdc	argv[1]	0xbffffff5	char *
0xbfffffd8	argv[0]	0xbfffffed	char *
0xbfffffd4	argv	0xbfffffd8	char **
0xbfffffd0	argc	4	int
0xbfffffcc	return address	0	void (*) ()
```

1. argv 를 스택에 역순으로 푸쉬하기

```c
  for (int i = argc - 1; i >= 0; i--) {
    // 스택 내부에서 차지하는 길이를 계산한다
    int len = strlen(argv[i]) + 1;
    // 데이터 + '\0' 이 차지하는 만큼 esp 를 아래로 내린다. 
    *esp -= len;
    // esp 에 argv[i] 를 복사한다.
    memcpy(*esp, argv[i], len);
    argv[i] = *esp;
  }
```

2. esp 를 word 크기로 정렬한다.

```c
   // align to word size
   // esp 는 밑으로 자리기 때문에, 4로 나눈 나머지 만큼이 다음 word 까지의 오프셋이다
  int padding = (size_t) *esp % 4;
  *esp -= padding;
  memset(*esp, 0, padding);
```

3. argv[i] 의 포인터를 스택에 푸쉬한다

```c
  // argv[argc] 에는 NULL 을 넣어준다
  argv[argc] = 0;
  for (int i = argc; i >= 0; i--) {
    *esp -= sizeof(char *);
    memcpy(*esp, &argv[i], sizeof(char *));
  }
```

4. argv, argc 를 스택에 푸쉬한다.
5. return address 를 스택에 푸쉬한다.

```c
  // push argv
  void *argv_ptr = *esp;
  *esp -= sizeof(char **);
  memcpy(*esp, &argv_ptr, sizeof(char **));

  // push argc
  *esp -= sizeof(int);
  memcpy(*esp, &argc, sizeof(int));

  // push fake return address
  *esp -= sizeof(void *);
  memset(*esp, 0, sizeof(void *));
```

## Process Termination Messages

유저 프로세스가 종료될때는, process termination message 를 출력해야한다.
이를 위해 `process_exit()` 에서 `printf()` 를 이용해 출력하도록 구현한다.

```c
void process_exit(void) {
  struct thread *cur = thread_current();
  uint32_t *pd;

  printf("%s: exit(%d)\n", cur->name, cur->pcb->exit_code);
  
  /* ... */
}
```

현재 스레드의 이름은 argument parsed 된 유저 프로세스의 이름이다. 또한, 종료 코드는 `sys_exit()`에서 `pcb` 에 넣어준다.

```c
static void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->pcb->exit_code = status;
  thread_exit();
}
```

`sys_exit() -> thread_exit() -> process_exit()` 순으로 호출되며, `process_exit()` 에서 `printf()` 를 통해 출력한다.

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

커널이 유저 메모리에 값을 작성하는 경우와 커널이 유저 메모리에서 값을 읽어오는 경우에 대해
각각 `safe_memcpy_to_user()`, `safe_memcpy_from_user()`를 구현하였다.
이때 접근하게 되는 유저 메모리의 시작과 끝 모두 `validate_uaddr()`를 통해 PHYS_BASE보다 작은지 확인한다.
이후 반복문으로 유저 메모리의 값을 하나씩 읽어오거나 작성한다.

`safe_strcpy_from_user()`는 null이 나올 때까지 유저 메모리에서 값을 읽어와 커널 메모리에 작성한다.

세 함수 모두 에러가 발생한 경우, 에러에 대응하는 값을 반환하여 호출자가 할당받은 메모리 해제 등의 에러에 대한 처리를 하도록 구현하였다.

## System Call Handlers

기존의 syscall_handler()이 적절히 동작하도록 추가 구현해주었다.

### Algorithms and Implementation

우선 시스템 콜 번호와 인자를 유저 스택에서 가져오기 위한 헬퍼 함수를 작성하였다.

```c
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
```

`syscall_handler()`는 `get_syscall_n()`을 이용해 시스템 콜 번호를 확인한다.
이후, `switch` 문을 이용해 각 시스템 콜에 따라 필요한 인자를 `get_syscall_args()`를 통해 받아온다.
받아온 인자를 시스템 콜의 각 동작을 수행하기 위해 따로 구현한 함수를 호출한다.
반환값이 존재하는 시스템 콜의 경우 인자로 받은 `intr_frame`의 `eax`에 이를 저장한다.

```c
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
    
  ...
  
  case SYS_CLOSE:
    get_syscall_args(f->esp, 1, syscall_arg);
    sys_close(syscall_arg[0]);
  default: break;
  }
}
```

각 시스템 콜 구현을 위한 함수들은 이후에 설명한다.

## User Process Manipulation

User Process Manipulation을 위해 필요한 시스템 콜은 `halt`, `exit`, `exec`과 `wait`이다.
각각의 시스템 콜을 위해 `sys_exit()`, `sys_exec()`, `sys_wait()`을 구현하였다.
`halt`의 경우, `shutdown_power_off()`를 호출하면 되기 때문에 별도의 구현이 필요하지 않다.

또한, 각 시스템 콜의 동작을 위해 `process_exec()`, `process_exit()`, `process_wait()`을 수정했다.

### Algorithms and Implementation

#### System call `exec`

`exec` 시스템 콜을 위한 `sys_exec()`은 다음과 같이 구현하였다.

```c
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
```

우선 유저 메모리에서 인자를 복사해오기 위한 메모리를 할당받는다. 할당받을 수 없는 경우에는 PID_ERROR를 반환한다.
`safe_strcpy_from_user()`를 통해 유저 메모리에서 인자를 복사해오는데, 이때 에러가 발생한 경우 할당받은 메모리를 해제하고 -1을 반환한다.

이후, `process_execute()`를 통해 새로운 프로세스를 생성한다.
`exec` 시스템 콜을 호출한 프로세스를 A, 새로 생성한 자식(Child) 프로세스를 C라고 하자.
`process_execute()`의 반환값인 C의 `tid`가 `TID_ERROR`인 경우 `PID_ERROR`를 반환한다.
아닌 경우, `get_thread_by_tid()`를 통해 C의 `pcb`를 찾아 `pid`를 반환한다.
C의 `pid`는 `start_process()`에서 `load()` 성공 시에 설정되고, 초기에는 `PID_ERROR`로 설정된다.
`process_execute()`는 반환하는 시점에 C의 `load()`가 종료되고 그 성공 여부에 따라 `pid`가 설정되었음이 보장되도록 구현하였다.
따라서, `sys_exec()`은 C의 `load()`가 성공했는지 따로 확인하지 않고 `pid`를 반환해도 문제없다.

실제 프로세스 생성과 실행 준비가 이루어지는 `process_execute()`의 수정된 구현을 살펴보겠다.

```c
tid_t process_execute(const char *file_name) {
  char *fn_copy, *command;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE);

  command = palloc_get_page(0);
  if (command == NULL) {
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }
  strlcpy(command, file_name, PGSIZE);

  parse_command(command);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(command, PRI_DEFAULT, start_process, fn_copy);
  palloc_free_page(command);
  if (tid == TID_ERROR) {
    palloc_free_page(fn_copy);
  }
  else {
    // if the load() is not finished, wait for it
    // fn_copy will be freed in start_process()
    sema_down(&get_thread_by_tid(tid)->pcb->load_sema);
  }
  return tid;
}
```

우선 C의 이름을 명령줄 인자를 제외하여 파싱한 결과로 사용하기 위해 `parse_command()`를 호출한다.
이후, `thread_create()`를 통해 새로운 스레드를 생성한다.
`thread_create()`가 실패한다면, `fn_copy`를 해제하고 `TID_ERROR`를 반환한다.
`thread_create()`는 새로 생성한 스레드의 `tid`를 반환하는데, A는 이를 통해 C의 `pcb`를 찾을 수 있다.
A는 C의 `pcb`의 `load_sema`를 통해 C의 `load()`가 완료될 때까지 대기한다.
C는 최초로 `start_process()`를 실행하는데, `start_process()`에서 적절하게 `load_sema`를 `sema_up()`한다면,
A의 `process_execute()` 반환 시 C의 `load()` 종료와 성공 여부에 따른 pid 설정이 완료되었음을 보장받을 수 있다.

이제 `start_process()`의 구현을 살펴보겠다.

```c
static void start_process(void *file_name_) {
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load(file_name, &if_.eip, &if_.esp);
  palloc_free_page(file_name);

  /* allocate pid when the load() succeed */
  if (success)
    thread_current()->pcb->pid = allocate_pid();
  /* Let the parent thread/process know the load() is finished */
  sema_up(&thread_current()->pcb->load_sema);

  /* If load failed, quit. */
  if (!success) {
    thread_current()->pcb->exit_code = -1;
    thread_exit();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit"
               :
               : "g"(&if_)
               : "memory");
  NOT_REACHED();
}
```

`start_process()`는 `load()`를 통해 C의 실행 파일을 메모리에 로드한다.
`load()`가 성공한 경우, C의 `pcb`의 `pid`를 설정한다.
이후, `load()`가 완료되었으므로, 성공 여부와는 관계없이 `load_sema`를 `sema_up()`하여 A가 `process_execute()`에서 대기를 종료하도록
한다.
따라서 이 시점에 C의 `load()`는 완료된 것이며, C의 `pid`가 적절하게 설정된 것이다.
이후 C는 자신의 본 기능을 수행하거나, `load()`가 실패했다면, `exit_code`를 -1로 설정하고 `thread_exit()`을 통해 종료할 수 있다.

#### System call `exit`

`exit` 시스템 콜을 위한 `sys_exit()`은 다음과 같이 구현하였다.

```c
static void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->pcb->exit_code = status;
  thread_exit();
}
```

단순히 `status`을 인자로 받아서 현재 `pcb`의 `exit_code`에 설정해 준 후, `thread_exit()`를 호출하여 프로세스를 종료한다.
`thread_exit()`은 `USERPROG`일 때 `process_exit()`를 호출하기 때문에 실질적인 프로세스의 종료 과정은 `process_exit()`에 구현하였다.

수정된 `process_exit()`은 다음과 같다.

```c
void process_exit(void) {
  struct thread *cur = thread_current();
  uint32_t *pd;

  printf("%s: exit(%d)\n", cur->name, cur->pcb->exit_code);

  /* allow write and close the executable file */
  file_close(cur->pcb->file);

  /* close all opened files */
  for (int fd = 2; fd < FD_MAX; fd++) {
    struct file *file = get_file_by_fd(fd);
    if (file != NULL) {
      file_close(file);
      free_fd(fd);
    }
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }
  sema_up(&cur->pcb->wait_sema);  // sema up wait_sema for waiting parent
  sig_children_parent_exit();     // sema up exit_sema for children to free their resources
  sema_down(&cur->pcb->exit_sema);// exit_sema up only when the parent exit
  free_pcb(cur->pcb);
}
```

우선, 프로세스 종료 메시지를 출력한다. 이후 프로세스의 자원들을 해제하는 작업을 한다.
`pcb`에 저장된 `file`과 `fd_list`에 남아있는 파일을 모두 닫아주며 프로세스 가상 주소를 위한 `pagedir`에 대한 후처리를 한다.
중요한 부분은 프로세스 종료를 위해 관계된 프로세스 사이의 싱크를 맞추는 것이다.
현재 `exit` 시스템 콜을 발생시킨 프로세스를 A라고 하자. A는 가장 먼저 자신의 `wait_sema`를 `sema_up` 해준다.
만약 A를 `wait`하고 있는 부모 프로세스가 있다면, 이때 `wait`을 종료한다. `wait`에 대해 자세한 내용은 이후에 다룬다.
다음으로 `sig_children_parent_exit()`을 통해 자식 프로세스들에게 부모인 A가 `exit` 함을 알린다.
마지막으로 남은 자원인 `pcb`를 해제하기 위해 `exit_sema`를 `sema_down()`한다.

`exit_sema`의 초기값이 0이기 때문에 `exit_sema`에 대해 다른 프로세스가 `sema_up()`을 해준 적이 없다면 `sema_up()`을 기다리게 된다.
이는 자식 프로세스인 A가 종료된 후에도 A의 부모 프로세스(P라고 하겠다)가 A에 대해 `wait`을 할 수 있기 때문에 필요한 장치이다.

`exit_sema`는 두 가지 상황에 `sema_up()`된다.
첫 번째는 P가 A에 대해 `wait`을 호출한 경우다. P는 `wait`을 위해 A의 `wait_sema`를 `sema_down()`한다.
A가 `exit`을 하고 `wait_sema`를 `sema_up()`하면 P는 `wait`을 종료하고 A의 `exit_sema`를 `sema_up()`한다.
두 번째는 P가 `exit`하는 경우다.
이때 P는 `exit`을 하고 `sig_children_parent_exit()`을 통해 A의 `exit_sema`를 `sema_up()`한다.

두 상황 모두 A가 더 이상 P의 `wait`을 고려하지 않아도 되는 상황임을 의미하며 이 시점부터 A는 자신의 `pcb`를 해제할 수 있다.

`sig_children_parent_exit()`의 구현은 아래와 같다.

```c
void sig_children_parent_exit(void) {
  struct thread *t;
  struct list_elem *e;
  struct thread *cur = thread_current();
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    t = list_entry(e, struct thread, allelem);
    if (t->pcb == NULL)
      continue;
    if (t->pcb->parent_tid == cur->tid) {
      sema_up(&t->pcb->exit_sema);
    }
  }
}
```

#### System call `wait`

`wait` 시스템 콜을 위한 `sys_wait()`은 다음과 같이 구현하였다.

```c
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
```

먼저 `pid`에 해당하는 자식 프로세스(C라고 하겠다)를 찾는다. pid에 해당하는 프로세스가 없는 경우 `-1`을 반환한다.
이후, C에 대해 `wait`을 할 수 있는지 C의 `pcb`의 `can_wait`을 이용해 확인한다.
`can_wait`이 참이라면 `can_wait`을 거짓으로 설정하고 `process_wait()`을 호출한다.
`can_wait`이 거짓이라면 `-1`을 반환한다. `can_wait`이 거짓이라는 것은 이미 한 번 C에 대한 `wait`이 이루어졌음을 의미한다.
`can_wait`을 통해 한 번 이상의 `wait`에 대해 -1을 반환하는 처리가 가능하다.
실제 대기는 `process_wait()`을 통해 이루어진다.
`process_wait()`이 종료되면 C가 자신의 pcb를 해제해도 문제없기 때문에, `sig_child_can_exit()`을 통해 C의 `exit_sema`
를 `sema_up()`한다.

`sig_child_can_exit()`의 구현은 다음과 같다.

```c
void sig_child_can_exit(pid_t pid) {
  struct thread *child = get_thread_by_pid(pid);
  if (child == NULL)
    return;
  if (child->pcb->parent_tid == thread_current()->tid)
    return;
  sema_up(&child->pcb->exit_sema);
}
```

이제 실제 대기가 이루어지는 `process_wait()`을 살펴보겠다.

```c
int process_wait(tid_t child_tid) {
  struct thread *current = thread_current();
  struct thread *child = get_thread_by_tid(child_tid);

  // invalid tid
  if (child == NULL)
    return -1;
  // not child
  if (child->pcb->parent_tid != current->tid)
    return -1;

  // else, wait for the child;
  // wait_sema of the child only up when the child exit
  sema_down(&child->pcb->wait_sema);
  return child->pcb->exit_code;
}
```

우선 인자로 받은 `tid`에 대한 자식 프로세스(C라고 하겠다)를 찾는다. 자식 프로세스가 없는 경우 `-1`을 반환한다.
또한, C가 자식 프로세스가 맞는지 확인하기 위해 `parent_tid`를 이용해 확인한다. C가 자식 프로세스가 아닌 경우 `-1`을 반환한다.
이후, C의 `wait_sema`를 `sema_down()`하여 C가 종료될 때까지 대기한다.
C가 종료하면서 `wait_sema`를 `sema_up()`해주면 이후에 `exit_code`를 반환한다.

## File Manipulation

파일 입출력을 위한 시스템 콜은 `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`
이다.

각각의 시스템 콜을
위해 `sys_create()`, `sys_remove()`, `sys_open()`, `sys_filesize()`, `sys_read()`, `sys_write()`,
`sys_seek()`, `sys_tell()`, `sys_close()`를 구현하였다.

또한, create와 remove를 제외한 시스템 콜은 file descriptor를 이용해 파일을 참조하기 때문에 이를 위한 구현이 필요하다.

### Data Structures

File Descriptor를 위해 `pcb`에 `fd_list`를 추가했다.
`fd_list`는 `file *`을 저장하는 배열이다. `fd_list`는 `init_pcb()`에서 `palloc_get_page()`을 통해 할당받는다.
고정된 크기이므로 `FD_MAX`가 존재하며 이 크기는 `PGSIZE / sizeof(struct file *)`로 설정하였다.
핀토스에서 `PGSIZE`는 4096이고 32비트 주소를 사용하므로 `FD_MAX`는 1024가 된다.
이는 핀토스 공식 문서에서 언급한 프로세스가 최대로 여는 파일이 128개를 넘지 않는다는 가정을 만족한다.

fd 0과 1은 각각 `STDIN_FILENO`, `STDOUT_FILENO`로 사용된다.
따라서, 해당 fd에 대한 처리와 유효한 fd에 대한 검증 등을 위해 `fd_list`와 상호작용하는 함수들을 따로 작성하였다.
실제로 `fd_list`에는 fd 2에 해당하는 파일의 `struct file *`부터 저장되도록 하였다.

### Algorithms and Implementation

#### File Descriptor System

File descriptor 사용을 위해 `fd_list`에 접근하는 함수들은 다음과 같이 구현하였다.

```c
static struct file *get_fd_list_entry(int fd) {
  return thread_current()->pcb->fd_list[fd - 2];
}

static void set_fd_list_entry(int fd, struct file *file) {
  thread_current()->pcb->fd_list[fd - 2] = file;
}

static bool valid_fd(int fd) {
  return fd >= 2 && fd < FD_MAX && get_fd_list_entry(fd) != NULL;
}

int allocate_fd(struct file *file) {
  int fd = -1;
  struct pcb *pcb = thread_current()->pcb;
  for (int i = 2; i < FD_MAX; i++) {
    if (get_fd_list_entry(i) == NULL) {
      fd = i;
      set_fd_list_entry(i, file);
      break;
    }
  }
  return fd;
}

void free_fd(int fd) {
  set_fd_list_entry(fd, NULL);
}

struct file *get_file_by_fd(int fd) {
  if (!valid_fd(fd))
    return NULL;
  return get_fd_list_entry(fd);
}
```

먼저 static 함수로 `fd_list`에 접근하는 함수들을 구현하였다.
`get_fd_list_entry()`은 `fd_list`의 `fd`에 해당하는 `file *`을 반환한다.
`set_fd_list_entry()`은 `fd_list`의 `fd`에 `file *`을 저장한다.
`valid_fd()`는 `fd`가 유효한지 확인한다.

이후로 시스템 콜에서 직접 사용하기 위한 함수들을 위의 함수들을 이용해 구현하였다.
`allocate_fd()`는 `fd_list`에서 비어있는 공간에 `file *`을 저장하고 해당 `fd`를 반환한다.
`free_fd()`는 `fd_list`의 `fd`에 저장된 `file *`을 해제한다.
`get_file_by_fd()`는 `fd`에 해당하는 `file *`을 반환한다.

`fd_list` 자체의 할당과 해제는 `init_pcb()`와 `free_pcb()`에서 이루어지며, 이는 User Process의 pcb를 설명하는 부분에서 살펴보았다.
프로세스가 종료될 때 `fd_list`에 남아있는 열린 파일들은 `exit` 시스템 콜에서 언급한대로 `process_exit()`에서 닫아준다.

#### File Descriptor System Calls

File descriptor를 사용하는 시스템 콜은 `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`이다.

먼저 `open`과 `close`, 다음으로 `filesize`, `seek`, `tell`, 마지막으로 `read`, `write` 순서로 나눠서 살펴보겠다.

```c
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

static void sys_close(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return;

  file_close(file);
  free_fd(fd);
}
```

`sys_open()`은 우선 유저 메모리에서 인자를 복사해오기 위한 메모리를 할당받는다. 메모리 할당을 실패한 경우 `-1`을 반환한다.
`safe_strcpy_from_user()`를 통해 유저 메모리에서 인자를 복사해온다.
이때 에러가 발생한 경우 할당받은 메모리를 해제하고 `sys_exit(-1)`을 통해 프로세스를 종료한다.
여기까지 문제가 없다면 `filesys_open()`을 통해 파일을 열고, 인자를 위해 할당받은 메모리를 해제한다.
`filesys_open()`이 실패한 경우 `-1`을 반환한다.
이후 `allocate_fd()`를 통해 `file *`을 `fd_list`에 저장하고 `fd`를 반환한다.

`sys_close()`는 먼저 `fd`에 해당하는 `file *`을 `get_file_by_fd()`를 통해 찾는다.
이때 `get_file_by_fd()`를 통해 `fd`에 해당하는 `file *`을 찾고 찾을 수 없는 경우에는 아무 동작도 하지 않고 리턴한다.
아니라면 `file_close()`를 호출하고, `free_fd()`를 사용해 `fd`에 해당하는 `file *`을 `fd_list`에서 해제한다.

```c
static int sys_filesize(int fd) {
  struct file *file;

  file = get_file_by_fd(fd);
  if (file == NULL)
    return 0;

  return file_length(file);
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
```

`sys_filesize()`, `sys_seek()`, `sys_tell()`은 fd를 이용해 파일을 참조하므로 `get_file_by_fd()`로 `fd`에
해당하는 `file *`을 찾는다.
찾을 수 없다면 각 시스템 콜에 맞게 적절한 값을 반환하고 다른 동작을 취하지 않는다.
아니라면 각 시스템 콜에 맞게 `file *`을 인자로 `file_length()`, `file_seek()`, `file_tell()`을 호출하고 필요한 경우 그 결과를
반환한다.

```c
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
```

`sys_read()`와 `sys_write()`는 우선 `get_file_by_fd()`를 통해 `fd`에 해당하는 `file *`을 찾는다.
이때 두 함수는 각각 특수한 fd인 `STDIN_FILENO`, `STDOUT_FILENO`에 대해서도 고려해야 한다.
특수한 fd가 아니면서 `file *`을 찾을 수 없다면 `-1`을 반환한다.

이후 유저 메모리에서 값을 읽어오거나 쓰기 위해 메모리를 할당받는다.
`size`가 얼마나 큰지에 따라 하나 이상의 page를 할당받아야 할 수도 있으므로 `palloc_get_multiple()`을 사용한다.
할당받아야 하는 page의 개수는 `size`를 `PGSIZE`로 나눈 몫에 1을 더한 값으로, 코드에서 이 값은 `page_cnt`에 저장된다.
메모리 할당 실패 시 `-1`을 반환한다. 이후` sys_read()`와 `sys_write()`의 동작이 다르므로 각각의 동작을 살펴보겠다.

우선, `sys_read()`는 `STDIN_FILENO`인 경우, `input_getc()`를 이용해 `size`만큼의 값을 할당받은 커널 메모리에 읽어온다.
혹은 일반적인 파일의 경우, `file_read()`를 이용해 `size`만큼의 값을 할당받은 커널 메모리에 읽어온다.
실제 읽어온 바이트 수를 `read_bytes`에 저장한다.
이후, `safe_memcpy_to_user()`를 통해 커널 메모리에서 유저 메모리로 읽어온 값을 복사한다.
복사한 후에는 할당받은 커널 메모리를 해제한다.
`safe_memcpy_to_user()`가 실패한 경우 `sys_exit(-1)`을 통해 프로세스를 종료하고, 성공한 경우 `read_bytes`를 반환한다.

`sys_write()`는 먼저 파일에 써야할 값을 `safe_memcpy_from_user()`를 통해 유저 메모리에서 `size`만큼 할당받은 커널 메모리에 복사한다.
`safe_memcpy_from_user()`가 실패한 경우 할당받은 커널 메모리를 해제하고 `sys_exit(-1)`을 통해 프로세스를 종료한다.
성공한다면 fd에 맞는 동작을 수행한다.
`STDOUT_FILENO`인 경우 `putbuf()`를 통해 커널 메모리의 값을 `size`만큼 터미널에 출력하고, `size`를 `write_bytes`에 저장한다.
일반적인 파일의 경우 `file_write()`를 통해 커널 메모리의 값을 `size`만큼 파일에 쓰고, 실제 작성한 바이트 수를 `write_bytes`에 저장한다.
이후 할당받은 커널 메모리를 해제하고 `write_bytes`를 반환한다.

#### Non-File Descriptor System Calls

`create` 는 파일을 생성하는 시스템 콜이다. `filesys` 에 미리 구현된 `filesys_create()` 를 통해 파일을 생성한다.
유저 메모리 영역에서 파일 이름을 복사해와 커널 메모리에 작성한다.
파일 시스템에 대한 쓰기 작업이므로, `file_lock` 을 이용해 파일 시스텀에 접근하기 전에 락을 확보하고 create 를 한다.
그리고 락을 해제한다.

```c
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
```

`remove` 는 파일을 삭제하는 시스템 콜이다. `filesys` 에 미리 구현된 `filesys_remove()` 를 통해 파일을 삭제한다. 유저 메모리 영역에서
파일 이름을 복사해 커널 메모리로 옮긴다. `create` 와 마찬가지로 파일 시스템에 대한 쓰기 작업이므로,
`file_lock` 을 이용해 파일 시스텀에 접근하기 전에 락을 확보하고 remove 를 한다. 그리고 락을 해제한다.

```c
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
```

## Discussion

- User memory accessing  
  유저 메모리에 접근하기 위해 구현한 함수 중 `safe_strcpy_from_user()`가 있다.
  현재 구현 상 문제가 발생하지는 않았지만, `strcpy`가 아닌 `strlcpy` 형태로 함수를 구현하는 것이 더 좋았을 것 같다.

- System call handler
  현재 구현처럼 고정된 배열에 필요한 만큼 인자를 미리 받아오고 사용하는 것과 각 시스템 콜에 대응하는 함수에서 필요할 때 유저 메모리에 접근하여 사용하는 방법 사이에서 고민했다.
  현재 구현을 채택한 이유는 유저 메모리에 접근하기 때문에 한 번에 접근을 마치고, 문제가 있을 때 조기 종료하는 것이 합리적이라 판단했기 때문이다.

- User process manipulation  
  자식 프로세스를 부모 프로세스가 따로 관리하지는 않고 있다.
  따라서, 어떤 pid 혹은 tid가 자식 프로세스가 맞는지 확인하기 위해서는 우선 `all_list`를 순회해야한다.
  이는 프로세스가 많아질 수록 성능에 안좋은 영향을 줄 수 있음을 의미한다.

- File manipulation
  현재 `fd_list`를 고정된 크기로 메모리를 할당받아 사용하고 있다. 이 때문에 프로세스가 열 수 있는 파일에 상한이 존재하게 되었다.
  핀토스 공식 문서에서 128개 이상의 파일은 열지 않는다고 언급이 되어있긴 하지만, 상한이 없는 편이 낫다고도 함께 나와있다.
  핀토스가 제공하는 `list`를 이용하여 `fd_list`를 구성할 수도 있을 것이다.
  다만 그러기 위해서는 추가적인 구조체 디자인이 필요하며, 해당 구조체를 위한 메모리가 추가로 필요해질 것이다.
  또한, 프로세스가 종료하는 과정에서 각 구조체에 대해 할당받은 메모리를 해제하는 작업 또한 필요해진다.
  여러 가지를 고려하여 간단하고 효율적인 구현을 선택하였다.
  만약 프로세스가 핀토스 공식 문서가 이야기한 것보다 많은 파일을 열어 `FD_MAX`를 넘어가는 일이 생긴다면
  추가로 `palloc_get_page()`를 이용해 `fd_list`를 확장하는 방법으로 대응할 수 있을 것 같다.

# Denying Writes to Executables

프로그램을 실행 중일 때는데, 실행 파일은 수정되지 않아야 한다.
이 기능은 `file.h` 의

- `void file_deny_write(struct file *);`
- `void file_allow_write(struct file *);`

을 통해서 구현할 수 있다. 프로그램을 메모리에 로드하는 시점에 로드에 성공 한다면, 그 즉시 파일에 대한 쓰기를 막아서, 오류를 방지한다.
또한, 프로세스가 종료될때는 이 파일에 대한 쓰기를 다시 허용해줘야한다. 이를 `load()`, `process_exit()` 에 구현해주었다.
프로세스를 열때, 파일에 대한 쓰기를 막고, pcb 에 파일을 저장해준다. 나중에 프로세스를 종료할 때 해당 pcb 를 참조해서
파일을 닫아준다. `file_close` 함수는 내부적으로 쓰기를 허용해주고, 닫기 때문에 이 함수를 사용하면 된다.

```c
  /* Open executable file. */
  file = filesys_open(argv[0]);
  if (file == NULL) {
    printf("load: %s: open failed\n", argv[0]);
    goto done;
  }
  file_deny_write(file);
  t->pcb->file = file;
```

```c
void process_exit(void) {
  struct thread *cur = thread_current();
  uint32_t *pd;

  printf("%s: exit(%d)\n", cur->name, cur->pcb->exit_code);

  /* allow write and close the executable file */
  file_close(cur->pcb->file);
  
  /* ... */
  
}
  
void file_close(struct file *file) {
  if (file != NULL) {
    file_allow_write(file);
    inode_close(file->inode);
    free(file);
  }
}
```