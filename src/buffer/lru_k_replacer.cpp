#include "onebase/buffer/lru_k_replacer.h"
#include <limits>
#include "onebase/common/exception.h"

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> guard(latch_);

  bool found_candidate = false;
  bool found_inf_distance = false;
  frame_id_t victim = INVALID_FRAME_ID;
  size_t best_first_ts = std::numeric_limits<size_t>::max();
  size_t best_k_distance = 0;

  for (const auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }

    const auto &history = entry.history_;
    const auto first_ts = history.empty() ? std::numeric_limits<size_t>::max() : history.front();

    if (history.size() < k_) {
      // +inf backward k-distance. Among this group, choose earliest first access.
      if (!found_inf_distance || first_ts < best_first_ts) {
        found_candidate = true;
        found_inf_distance = true;
        victim = fid;
        best_first_ts = first_ts;
      }
      continue;
    }

    if (found_inf_distance) {
      continue;
    }

    // For entries with at least k accesses, largest backward k-distance wins.
    const size_t k_distance = current_timestamp_ - history.front();
    if (!found_candidate || k_distance > best_k_distance ||
        (k_distance == best_k_distance && first_ts < best_first_ts)) {
      found_candidate = true;
      victim = fid;
      best_k_distance = k_distance;
      best_first_ts = first_ts;
    }
  }

  if (!found_candidate) {
    return false;
  }

  entries_.erase(victim);
  curr_size_--;
  *frame_id = victim;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::lock_guard<std::mutex> guard(latch_);
  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_);
  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  auto &entry = it->second;
  if (entry.is_evictable_ == set_evictable) {
    return;
  }

  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
  entry.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> guard(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_) {
    throw OneBaseException("LRUKReplacer::Remove called on non-evictable frame");
  }

  entries_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() const -> size_t {
  std::lock_guard<std::mutex> guard(const_cast<std::mutex &>(latch_));
  return curr_size_;
}

}  // namespace onebase
