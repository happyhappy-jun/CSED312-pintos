# Project 3: Virtual Memory

## Preliminaries

Team Number: 20

윤병준(20190766)

김치헌(20190806)

## 1. Frame Table

물리 메모리를 관리하기 위해 `frame table`을 구현한다.

### Data Structure

Frame Table 구현을 위해 사용한 자료 구조는 다음과 같다.

```c
struct frame_table {
  struct hash frame_table;
  struct lock frame_table_lock;
};

struct frame {
  void *kpage;
  void *upage;
  int64_t timestamp;
  bool pinned;
  struct spt_entry *spte;
  struct thread *thread;
  struct hash_elem elem;
};
```

`frame_table`의 멤버 변수는 다음과 같다.
- `frame_table` : `frame`을 저장하는 `hash table`
- `frame_table_lock` : `frame_table`에 대한 동시성 제어를 위한 `lock`

`frame`의 멤버 변수는 다음과 같다.
- `kpage` : `frame`에 할당된 `kernel virtual address`. `palloc_get_page()`를 통해 받은 값을 저장한다.
- `upage` : `frame`에 매핑된 `user virtual address`
- `timestamp` : `frame`이 할당된 시간
- `pinned` : `frame`이 `pinned`되어 있는지 여부
- `spte` : `frame`에 매핑된 `spte`
- `thread` : `frame`을 할당받은 `thread`
- `elem` : `frame_table`에 저장하기 위한 `hash_elem`

### Algorithms and Implementation

#### Frame Table Initialization

Frame Table은 전역적으로 하나만 존재하기 때문에, threads/init.c에서 초기화한다.

```c
void frame_table_init(void) {
  hash_init(&frame_table.frame_table, frame_table_hash, frame_table_less, NULL);
  lock_init(&frame_table.frame_table_lock);
}

static unsigned frame_table_hash(const struct hash_elem *elem, void *aux) {
  struct frame *fte = hash_entry(elem, struct frame, elem);
  return hash_bytes(&fte->kpage, sizeof(fte->kpage));
}

static bool frame_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  struct frame *fte_a = hash_entry(a, struct frame, elem);
  struct frame *fte_b = hash_entry(b, struct frame, elem);
  return fte_a->kpage < fte_b->kpage;
}
```

`frame_table`의 hash table인 `frame_table`을 초기화하고, `frame_table_lock`을 초기화한다.

#### Frame Allocation

Frame Table에 새로운 `frame`을 할당하기 위해 `frame_alloc()` 함수를 구현한다.

```c
void *frame_alloc(void *upage, enum palloc_flags flags) {
  void *kpage = palloc_get_page(flags);
  
  if (kpage == NULL) {
    kpage = frame_switch(upage, flags);
    return kpage;
  }
  
  struct frame *f = malloc(sizeof(struct frame));
  f->kpage = kpage;
  f->upage = upage;
  f->thread = thread_current();
  f->timestamp = timer_ticks();
  f->spte = NULL;

  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  hash_insert(&frame_table.frame_table, &f->elem);
  frame_pin(f->kpage);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
  return kpage;
}
```

`palloc_get_page()`를 통해 `kpage`을 할당받는다. 
할당받은 `kpage`을 가지고 `frame`을 생성한다.
`malloc()`을 통해 `frame`을 할당받고, `frame`의 멤버 변수들을 초기화한다.
이후 `frame_table`에 `frame`을 추가한다. 이때 `frame_table`에 대한 접근은 critical section으로, `frame_table_lock`을 이용하여 보호한다.
`frame_table`에 추가한 후 critical section을 벗어나기 전에 `frame`을 `frame_pin()`을 이용하여 `pin`한다.
`frame`의 pinning은 이후에 따로 설명한다.

만약 `palloc_get_page()`로 `frame`을 할당받지 못했다면, `frame_switch()`를 통해 `frame`을 할당받는다.
`frame_switch()`에 대한 내용은 이후에 설명한다.

#### Frame Deallocation

