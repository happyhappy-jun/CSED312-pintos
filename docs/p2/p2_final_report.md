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
    if (t->pcb == NULL)
      continue;
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

## User Process Manipulation

## File Manipulation

# Denying Writes to Executables
