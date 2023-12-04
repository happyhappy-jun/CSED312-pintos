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

#### Unload Page Data

`frame`에 저장된 데이터를 적절한 backing store에 저장하기 위해 `unload_page_data()`를 구현한다.

```c
bool unload_page_data(struct spt *spt, struct spt_entry *spte) {
  bool hold = lock_held_by_current_thread(&spte->lock);
  if (!hold)
      lock_acquire(&spte->lock);
  ASSERT(spte->location == LOADED)
  void *kpage = spte->kpage;
  bool dirty = spte->dirty;
  if (!dirty) {
    dirty = pagedir_is_dirty(spt->pagedir, spte->upage);
    spte->dirty = dirty;
  }
  spte->kpage = NULL;
  pagedir_clear_page(spt->pagedir, spte->upage);

  void *kbuffer = NULL;
  if (dirty) {
    kbuffer = palloc_get_page(PAL_ZERO);
    memcpy(kbuffer, kpage, PGSIZE);
  }

  switch (spte->type) {
  case MMAP:
    if (dirty) {
      unload_file(kbuffer, spte);
      spte->dirty = false;
    }
    spte->location = FILE;
    break;
  case EXEC:
    if (dirty) {
      unload_swap(kbuffer, spte);
      spte->location = SWAP;
    } else {
      spte->location = FILE;
    }
    break;
  case STACK:
    if (dirty) {
      unload_swap(kbuffer, spte);
      spte->location = SWAP;
    } else {
      spte->location = ZERO;
    }
    break;
  default:
    return false;
  }
  if (dirty)
    palloc_free_page(kbuffer);
  ASSERT(spte->location != LOADED)
  if (!hold)
      lock_release(&spte->lock);
  return true;
}

static void unload_file(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->file_info != NULL)

  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  int write_bytes = file_write_at(file_info->file, kbuffer, (int) file_info->read_bytes, file_info->offset);
  lock_release(&file_lock);
  if (write_bytes != (int) file_info->read_bytes) {
    PANIC("Failed to write file");
  }
}

static void unload_swap(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->swap_index == -1)

  spte->swap_index = (int) swap_out(kbuffer);
}
```

`unload_page_data()`는 우선 load된 data의 dirty 여부를 확인한다.
dirty 여부는 `spte`의 `dirty`와 `pagedir_is_dirty()`를 통해 확인한다.
이후, `spte`의 `kpage`를 `NULL`로 설정하고, `pagedir_clear_page()`를 통해 `pagedir`에서 `upage`를 제거한다.
이는 frame에서 unload 과정이 진행되는 동안 원래 주인 프로세스가 upage에 접근하여 내용을 읽거나 수정하는 것을 막기 위해서이다.
원래 주인 프로세스는 이 시점부터 upage 접근 시 page fault가 발생하고 이후 설명할 page fault handler에서 적절한 동작을 수행하여 다시 load한다.

만약 `dirty`라면, `kbuffer`를 할당받고, `memcpy()`를 통해 `kpage`의 데이터를 `kbuffer`에 복사한다.
`dirty`인 경우에만 `kbuffer`를 할당받는 이유는, `dirty`가 아닌 경우에는 `kpage`의 데이터가 `spte`에 원래 지정된 backing store에 저장되어 있기 때문이다.

`switch`문을 통해 `type`에 따라 `dirty`인 경우 데이터를 backing store에 저장한다.
`type`이 `EXEC`이라면, `unload_swap()`을 통해 `swap`에 데이터를 저장한다.
`type`이 `MMAP`이라면, `unload_file()`을 통해 `file`에 데이터를 저장한다.
`type`이 `STACK`이라면, `unload_swap()`을 통해 `swap`에 데이터를 저장한다.

저장을 완료한 후 저장한 위치에 맞게 `spte`의 `location`을 수정한다.
`dirty`가 아니라서 저장하지 않은 경우라면 `type`이 `EXEC`이나 `MMAP`이라면 `location`을 `FILE`로, `STACK`이라면 `location`을 `ZERO`로 수정한다.