`frame`을 해제하기 위해 `frame_free()` 함수를 구현한다.

```c
void frame_free(void *kpage) {
  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  struct frame *f = frame_find(kpage);
  hash_delete(&frame_table.frame_table, &f->elem);
  palloc_free_page(kpage);
  free(f);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
}
```

`frame_free()`의 호출은 `frame`이 가지고 있던 정보가 적절한 backing store에 저장되었음을 가정한다.
`frame_table`에 대한 접근은 critical section으로, `frame_table_lock`을 이용하여 보호한다.
이후 `frame_table`에서 `frame`을 제거하고, `palloc_free_page()`를 통해 `kpage`을 해제한다.
마지막으로 `malloc()`으로 할당받았던 `frame`을 `free()`를 통해 해제한다.

#### Frame Switch

`palloc_get_page()`를 통해 `frame`을 할당받지 못했을 때, `frame_switch()`를 통해 `frame`을 할당받는다.

```c
void *frame_switch(void *upage, enum palloc_flags flags) {
  struct frame *target = frame_to_evict();
  struct thread *target_thread = target->thread;
  struct spt_entry *target_spte = target->spte;
  bool zero = flags & PAL_ZERO;

  if (target == NULL) {
    PANIC("Cannot find frame to evict");
  }

  target->upage = upage;
  target->thread = thread_current();
  target->timestamp = timer_ticks();
  target->spte = NULL;
  unload_page_data(&target_thread->spt, target_spte);

  if (zero)
    memset(target->kpage, 0, PGSIZE);

  return target->kpage;
}

static struct frame *frame_to_evict(void){
  struct frame *target = NULL;
  struct hash_iterator i;
  int64_t min = INT64_MAX;

  bool hold = lock_held_by_current_thread(&frame_table.frame_table_lock);
  if (!hold)
    lock_acquire(&frame_table.frame_table_lock);
  hash_first(&i, &frame_table.frame_table);
  while (hash_next(&i)) {
      struct frame *f = hash_entry(hash_cur(&i), struct frame, elem);
      if (f->pinned)
          continue;
      if (f->timestamp < min) {
          min = f->timestamp;
          target = f;
      }
  }
  frame_pin(target->kpage);
  if (!hold)
    lock_release(&frame_table.frame_table_lock);
  return target;
}
```

우선 `frame_switch()`는 `frame_to_evict()`를 통해 교체할 `frame`을 선택한다.
교체할 `frame`을 선택한 후, `frame`의 멤버 변수들을 새로운 `frame`에 맞게 수정한다.
기존의 `frame`에 매핑된 `spte`를 적절한 backing store에 저장하기 위해 `unload_page_data()`를 사용한다.
`unload_page_data()`에 대한 내용은 이후에 설명한다.
만약 `frame_alloc()`을 호출할 때 zero flag가 설정되어 있다면, `memset()`을 통해 `kpage`을 0으로 초기화한다.
마지막으로 `frame`의 `kpage`을 반환한다.

`frame_to_evict()`는 `frame`의 `timestamp`를 기준으로 가장 오래된 `frame`을 선택한다. (LRU)
`frame_table`에 대한 접근은 critical section으로, `frame_table_lock`을 이용하여 보호한다.
이때 `frame`의 `pinned` 여부를 확인하여 `pinned`된 `frame`은 선택하지 않는다.
`frame`이 결정되었다면 `frame_pin()`을 통해 `pin`한다.

#### Frame Pinning

Frame Pinning을 Synchronization을 위해 구현하였다.
Pin이 되어있는 Frame은 Eviction이 되지 않는다.
 
Frame은 오로지 `frame_alloc()`에서만 pin이 된다.
Frame unpin은 오로지 `load_page()`에서만 unpin이 된다.

따라서 frame이 pin이 되어있다는 것은 frame이 `frame_alloc()`에서 할당되었고, 아직 `load_page()`이 완료되지 않았음을 의미한다.
또한, `load_page()`가 완료되면 frame은 반드시 unpin이 되고, eviction의 대상이 될 수 있다.

