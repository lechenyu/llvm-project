#include "tsan_avltree.h"

#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_mman.h"

namespace __tsan {

void IntervalTree::print_by_height(Node *head) {
  Printf("\n");
  if (head == nullptr) {
    return;
  }

  Vector<Node *> v1, v2;
  Vector<Node *> *curr = &v1, *next = &v2;
  curr->PushBack(head);
  u32 level = 0;
  while (true) {
    Printf("Level %u: ", level);
    uptr i = 0;
    while (i < curr->Size()) {
      Node *t = (*curr)[i];
      if (t == nullptr) {
        break;
      }
      if (t->parent == nullptr) {
        Printf("%p $$ %p (i: %d, p: %d)      ", (char *)(t->interval.left_end),
               (char *)(t->interval.right_end), t->index, -1);
      } else {
        Printf("%p $$ %p (i: %d, p: %d)      ", (char *)(t->interval.left_end),
               (char *)(t->interval.right_end), t->index, t->parent->index);
      }

      if (t->left_child != nullptr) {
        next->PushBack(t->left_child);
      }

      if (t->right_child != nullptr) {
        next->PushBack(t->right_child);
      }
      i += 1;
    }

    level++;
    if (next->Size() == 0) {
      break;
    }
    Vector<Node *> *temp = curr;
    curr = next;
    next = temp;
    next->Resize(0);
    Printf("\n");
  }
  Printf("\n");
}

int max(int a, int b) {
  if (a > b) {
    return a;
  }
  return b;
}

Node *IntervalTree::rightRotation(Node *head) {
  Node *newhead = head->left_child;
  head->left_child = newhead->right_child;
  newhead->right_child = head;

  newhead->parent = head->parent;
  if (head->left_child != nullptr) {
    head->left_child->parent = head;
  }
  newhead->right_child->parent = newhead;

  head->height = 1 + max(height(head->left_child), height(head->right_child));
  newhead->height =
      1 + max(height(newhead->left_child), height(newhead->right_child));
  return newhead;
}

Node *IntervalTree::leftRotation(Node *head) {
  Node *newhead = head->right_child;
  head->right_child = newhead->left_child;
  newhead->left_child = head;

  newhead->parent = head->parent;
  if (head->right_child != nullptr) {
    head->right_child->parent = head;
  }
  newhead->left_child->parent = newhead;

  head->height = 1 + max(height(head->left_child), height(head->right_child));
  newhead->height =
      1 + max(height(newhead->left_child), height(newhead->right_child));
  return newhead;
}

Node *IntervalTree::insertUtil(Node *head, const Interval &interval,
                               const MapInfo &info) {
  Node *n = nullptr;

  if (head == nullptr) {
    void *ptr = InternalAlloc(sizeof(Node));
    n = new (ptr) Node(interval, info);

    n->index = size;
    size += 1;
    return n;
  }

  bool left = true;
  if (head->interval > interval) {
    n = insertUtil(head->left_child, interval, info);
  } else if (head->interval < interval) {
    n = insertUtil(head->right_child, interval, info);
    left = false;
  } else {
    return nullptr;
  }

  if (n != nullptr) {
    if (left) {
      head->left_child = n;
    } else {
      head->right_child = n;
    }
    n->parent = head;
  }

  head->height = 1 + max(height(head->left_child), height(head->right_child));
  int bal = height(head->left_child) - height(head->right_child);
  if (bal > 1) {
    if (interval < head->left_child->interval) {
      return rightRotation(head);
    } else {
      head->left_child = leftRotation(head->left_child);
      return rightRotation(head);
    }
  } else if (bal < -1) {
    if (interval > head->right_child->interval) {
      return leftRotation(head);
    } else {
      head->right_child = rightRotation(head->right_child);
      return leftRotation(head);
    }
  }

  return head;
}

Node *IntervalTree::searchUtil(Node *head, const Interval &i) {
  if (head == nullptr) {
    return nullptr;
  }

  if (head->interval.contains(i)) {
    return head;
  }

  Node *next = (head->interval > i) ? head->left_child : head->right_child;
  if (next) {
    return searchUtil(next, i);
  } else {
    return nullptr;
  }
}

Node *IntervalTree::removeUtil(Node *head, const Interval &i) {
  if (head == nullptr)
    return nullptr;

  if (i < head->interval) {
    head->left_child = removeUtil(head->left_child, i);
  } else if (i > head->interval) {
    head->right_child = removeUtil(head->right_child, i);
  } else if (head->interval.contains(i)) {
    Node *p = head->parent;

    if (head->right_child == nullptr && head->left_child == nullptr) {
      if (head->parent != nullptr) {
        if (head->interval < head->parent->interval) {
          head->parent->left_child = nullptr;
        } else {
          head->parent->right_child = nullptr;
        }
      }
      InternalFree(head);
      head = nullptr;
    } else if (head->right_child == nullptr) {
      Node *l = head->left_child;
      InternalFree(head);
      head = l;
      head->parent = p;
    } else if (head->left_child == nullptr) {
      Node *r = head->right_child;
      InternalFree(head);
      head = r;
      head->parent = p;
    } else {
      Node *r = head->right_child;
      while (r->left_child != nullptr) r = r->left_child;
      head->interval = r->interval;
      head->info = r->info;

      head->right_child = removeUtil(head->right_child, r->interval);
    }
  }

  if (head == nullptr)
    return nullptr;
  if (head->parent != nullptr) {
    if (head->interval < head->parent->interval) {
      head->parent->left_child = head;
    } else {
      head->parent->right_child = head;
    }
  }

  head->height = 1 + max(height(head->left_child), height(head->right_child));
  int bal = height(head->left_child) - height(head->right_child);
// bug
  if (bal > 1) {
    if (height(head->left_child->left_child) >= height(head->left_child->right_child)) {
      return rightRotation(head);
    } else {
      head->left_child = leftRotation(head->left_child);
      return rightRotation(head);
    }
  } else if (bal < -1) {
    if (height(head->right_child->right_child) >= height(head->right_child->left_child)) {
      return leftRotation(head);
    } else {
      head->right_child = rightRotation(head->right_child);
      return leftRotation(head);
    }
  }

  return head;
}

bool IntervalTree::isOverflow(uptr base, uptr addr) {
  Node *baseNode = this->find({base, base + 1});
  if (baseNode) {
    Node *addrNode = this->find({addr, addr + 1});
    if (baseNode == addrNode) {
      return false;
    } else {
      return true;
    }
  } else {
    return false;
  }
}

IntervalTree::~IntervalTree() {
  if (root) {
    Vector<Node *> stack;
    stack.PushBack(root);
    while (stack.Size()) {
      Node *next = stack.Back();
      stack.PopBack();
      if (next->left_child != nullptr) {
        stack.PushBack(next->left_child);
      }
      if (next->right_child != nullptr) {
        stack.PushBack(next->right_child);
      }
      InternalFree(next);
    }
  }
}

int sortHelper(Node *n, Node **result, int next_idx) {
  if (n->left_child) {
    next_idx = sortHelper(n->left_child, result, next_idx);
  }
  result[next_idx++] = n;
  if (n->right_child) {
    next_idx = sortHelper(n->right_child, result, next_idx);
  }
  return next_idx;
}

IntervalTree::Iterator::Iterator(Node *root, u32 size)
    : idx(0), size(size), next_node(nullptr), stack(nullptr) {
  stack = reinterpret_cast<Node **>(InternalAlloc(sizeof(Node *) * size));
  sortHelper(root, stack, 0);
  next_node = stack[idx++];
}

IntervalTree::Iterator::Iterator(const Iterator &i)
    : idx(i.idx), size(i.size), next_node(nullptr), stack(nullptr) {
  if (i.stack) {
    stack = reinterpret_cast<Node **>(InternalAlloc(sizeof(Node *) * size));
    internal_memcpy(stack, i.stack, sizeof(Node *) * size);
    next_node = stack[idx - 1];
  }
}

IntervalTree::Iterator::~Iterator() {
  if (stack) {
    InternalFree(stack);
  }
}

}  // namespace __tsan
