#ifndef TSAN_AVLTREE_H
#define TSAN_AVLTREE_H

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_vector.h"

#define ASSERT(statement, msg, ...) \
  do {                              \
    if (!(statement)) {             \
      Printf(msg, ##__VA_ARGS__);   \
      Die();                        \
    }                               \
  } while (0)

namespace __tsan {
struct Interval {
  uptr left_end;   // included
  uptr right_end;  // excluded

  static const Interval UNIVERSAL;
  
  bool operator<(const Interval &other) const {
    if (this->right_end <= other.left_end) {
      return true;
    } else {
      return false;
    }
  }

  bool operator>(const Interval &other) const {
    if (this->left_end >= other.right_end) {
      return true;
    } else {
      return false;
    }
  }

  bool contains(const Interval &other) const {
    return this->left_end <= other.left_end &&
           this->right_end >= other.right_end;
  }

  bool operator==(const Interval &other) const {
    return this->left_end == other.left_end &&
           this->right_end == other.right_end;
  }

  bool operator!=(const Interval &other) const {
    return !(*this == other);
  }

  bool isOverlap(const Interval &other) const {
    return !contains(other) &&
           this->left_end < other.right_end && 
           this->right_end > other.left_end;
  }

};

struct MapInfo {
  uptr start;
  uptr size;
};

struct Node {
  Interval interval;  // address interval on the host
  MapInfo info;       // address interval on target
  Node *left_child;
  Node *right_child;
  Node *parent;
  int height;
  int index;

  Node(const Interval &interval, const MapInfo &info)
      : interval(interval),
        info(info),
        left_child(nullptr),
        right_child(nullptr),
        parent(nullptr),
        height(1),
        index(-1) {}
};

class IntervalTree {
 public:
  Node *root;
  u32 size;

  class Iterator {
   private:
    u32 idx;
    u32 size;
    Node *next_node;
    Node **stack;
    friend class IntervalTree;

   public:
    Iterator(Node *root, u32 size);

    Iterator(const Iterator &i);

    Iterator() = default;

    ~Iterator();

    Iterator &operator++() {
      if (idx < size) {
        next_node = stack[idx++];
      } else {
        next_node = nullptr;
      }
      return *this;
    }

    Node *operator*() const { return next_node; }

    bool operator==(const Iterator &i) const {
      return this->idx == i.idx;
    }

    bool operator!=(const Iterator &i) const { return !(*this == i); }
  };

  IntervalTree() : root(nullptr), size(0) {}

  Node *getRoot() { return root; }

  Node *find(const Interval &i) { return searchUtil(root, i); }

  bool insert(const Interval &interval, const MapInfo &info) {
    // Printf("try to insert interval: %p %p \n", interval.left_end,
    // interval.right_end);
    Node *n = insertUtil(root, interval, info);
    if (n == nullptr) {
      return false;
    }

    root = n;
    return true;
  }

  void remove(const Interval &i) { root = removeUtil(root, i); }

  bool isOverflow(uptr base, uptr addr);

  ~IntervalTree();

  Iterator begin() { return Iterator(root, size); }

  Iterator end() { return Iterator(nullptr, size); }

  Node *find(uptr begin, uptr size) { return find({begin, begin + size}); }

  int height(Node *head) {
    if (head == nullptr)
      return 0;
    return head->height;
  }

  void inorderUtil(Node *head) {
    if (head == nullptr)
      return;
    inorderUtil(head->left_child);
    Printf("%lu $$ %lu  ;;  ", head->interval.left_end,
           head->interval.right_end);
    inorderUtil(head->right_child);
  }

  bool satisfyBalanceFactor(Node *head) {
    if (!head) {
      return true;
    }
    int bf = height(head->left_child) - height(head->right_child);
    if (bf > 1 || bf < -1) {
      return false;
    } else {
      return (head->left_child ? satisfyBalanceFactor(head->left_child)
                               : true) &&
             (head->right_child ? satisfyBalanceFactor(head->right_child)
                                : true);
    }
  }

  void searchRange(Vector<Interval> &result, const Interval &range) {
    Node *node = searchUtil(root, range);
    if (node) {
      result.PushBack(node->interval);
    } else {
      searchRangeHelper(root, result, range, Interval::UNIVERSAL);
    }
  }

  void removeAllNodesWithinRange(const Interval &range);

  void printByHeight(Node *head);

 private:
  Node *rightRotation(Node *head);
  Node *leftRotation(Node *head);
  Node *insertUtil(Node *head, const Interval &interval, const MapInfo &info);
  Node *removeUtil(Node *head, const Interval &i);
  Node *searchUtil(Node *head, const Interval &i);
  void searchRangeHelper(Node *head, Vector<Interval> &result,
                         const Interval &range, const Interval &all);
};
}  // namespace __tsan
#endif  // TSAN_AVLTREE_H