## 2. Lazy Loading

Lazy Loading을 위해서는 `load_segment()`와 `page_fault()`를 수정해야한다.
`load_segment()`는 직접 `install_page()`를 호출하는 것이 아닌, 이후 설명할 Supplemental page table에 관련된 entry를 추가한다.
`page_fault()`는 fault가 발생한 주소를 이용하여 Supplemental page table을 탐색하고 valid한 page라면 load한다.
`load_segment()`에서 사용하는 `spt_insert_exec()`과 `page_fault()`에서 사용하는 `load_page()`는 이후 `Supplemental Page Table`에서 설명한다.

수정된 `load_segment()`는 다음과 같다.
```c
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
             uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  int offset = 0;
  struct thread *cur = thread_current();
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct spt_entry *spte = spt_insert_exec(&cur->spt, upage, writable, file, ofs + offset, page_read_bytes, page_zero_bytes);
    if (spte == NULL)
      return false;

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    offset += PGSIZE;
  }
  return true;
}
```

수정된 `page_fault()`는 다음과 같다.
```c
static void
page_fault(struct intr_frame *f) {
  bool not_present; /* True: not-present page, false: writing r/o page. */
  bool write;       /* True: access was write, false: access was read. */
  bool user;        /* True: access by user, false: access by kernel. */
  void *fault_addr; /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm("movl %%cr2, %0"
      : "=r"(fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  struct thread *cur = thread_current();
  void *fault_page = pg_round_down(fault_addr);
  void *esp = user ? f->esp : cur->intr_esp;
  struct spt *spt = &cur->spt;

  if (fault_addr < PHYS_BASE && not_present) {
    if (stack_growth(fault_addr, esp)) {
      spt_insert_stack(spt, fault_page);
      cur->stack_pages++;
    }
    struct spt_entry *spte = spt_find(spt, fault_page);
    if (spte != NULL) {
      if (load_page(spte))
        return;
      else {
        thread_current()->pcb->exit_code = -1;
        thread_exit();
      }
    }
  }

  // fault under PHYS_BASE access by kernel
  // => fault while accessing user memory
  if (fault_addr < PHYS_BASE && !user) {
    f->eip = (void (*)(void))(f->eax);
    f->eax = 0xffffffff;
    return;
  }

  thread_current()->pcb->exit_code = -1;
  thread_exit();
}
```

## 3. Supplemental Page Table

현재 Process의 valid한 page들과 현재 각 page의 위치 등을 관리하기 위해 `Supplemental Page Table`을 구현한다.

### Data Structure

Supplemental Page Table 구현을 위해 사용한 자료 구조는 다음과 같다.

```c
struct spt {
  struct hash table;
  void *pagedir;
};

struct spt_entry {
  void *upage;
  void *kpage;
  bool writable;
  bool dirty;

  enum spte_type type;
  enum page_location location;

  struct file_info *file_info;
  int swap_index;

  struct lock lock;
  struct hash_elem elem;
};
```

`spt`의 멤버 변수는 다음과 같다.
- `table` : `spte`을 저장하는 `hash table`
- `pagedir` : 현재 `spt`를 가지는 `thread`의 `pagedir`

`spt_entry`의 멤버 변수는 다음과 같다.
- `upage` : `user virtual address`
- `kpage` : `kernel virtual address`
- `writable` : `writable` 여부
- `dirty` : `dirty` 여부
- `type` : `spte`의 종류
- `location` : `spte`의 위치
- `file_info` : `spte`의 종류가 `EXEC`이나 `MMAP`일 경우, `file`에 대한 정보
- `swap_index` : `spte`의 위치가 `SWAP`일 경우, `swap`에 저장된 위치
- `lock` : `spte`에 대한 동시성 제어를 위한 `lock`
- `elem` : `spt`에 저장하기 위한 `hash_elem`

이중 `type`과 `location`은 다음과 같은 `enum`으로 정의된다.
```c
enum spte_type {
  MMAP,
  EXEC,
  STACK
};

enum page_location {
  LOADED,
  FILE,
  SWAP,
  ZERO
};
```

