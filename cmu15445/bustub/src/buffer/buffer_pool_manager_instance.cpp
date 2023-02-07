//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }

  // TODO(students): remove this line after you have implemented the buffer pool manager
  // throw NotImplementedException(
  //    "BufferPoolManager is not implemented yet. If you have finished implementing BPM, please remove the throw "
  //    "exception line in `buffer_pool_manager_instance.cpp`.");
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  if (IsFullPage()) {
    // printf("new full\n");
    return nullptr;
  }
  if (!free_list_.empty()) {
    frame_id_t frame_id = 0;
    GetListFront(&frame_id);
    return AllocateNewFrame(frame_id, page_id);
  }
  frame_id_t frame_id = 0;
  GetAndEvict(&frame_id);
  return AllocateNewFrame(frame_id, page_id);
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);
  // printf("fetch %d\n", page_id);
  frame_id_t frame_id1 = 0;
  if (!page_table_->Find(page_id, frame_id1)) {
    if (IsFullPage()) {
      return nullptr;
    }
    frame_id_t frame_id = 0;
    if (!free_list_.empty()) {
      GetListFront(&frame_id);
    } else {
      GetAndEvict(&frame_id);
    }
    pages_[frame_id].page_id_ = page_id;
    pages_[frame_id].is_dirty_ = false;
    pages_[frame_id].pin_count_ = 1;
    page_table_->Insert(page_id, frame_id);
    replacer_->SetEvictable(frame_id, false);
    replacer_->RecordAccess(frame_id);
    disk_manager_->ReadPage(page_id, pages_[frame_id].data_);
    return &pages_[frame_id];
  }

  // I am right ?
  pages_[frame_id1].pin_count_++;
  replacer_->SetEvictable(frame_id1, false);
  replacer_->RecordAccess(frame_id1);
  return &pages_[frame_id1];
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // ("unpin %d %d\n", page_id, static_cast<int>(is_dirty));
  frame_id_t my_frame = 0;
  if (!page_table_->Find(page_id, my_frame)) {
    // to do
    // printf("a try\n");
    return true;
  }
  // the position is right ?
  if (is_dirty) {
    pages_[my_frame].is_dirty_ = is_dirty;
  }

  if (pages_[my_frame].pin_count_ <= 0) {
    return false;
  }

  if ((--(pages_[my_frame].pin_count_)) == 0) {
    replacer_->SetEvictable(my_frame, true);
  }
  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  frame_id_t frame_id = 0;
  // printf("flush %d\n", page_id);
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }
  // page_table_->Remove(page_id);
  // replacer_->SetEvictable(frame_id, true);
  disk_manager_->WritePage(page_id, pages_[frame_id].data_);
  pages_[frame_id].is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  // printf("flush all\n");
  for (size_t i = 0; i < pool_size_; i++) {
    FlushPgImp(pages_[i].page_id_);
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // printf("delete pageid %d\n", page_id);
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    return true;
  }
  if (pages_[frame_id].pin_count_ > 0) {
    return false;
  }
  page_table_->Remove(page_id);
  replacer_->Remove(frame_id);
  replacer_->SetEvictable(frame_id, true);
  free_list_.push_front(frame_id);
  memset(pages_[frame_id].data_, 0, sizeof(pages_[frame_id].data_));
  pages_[frame_id].is_dirty_ = false;
  pages_[frame_id].pin_count_ = 0;
  pages_[frame_id].page_id_ = INVALID_PAGE_ID;
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
