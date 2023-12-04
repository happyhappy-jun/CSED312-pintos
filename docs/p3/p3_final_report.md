# Project 3: Virtual Memory

## Preliminaries

Team Number: 20

윤병준(20190766)

김치헌(20190806)

## 1. Frame Table

## 2. Lazy Loading

## 3. Supplemental Page Table

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