이후, `kbuffer`를 해제하고, `spte`의 `location`이 `LOADED`가 아님을 확인한다.

모든 과정은 `spte`의 정보를 수정하기 때문에 `spte`의 `lock`을 이용하여 동시성을 보장한다.
`spte` 각각의 정보에 대한 내용은 이후 Supplemental Page에서 설명한다.


## 2. Lazy Loading

Lazy Loading을 위해서는 `load_segment()`와 `page_fault()`를 수정해야한다.

### Algorithms and Implementation

#### Load Segment
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
`load_segment()`는 직접 `install_page()`를 호출하는 것이 아닌, 이후 설명할 Supplemental page table에 관련된 entry를 추가한다.
`load_segment()`에서 사용하는 `spt_insert_exec()`은 이후 설명할 Supplemental page table에 실행파일 페이지 entry를 추가하는 함수이다.

#### Page Fault Handler and loading page

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
`page_fault()`는 `fault_addr`를 이용하여 Supplemental page table을 탐색하고 valid한 page라면 load한다.
`page_fault()`에서 `spt`를 탐색하고, valid한 page라면 load하기 위해 `load_page()`를 구현한다.

```c
bool load_page(struct spt_entry *spte) {
  bool hold = lock_held_by_current_thread(&spte->lock);
  if (!hold)
    lock_acquire(&spte->lock);
  void *kpage = frame_alloc(spte->upage, PAL_USER);
  bool success = load_page_data(kpage, spte);
  if (!hold)
      lock_release(&spte->lock);
  if (!success) {
    frame_free(kpage);
    return false;
  }
  frame_set_spte(kpage, spte);
  frame_unpin(kpage);
  return true;
}

bool load_page_data(void *kpage, struct spt_entry *spte) {
  bool hold = lock_held_by_current_thread(&spte->lock);
  if (!hold)
    lock_acquire(&spte->lock);
  ASSERT(spte->location != LOADED)
  void *kbuffer = palloc_get_page(PAL_ZERO);
  switch (spte->location) {
  case LOADED:
    PANIC("Page already loaded");
  case FILE:
    load_file(kbuffer, spte);
    break;
  case SWAP:
    load_swap(kbuffer, spte);
    break;
  case ZERO:
    memset(kbuffer, 0, PGSIZE);
    break;
  default:
    return false;
  }
  memcpy(kpage, kbuffer, PGSIZE);
  palloc_free_page(kbuffer);
  spte->location = LOADED;
  spte->kpage = kpage;
  bool result = install_page(spte->upage, spte->kpage, spte->writable);
  if (!hold)
      lock_release(&spte->lock);
  return result;
}

static void load_file(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->location == FILE)
  ASSERT(spte->file_info != NULL)
  struct file_info *file_info = spte->file_info;

  lock_acquire(&file_lock);
  int read_bytes = file_read_at(file_info->file, kbuffer, (int) file_info->read_bytes, file_info->offset);
  lock_release(&file_lock);
  memset(kbuffer + read_bytes, 0, (int) file_info->zero_bytes);

  if (read_bytes != (int) file_info->read_bytes) {
    PANIC("Failed to read file");
  }
}

static void load_swap(void *kbuffer, struct spt_entry *spte) {
  ASSERT(kbuffer != NULL)
  ASSERT(spte->location == SWAP)
  ASSERT(spte->swap_index != -1)

  swap_in(spte->swap_index, kbuffer);
  spte->swap_index = -1;
}
```

`load_page()`는 `frame_alloc()`을 통해 `frame`을 할당받고, `load_page_data()`를 통해 `frame`에 데이터를 로드한다.
`frame`에 데이터를 로드한 후, `frame`에 `spte`를 설정하고, `frame`을 `unpin`한다.

