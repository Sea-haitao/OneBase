#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  frame_id_t frame_id = INVALID_FRAME_ID;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (!replacer_->Evict(&frame_id)) {
    return nullptr;
  }

  auto &frame = pages_[frame_id];
  if (frame.page_id_ != INVALID_PAGE_ID) {
    if (frame.is_dirty_) {
      disk_manager_->WritePage(frame.page_id_, frame.GetData());
    }
    page_table_.erase(frame.page_id_);
  }

  const page_id_t new_page_id = disk_manager_->AllocatePage();
  *page_id = new_page_id;

  frame.ResetMemory();
  frame.page_id_ = new_page_id;
  frame.pin_count_ = 1;
  frame.is_dirty_ = false;

  page_table_[new_page_id] = frame_id;
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &frame;
}

auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    auto &frame = pages_[it->second];
    frame.pin_count_++;
    replacer_->RecordAccess(it->second);
    replacer_->SetEvictable(it->second, false);
    return &frame;
  }

  frame_id_t frame_id = INVALID_FRAME_ID;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (!replacer_->Evict(&frame_id)) {
    return nullptr;
  }

  auto &frame = pages_[frame_id];
  if (frame.page_id_ != INVALID_PAGE_ID) {
    if (frame.is_dirty_) {
      disk_manager_->WritePage(frame.page_id_, frame.GetData());
    }
    page_table_.erase(frame.page_id_);
  }

  disk_manager_->ReadPage(page_id, frame.GetData());
  frame.page_id_ = page_id;
  frame.pin_count_ = 1;
  frame.is_dirty_ = false;

  page_table_[page_id] = frame_id;
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  return &frame;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  auto &frame = pages_[it->second];
  if (frame.pin_count_ <= 0) {
    return false;
  }

  frame.pin_count_--;
  frame.is_dirty_ = frame.is_dirty_ || is_dirty;
  if (frame.pin_count_ == 0) {
    replacer_->SetEvictable(it->second, true);
  }
  return true;
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    disk_manager_->DeallocatePage(page_id);
    return true;
  }

  const frame_id_t frame_id = it->second;
  auto &frame = pages_[frame_id];
  if (frame.pin_count_ > 0) {
    return false;
  }

  replacer_->SetEvictable(frame_id, true);
  replacer_->Remove(frame_id);
  page_table_.erase(it);

  frame.ResetMemory();
  frame.page_id_ = INVALID_PAGE_ID;
  frame.pin_count_ = 0;
  frame.is_dirty_ = false;
  free_list_.push_back(frame_id);

  disk_manager_->DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  auto &frame = pages_[it->second];
  disk_manager_->WritePage(page_id, frame.GetData());
  frame.is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard<std::mutex> guard(latch_);

  for (size_t i = 0; i < pool_size_; ++i) {
    auto &frame = pages_[i];
    if (frame.page_id_ == INVALID_PAGE_ID) {
      continue;
    }
    disk_manager_->WritePage(frame.page_id_, frame.GetData());
    frame.is_dirty_ = false;
  }
}

}  // namespace onebase