`spte_type` 각각의 의미는 다음과 같다.
- `MMAP` : mmap을 통해 연결된 page
- `EXEC` : 실행 파일에서 불러온 page
- `STACK` : stack page

`page_location` 각각의 의미는 다음과 같다.
- `LOADED` : `frame`에 로드된 page
- `FILE` : `file`에 저장된 page
- `SWAP` : `swap`에 저장된 page
- `ZERO` : 빈 페이지

`file_info`는 `spte`의 `type`이 `EXEC`나 `MMAP`일 경우, `file`에 대한 정보를 저장하기 위해 사용된다.

```c
struct file_info {
  struct file *file;
  off_t offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
};
```

파일에서 page를 load하기 위한 정보를 저장한다.

### Algorithms and Implementation

#### Supplemental Page Table Initialization

Supplemental Page Table은 각 프로세스마다 하나씩 존재하기 때문에, `start_process()`에서 초기화한다.

```c
void spt_init(struct spt *spt) {
  spt->pagedir = thread_current()->pagedir;
  hash_init(&spt->table, spt_hash, spt_less, NULL);
}

static unsigned spt_hash(const struct hash_elem *elem, void *aux) {
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  return hash_bytes(&spte->upage, sizeof(spte->upage));
}

static bool spt_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  struct spt_entry *spte_a = hash_entry(a, struct spt_entry, elem);
  struct spt_entry *spte_b = hash_entry(b, struct spt_entry, elem);
  return spte_a->upage < spte_b->upage;
}
```

#### Supplemental Page Table Destruction and Supplemental Table Entry Destruction

spt를 해제하기 위해 `spt_destroy()`를 구현한다.
`hash_destroy()`를 이용하여 spt를 해제하고, spte를 해제하기 위해 destructor `spte_destroy()`를 구현한다.

spt에서 page를 제거하기 위해 `spt_remove()`를 구현한다.
`spt_remove()`도 destructor `spte_destroy()`를 이용한다.

```c
void spt_destroy(struct spt *spt) {
  hash_destroy(&spt->table, spte_destroy);
}

void spt_remove(struct spt *spt, void *upage) {
  struct spt_entry *spte = spt_find(spt, upage);
  hash_delete(&spt->table, &spte->elem);
  spte_destroy(&spte->elem, NULL);
}
```

핵심 기능인 `spte_destroy()`는 이후 `On Process Termination`에서 설명한다.

#### Supplemental Page Table Insertion

`spt_entry`의 설명에서 살펴봤듯이 `spt`에 추가가능한 page의 종류는 3가지이다.
각각에 대해 `spt_insert_exec()`, `spt_insert_mmap()`, `spt_insert_stack()`을 구현한다.

```c
struct spt_entry *spt_insert_mmap(struct spt *spt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = true;
  spte->dirty = false;
  spte->type = MMAP;
  spte->location = FILE;
  spte->file_info = file_info_generator(file, offset, read_bytes, zero_bytes);
  spte->swap_index = -1;
  lock_init(&spte->lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  if (e == NULL) {
    return spte;
  } else {
    free(spte);
    return NULL;
  }
}

struct spt_entry *spt_insert_exec(struct spt *spt, void *upage, bool writable, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = writable;
  spte->dirty = false;
  spte->type = EXEC;
  spte->location = FILE;
  spte->file_info = file_info_generator(file, offset, read_bytes, zero_bytes);
  spte->swap_index = -1;
  lock_init(&spte->lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  if (e == NULL) {
    return spte;
  } else {
    free(spte);
    return NULL;
  }
}

struct spt_entry *spt_insert_stack(struct spt *spt, void *upage) {
  struct spt_entry *spte = malloc(sizeof(struct spt_entry));
  if (spte == NULL) {
    return NULL;
  }
  spte->upage = upage;
  spte->kpage = NULL;
  spte->writable = true;
  spte->dirty = false;
  spte->type = STACK;
  spte->location = ZERO;
  spte->file_info = NULL;
  spte->swap_index = -1;
  lock_init(&spte->lock);
  struct hash_elem *e = hash_insert(&spt->table, &spte->elem);
  if (e == NULL) {
      return spte;
  } else {
      free(spte);
      return NULL;
  }
}

static struct file_info *file_info_generator(struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes) {
  struct file_info *file_info = malloc(sizeof(struct file_info));
  file_info->file = file;
  file_info->offset = offset;
  file_info->read_bytes = read_bytes;
  file_info->zero_bytes = zero_bytes;
  return file_info;
}
```

