#ifndef CONCURRENCY_VECTOR_H
#define CONCURRENCY_VECTOR_H

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "data_structure.h"

namespace __tsan {
class ConcurrencyVector {
 public:
  ConcurrencyVector(int capacity) : begin_(), cap_(capacity), size_({1}) {
    if (cap_) {
      begin_ = (TreeNode *)InternalAlloc(cap_ * sizeof(TreeNode));
      internal_memset(begin_, 0, cap_ * sizeof(TreeNode));
    }
  }
  
  ~ConcurrencyVector() {
    // if (begin_) {
    //   InternalFree(begin_);
    // }
  }

  TreeNode &operator[](uptr i) {
    DCHECK_LT(i, Size());
    return begin_[i];
  }

  TreeNode &EmplaceBack(NodeType type, int preceeding_taskwait, Sid sid, Epoch ev, TreeNode *parent) {
    int step_index = atomic_fetch_add(&size_, 1, memory_order_relaxed);
    if (UNLIKELY (step_index >= cap_)) {
      Printf("Concurrency vector reaches its capacity\n");
      internal__exit(1);
    }
    // new (begin_ + step_index) TreeNode(task_id, step_index, type, depth,
    //                                    nth_child, preceeding_taskwait,
    //                                    parent);
    begin_[step_index] = {step_index, type, parent->depth + 1, 0, parent->number_of_child, preceeding_taskwait, sid, ev, parent, nullptr, nullptr, nullptr};
    return begin_[step_index];
  }

  int Size() {
    return atomic_load_relaxed(&size_);
  }
 private:
  TreeNode *begin_;
  int cap_;
  atomic_sint32_t size_; // step_id start from 1

};

} // namespace __tsan
#endif // CONCURRENCY_VECTOR_H