#ifndef TSAN_AVLTREE_H
#define TSAN_AVLTREE_H

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_vector.h"         

#define ASSERT(statement, msg, ...)             \
    do {                                        \
        if (!(statement)) {                     \
            Printf(msg, ##  __VA_ARGS__ );      \
            Die();                              \
        }                                       \
    } while(0)     
    
namespace __tsan {

typedef enum ompt_mapping_op_t {
    ompt_mapping_alloc                   = 1,
    ompt_mapping_transfer_to_device      = 2,
    ompt_mapping_transfer_from_device    = 3,
    ompt_mapping_delete                  = 4,
    ompt_mapping_associate               = 5,
    ompt_mapping_disassociate            = 6
} ompt_mapping_op_t;

struct Interval {
    uptr left_end;   // included
    uptr right_end;  // excluded

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
        return this->left_end <= other.left_end && this->right_end >= other.right_end;
    }

    bool operator==(const Interval &other) const {
        return this->left_end == other.left_end && this->right_end == other.right_end;
    }
};

struct MapInfo {
    uptr start;
    uptr size;
};


struct Node {
    Interval interval;  // address interval on the host
    MapInfo info; // address interval on target
    Node *left_child;
    Node *right_child;
    Node *parent;
    int height;
    int index;

    Node(const Interval &interval, const MapInfo &info) :
        interval(interval),
        info(info),
        left_child(nullptr),
        right_child(nullptr), 
        parent(nullptr),
        height(1),
        index(-1) {}

    // bool insert(Node *n);

    // Node* find(const Interval &i);

    // Node* removeCurrentNode(const Interval &i, bool left_child_for_parent);

    // Node* remove(const Interval &i, bool left_child_for_parent);
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

        Iterator& operator++() {
            next_node = stack[idx++];
            return *this;
        }
        
        Node* operator*() const {
            return next_node;
        }

        bool operator==(const Iterator &i) const {
            return this->next_node == i.next_node; 
        }

        bool operator!=(const Iterator &i) const {
            return !(*this == i);
        }
    };

    IntervalTree() : root(nullptr), size(0) {}

    Node* getRoot() {
        return root;
    }

    Node* find(const Interval &i){
        return searchUtil(root, i);
    }

    bool insert(const Interval &interval, const MapInfo &info){
        Node* n = insertUtil(root, interval, info);
        if (n == nullptr){
            return false;
        }

        root = n;
        return true;
    }

    void remove(const Interval &i){
        root = removeUtil(root,i);
    }

    bool isOverflow(uptr base, uptr addr);

    ~IntervalTree();

    Iterator begin() {
        return Iterator(root->parent, size + 1);
    }

    Iterator end() {
        Iterator i{};
        i.next_node = root->parent;
        return i;
    }

    Node* find(uptr begin, uptr size) {
        return find({begin, begin + size});
    }

    int height(Node * head){
        if(head==nullptr) return 0;
        return head->height;
    }

    void inorderUtil(Node* head){
        if(head==nullptr) return ;
        inorderUtil(head->left_child);
        Printf("%lu $$ %lu  ;;  ", head->interval.left_end, head->interval.right_end);
        inorderUtil(head->right_child);
    }

    void print_by_height(Node* head, bool print_dpst);

private:
    Node* rightRotation(Node* head);
    Node* leftRotation(Node* head);
    Node* insertUtil(Node* head, const Interval &interval, const MapInfo &info);
    Node* removeUtil(Node* head, const Interval &i);
    Node* searchUtil(Node* head, const Interval &i);

};
} // namespace __tsan
#endif // TSAN_AVLTREE_H