`load_page_data()`는 데이터 로드를 위해 `kbuffer`를 할당받고, `switch`문을 통해 `location`에 따라 데이터를 `kbuffer`에 로드한다.
`location`이 `LOADED`라면, 이미 `frame`에 로드된 page이므로 PANIC한다.
`location`이 `FILE`이라면, `load_file()`을 통해 `file`에서 데이터를 읽어온다.
`location`이 `SWAP`이라면, `load_swap()`을 통해 `swap`에서 데이터를 읽어온다.
`location`이 `ZERO`라면, `memset()`을 통해 `frame`을 0으로 초기화한다.

kbuffer에 정상적으로 데이터를 로드한 후, `memcpy()`를 통해 `kbuffer`의 데이터를 `frame`에 복사한다.
`frame`에 데이터를 로드한 후, `spte`의 `location`을 `LOADED`로 설정하고, `spte`의 `kpage`를 `frame`의 `kpage`로 설정한다.
이후 `install_page()`를 통해 `frame`을 `upage`에 매핑한다.

모든 과정은 `spte`의 정보를 수정하기 때문에 `spte`의 `lock`을 이용하여 동시성을 보장한다.

`spte` 각각의 정보에 대한 내용은 이후 Supplemental Page에서 설명한다.

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

`spt`는 프로세스마다 하나씩 가지고 있으며, `thread` 구조체에 추가하여 관리한다.

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
다만 `pagedir`은 `load()`에서 초기화되므로, 이후에 값을 적절히 초기화해야 한다.

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

`stack`의 크기를 늘리기 위해 `stack_growth()`를 구현한다.

### Data Structure

현재 프로세스가 몇 개의 stack page를 가지고 있는지 알기 위해 `stack_pages`를 `thread` 구조체에 추가한다.
또한, stack growth 상황을 판단하기 위해 `intr_esp`를 `thread` 구조체에 추가한다.

### Algorithms and Implementation

#### Setup Stack

최초의 stack은 lazy loading이 아니라 바로 load가 되어야하지만, evict 대상이 될 수 있기 때문에 여전히 spt에 추가될 필요가 있다.
따라서, `setup_stack()`을 다음과 같이 수정한다.

```c
static bool
setup_stack(void **esp) {
  bool success = false;
  struct thread *cur = thread_current();

  void *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  struct spt_entry *initial_stack = spt_insert_stack(&cur->spt, upage);
  success = load_page(initial_stack);
  if (success) {
    *esp = PHYS_BASE;
    cur->stack_pages = 1;
  } else {
    spt_remove(&cur->spt, initial_stack);
  }

  return success;
}
```

#### Stack Growth

우선, stack growth 상황에 대한 판단은 `stack_growth()`를 통해 한다. 구현은 다음과 같다.
```c
static bool stack_growth(void *fault_addr, void *esp) {
  if (thread_current()->stack_pages >= STACK_MAX_PAGES) return false;
  return esp - 32 <= fault_addr && fault_addr >= PHYS_BASE - STACK_MAX_PAGES * PGSIZE && fault_addr <= PHYS_BASE;
}
```

STACK_MAX_PAGES는 2048로 이는 stack size인 8MB를 PGSIZE로 나눈 값이다.

이때 사용하는 esp는
```c
void *esp = user ? f->esp : cur->intr_esp;
```
와 같이 계산된다. `cur->intr_esp`는 thread 구조체 추가한 멤버 변수이다. 
이런 계산이 필요한 이유는, kernel context인 syscall handler 도중에 page fault가 발생할 때 유저 프로세스의 esp가 intr_frame에 제대로 저장되지 않기 때문이다.
따라서, syscall handler가 불릴 때 cur->intr_esp에 esp를 저장해두고, page fault handler에서는 kernel context이라면 이를 사용한다.


