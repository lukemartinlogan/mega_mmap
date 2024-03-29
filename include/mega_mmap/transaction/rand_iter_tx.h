//
// Created by llogan on 3/11/24.
//

#ifndef MEGAMMAP_INCLUDE_MEGA_MMAP_TRANSACTION_RAND_TX_H_
#define MEGAMMAP_INCLUDE_MEGA_MMAP_TRANSACTION_RAND_TX_H_

#include "hermes_shm/util/random.h"

namespace mm {

class RandIterTx : public Tx {
 public:
  hshm::UniformDistribution gen_;
  hshm::UniformDistribution log_gen_;
  hshm::UniformDistribution prefetch_gen_;
  size_t size_;
  size_t base_ = 0;
  bitfield32_t flags_;
  size_t rand_left_;
  size_t rand_size_;
  size_t num_elmts_;
  size_t first_page_idx_;
  size_t first_page_shift_;
  size_t first_page_base_;
  size_t first_page_size_;
  size_t last_page_idx_;
  size_t last_page_size_;
  size_t num_pages_;
  size_t net_num_pages_;
  size_t last_prefetch_;

 public:
  RandIterTx(Vector *vec, size_t seed, size_t rand_left, size_t rand_size,
             size_t size, uint32_t flags) : Tx(vec) {
    gen_.Seed(seed);
    gen_.Shape((double)rand_left,
               (double)rand_left + (double)rand_size);
    size_ = size;
    rand_left_ = rand_left;
    rand_size_ = rand_size;
    flags_.SetBits(flags);
    log_gen_ = gen_;
    prefetch_gen_ = gen_;
    first_page_idx_ = rand_left_ / vec_->elmts_per_page_;
    first_page_shift_ = rand_left_ % vec_->elmts_per_page_;
    first_page_base_ =
        first_page_idx_ * vec_->elmts_per_page_ + first_page_shift_;
    first_page_size_ = vec_->elmts_per_page_ - first_page_shift_;
    last_page_idx_ = (rand_left_ + rand_size_) / vec_->elmts_per_page_;
    last_page_size_ = (rand_left_ + rand_size_) % vec_->elmts_per_page_;
    num_elmts_ = vec_->elmts_per_page_;
    num_pages_ = 0;
    net_num_pages_ = 0;
  }

  virtual ~RandIterTx() = default;

  void _ProcessLog(bool end) override {
    // Get number of pages iterated over
    size_t num_pages = num_pages_;
    if (!end && num_pages <= 1) {
      return;
    }
    if (!end) {
      num_pages -= 1;
    }

    // Evict processed pages
    for (size_t i = 0; i < num_pages; ++i) {
      size_t page_idx = log_gen_.GetSize() / vec_->elmts_per_page_;
      if (page_idx == first_page_idx_) {
        vec_->Rescore(page_idx,
                      first_page_shift_,
                      first_page_size_,
                      0, flags_);
      } else if (page_idx == last_page_idx_) {
        vec_->Rescore(page_idx,
                      0,
                      last_page_size_,
                      0, flags_);
      } else {
        vec_->Rescore(page_idx,
                      0,
                      vec_->elmts_per_page_,
                      0, flags_);
      }
    }
    num_pages_ = 0;

    // Prefetch future pages
    if (vec_->window_size_ >= vec_->cur_memory_ || end) {
      return;
    }
    for (size_t i = last_prefetch_; i <= net_num_pages_; ++i) {
      prefetch_gen_.GetSize();
      ++last_prefetch_;
    }
    size_t count = NumPrefetchPages(size_);
    for (size_t i = 0; i < count; ++i) {
      size_t page_idx = prefetch_gen_.GetSize() / vec_->elmts_per_page_;
      if (page_idx == first_page_idx_) {
        vec_->Rescore(page_idx,
                      first_page_shift_,
                      first_page_size_,
                      1.0, flags_);
      } else if (page_idx == last_page_idx_) {
        vec_->Rescore(page_idx,
                      0,
                      last_page_size_,
                      1.0, flags_);
      } else {
        vec_->Rescore(page_idx,
                      0,
                      vec_->elmts_per_page_,
                      1.0, flags_);
      }
    }
    last_prefetch_ += count;
  }

  size_t Get() {
    size_t off = tail_ % num_elmts_;
    if (off == 0) {
      size_t page_idx = (gen_.GetSize() / vec_->elmts_per_page_);
      if (page_idx == first_page_idx_) {
        num_elmts_ = first_page_size_;
        base_ = first_page_base_;
      } else if (page_idx == last_page_idx_) {
        num_elmts_ = last_page_size_;
        base_ = last_page_idx_ * vec_->elmts_per_page_;
      } else {
        num_elmts_ = vec_->elmts_per_page_;
        base_ = page_idx * vec_->elmts_per_page_;
      }
      num_pages_ += 1;
      net_num_pages_ += 1;
    }
    return base_ + off;
  }
};

}  // namespace mm

#endif //MEGAMMAP_INCLUDE_MEGA_MMAP_TRANSACTION_RAND_TX_H_