각각의 함수는 `spt_entry`를 생성하고, `spt`에 추가한다.
생성하는 `spt_entry`의 `type`에 맞게 각각의 멤버 변수를 초기화해준다.
`file_info`는 `file`에 대한 정보를 저장하기 위해 `file_info_generator()`를 통해 생성한다.


## 4. Stack Growth

## 5. File Memory Mapping

## 6. Swap Table

## 7. On Process Termination
VM 관련 기능 구현을 위해 새로 추가한 데이터 구조들을 프로세스 종료시 정리해줘야한다. 

첫번째로, `process_exit()` 시에 `mmap` 해준 항목들과 `supplemental page table`을 정리해준다.
현재 스레드의 `mmap` 리스트를 참고하여, `mmap` 해준 파일들을 `munmap` 해준다. `supplemental_page_table` 은 
`hash_destory()` 를 이용해 정리한다. 이때 `spte_destroy()` 함수를 정의해 각 테이블의 엔트리에 대해 정리해준다. 
```c
void process_exit(void) {
#ifdef VM
  mmap_destroy(&cur->mmap_list);
  spt_destroy(&cur->spt);
#endif
  /* ... */
}
  
void mmap_destroy(struct mmap_list *mmap_list) {
    struct list_elem *e;
    while (!list_empty(&mmap_list->list)) {
        e = list_front(&mmap_list->list);
        struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
        mmap_unmap_file(mmap_list, mmap_entry->id);
        free(mmap_entry);
    }
}

void spt_destroy(struct spt *spt) {
  hash_destroy(&spt->table, spte_destroy);
}
```

자세한 `supplemental page table` 의 `entry` 의 정리는 다음과 같다. 
먼저 엔트리가 로드되어있는 상태라면 다음 로직을 거친다. 
동시성 문제를 위해 만약 엔트리가 `pin`이 되어있는지 체크하고, 안되어있다면, `pin` 을 함으로 eviction 을 막고, 만약, 
메모리에 올라가있는 페이지라면, `thread_yield()`를 한다. TODO: 이부분 살짝 이해 안되는데 추가 설명 좀 부탁드립니다!
그 후, page 를 unload 한다. 이 때, page 가 dirty 하다면, `spte` 의 `type` 에 따라 파일에 변경 사항을 다시 쓰거나, 
`EXEC, STACK` 일 경우, `swap_out` 한다.

이후, `spte` 가 스왑에 으면, `swap_free` 를 이용해 스왑을 해제한다.
마지막으로, `spte` 의 `type` 이 `EXEC, MMAP` 일 경우, `file_info` 를 해제한다.

```c
static void spte_destroy(struct hash_elem *elem, void *aux) {
  struct spt_entry *spte = hash_entry(elem, struct spt_entry, elem);
  lock_acquire(&spte->lock);
  if (spte->location == LOADED) {
    if (frame_test_and_pin(spte->kpage))
      unload_page(&thread_current()->spt, spte);
    else {
      lock_release(&spte->lock);
      while (spte->location == LOADED) {
        thread_yield();
      }
      lock_acquire(&spte->lock);
    }
  }
  if (spte->location == SWAP) {
    swap_free(spte->swap_index);
  }
  if (spte->type == EXEC || spte->type == MMAP) {
    free(spte->file_info);
  }
  lock_release(&spte->lock);
  free(spte);
}
```