stack growth 상황인지 판단한 후 spt에 새로운 stack page를 추가하고 Load한다.
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
    // lazy loading
    ...
  }

  // user memory access control
  ...

  thread_current()->pcb->exit_code = -1;
  thread_exit();
}
```


## 5. File Memory Mapping

## 6. Swap Table

## 7. On Process Termination
VM 관련 기능 구현을 위해 새로 추가한 데이터 구조들을 프로세스 종료시 정리해줘야한다. 

### Algorithms and Implementation

`process_exit()`에서 mmap_destroy()와 spt_destroy()를 호출하여 정리한다.
```c
void process_exit(void) {
#ifdef VM
  mmap_destroy(&cur->mmap_list);
  spt_destroy(&cur->spt);
#endif
  /* ... */
}
```

#### Memory map destroy
`process_exit()`시에 현재 스레드의 `mmap` 리스트를 참고하여, `mmap` 해준 파일들을 `munmap` 해준다. 
```c
void mmap_destroy(struct mmap_list *mmap_list) {
    struct list_elem *e;
    while (!list_empty(&mmap_list->list)) {
        e = list_front(&mmap_list->list);
        struct mmap_entry *mmap_entry = list_entry(e, struct mmap_entry, elem);
        mmap_unmap_file(mmap_list, mmap_entry->id);
        free(mmap_entry);
    }
}

```

`spt`는 `spt_destroy()`를 이용해 정리한다.

```c
void spt_destroy(struct spt *spt) {
  hash_destroy(&spt->table, spte_destroy);
}
```

`spt`의 각 `elem`의 정리를 위해 사용하는 destructor인 `spte_destroy()`을 다음과 같이 구현하여 사용한다.

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

`spte_destroy()`가 호출되면 우선 `spte`의 `lock`을 획득한다.
이후, `spte`의 `location`이 `LOADED`인 경우, `frame`에 로드된 상태이므로 `frame`을 `unload`해준다.
이때 연결된 frame의 pin 여부를 `frame_test_and_pin()`을 통해 확인한다. 
pin 여부를 확인했을 때 이미 pin된 frame이라면 이것은 다른 프로세스가 해당 frame을 점유하기 위해 `frame_switch()` 에서 pin을 걸어둔 것이다.
이런 경우라면 해당 process가 연결된 spte의 data를 unload 해줄 것이기 때문에 이를 busy waiting 방식으로 기다린다.
이때 busy waiting 동안에는 lock을 release 해준다.

`frame_test_and_pin()`은 `frame_table_lock`을 사용하여 frame table 접근에 대해 atomic하기 때문에 동시성 문제가 발생하지 않는다.
`frame_test_and_pin()`을 사용하여 pin을 성공적으로 진행했다면, `unload_page()`를 통해 직접 unload를 해준다.

```c
bool unload_page(struct spt *spt, struct spt_entry *spte) {
  bool hold = lock_held_by_current_thread(&spte->lock);
  if (!hold)
    lock_acquire(&spte->lock);
  void *kpage = spte->kpage;
  bool success = unload_page_data(spt, spte);
  if (!hold)
      lock_release(&spte->lock);
  if (!success) {
    return false;
  }
  frame_free(kpage);
  return true;
}
```

`unload_page()`는 `unload_page_data()`를 통해 `frame`에 저장된 데이터를 unload한다. 
사용하는 `unload_page_data()`는 Frame Table에서 설명한 내용이다.
`unload_page()`는 이후 추가로 `frame_free()`를 통해 frame을 해제한다.

`frame` 자체가 해제되기 때문에 unpin을 따로 해줄 필요는 없다.

`unload_page()` 과정이 끝나면 `spte->location`이 `SWAP`인지 확인하고 맞다면 연결된 swap slot을 해제해준다.
swap에 저장된 데이터는 `spte`가 프로세스로 부터 제거된다면 휘발되어도 상관이 없는 데이터이기 때문에 따로 다른 곳에 저장해줄 필요는 없다.

이후, `spte`의 `type`이 `EXEC`나 `MMAP`인 경우, `malloc()`으로 할당받았던 `file_info`를 해제해준다.
마지막으로 `spte` 자체를 해제해준다.

주의해야하는 점은 destructor인 `spte_destroy()`가 호출되는 시점에 `spte`는 이미 `spt`에서 제거된 상태이다.
따라서 `spte_destroy()`에서 호출하는 함수가 `upage`등을 이용해 `spt`에서 현재 제거 중인 `spte`를 찾으려고 하면 정의되지 않은 동작을 하게되니 주의해야한다.
















