#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_vector.h"
#include "tsan_rtl.h"
#include "concurrency_vector.h"

namespace __tsan{

extern ConcurrencyVector step_nodes;

// dpst root
TreeNode dpst_root;

static const char* node_desc[] = {"Root", "TaskGroup", "Async_I", "Async_E", "Future", "Step", "TaskWait", "Parallel"};

// static int node_index_base = 0;
// atomic_uint32_t *atomic_node_index = reinterpret_cast<atomic_uint32_t *>(&node_index_base);

// static u32 step_index_base = 1;
// atomic_uint32_t *atomic_step_index = reinterpret_cast<atomic_uint32_t *>(&step_index_base);

extern "C" {
INTERFACE_ATTRIBUTE
TreeNode* __tsan_get_step_in_tls(){
  ThreadState* current_thread = cur_thread();
  return &step_nodes[current_thread->step_id];
}

INTERFACE_ATTRIBUTE
void __tsan_reset_step_in_tls(){
  ThreadState* current_thread = cur_thread();
  current_thread->step_id = kNullStepId;
}

INTERFACE_ATTRIBUTE
void __tsan_set_step_in_tls(int step_id){
    // Printf("new task node index is %d \n", node->index);
  ThreadState* thr = cur_thread();
  thr->step_id = step_id;
}

INTERFACE_ATTRIBUTE
TreeNode *__tsan_init_DPST() {
  ThreadState *thr = cur_thread();
  dpst_root.corresponding_id = 0;
  dpst_root.node_type = ASYNC_I;
  dpst_root.depth = 0;
  dpst_root.number_of_child = 0;
  dpst_root.is_parent_nth_child = 0;
  dpst_root.preceeding_taskwait = kNullStepId;
  dpst_root.sid = thr->fast_state.sid();
  dpst_root.ev = thr->fast_state.epoch();
  dpst_root.parent = nullptr;
  dpst_root.children_list_head = nullptr;
  dpst_root.children_list_tail = nullptr;
  dpst_root.next_sibling = nullptr;
  return &dpst_root;
}

/**
 * @brief  Create a new tree node
 * @note   
 * @retval The newly created tree node
 */
INTERFACE_ATTRIBUTE
TreeNode *__tsan_alloc_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait)
{
  // Allocate memory for new node
  // tree_node* node = new tree_node();
  TreeNode *node = (TreeNode *)InternalAlloc(sizeof(TreeNode));
  node->corresponding_id = internal_node_id;
  node->node_type = node_type;
  node->depth = parent->depth + 1;
  node->number_of_child = 0;
  node->is_parent_nth_child = parent->number_of_child;
  node->preceeding_taskwait = preceeding_taskwait;
  node->parent = parent;
  node->children_list_head = nullptr;
  node->children_list_tail = nullptr;
  node->next_sibling = nullptr;
  // Printf("new %p internal id %d, type %d, parent %p\n", node, internal_node_id, node_type, parent);
  return node;
}


/**
 * @brief  Insert a tree node (not a step node) under parent
 * @note   
 * @param  nodeType: root,async,future or finish
 * @param  *parent: parent node, the inserted node will become a child of parent
 * @retval the newly inserted node
 */
INTERFACE_ATTRIBUTE
TreeNode *__tsan_insert_internal_node(TreeNode *node, TreeNode *parent) {  
  parent->number_of_child += 1; 
  if (parent->children_list_head) {
    parent->children_list_tail->next_sibling = node;
    parent->children_list_tail = node;
  } else {
    parent->children_list_head = node;
    parent->children_list_tail = node;
  }
  return node;
}

INTERFACE_ATTRIBUTE
TreeNode *__tsan_alloc_insert_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait) {
  TreeNode *node = __tsan_alloc_internal_node(internal_node_id, node_type, parent, preceeding_taskwait);
  __tsan_insert_internal_node(node, parent);
  return node;
}

/**
 * @brief  Insert a new leaf(step) node to the task_node
 * @note   
 * @param  *task_node: the node that will have a new leaf
 * @retval the newly inserted leaf(step) node
 */
INTERFACE_ATTRIBUTE
TreeNode *__tsan_insert_leaf(TreeNode *parent, int preceeding_taskwait) {
  ThreadState *thr = cur_thread();
  TreeNode &n = step_nodes.EmplaceBack(
      STEP, preceeding_taskwait,
      thr->fast_state.sid(), thr->fast_state.epoch(), parent);
  TreeNode *new_step = &n;
  parent->number_of_child += 1;

  if (parent->children_list_head == nullptr) {
    parent->children_list_head = new_step;
    parent->children_list_tail = new_step;
  } else {
    parent->children_list_tail->next_sibling = new_step;
    parent->children_list_tail = new_step;
  }

  // u32 step_index = atomic_load(atomic_step_index, memory_order_acquire);
  // atomic_store(atomic_step_index,step_index+1,memory_order_consume);

  // new_step->corresponding_step_index = step_index;
  // step_nodes[step_index] = new_step;
  // Printf("insert into %d\n", step_index);

  return new_step;
}

static void print_node(TreeNode *t) {
  constexpr uptr mask = 0xFFFFUL;
  Printf("[%s_%lu]", node_desc[t->node_type],
         t->corresponding_id ? t->corresponding_id : (uptr)t & mask);
  TreeNode *p = t->parent;
  if (p) {
    Printf("(%s_%lu[%u])", node_desc[p->node_type],
           p->corresponding_id ? p->corresponding_id : (uptr)p & mask,
           t->is_parent_nth_child);
  }
}

static void get_DPST_statistics(u32 *counts, int &max_depth, bool print_dpst) {
  if (print_dpst) {
    Printf("DPST Structure: \n");
  }
  Vector<TreeNode *> v1, v2;
  Vector<TreeNode *> *curr = &v1, *next = &v2;
  curr->PushBack(&dpst_root);
  u32 level = 0;
  while (curr->Size()) {
    if (print_dpst) {
      Printf(" Level %u: ", level);
    }
    for (TreeNode *t : *curr) {
      if (print_dpst) {
        print_node(t);
        Printf(" ");
      }
      max_depth = (max_depth < t->depth) ? t->depth : max_depth;
      counts[t->node_type]++;
      for (TreeNode *c = t->children_list_head; c != nullptr;
           c = c->next_sibling) {
        next->PushBack(c);
      }
    }
    level++;
    Swap(curr, next);
    next->Clear();
    if (print_dpst) {
      Printf("\n");
    }
  }
}

INTERFACE_ATTRIBUTE
void __tsan_print_DPST_info(bool print_dpst) {
  Printf("=============================================================\n");
  u32 node_counts[NODE_TYPE_END]{};
  int max_depth{};
  u32 num_nodes{};
  get_DPST_statistics(node_counts, max_depth, print_dpst);
  Printf("\nDPST Statistics:\n");
  for (u32 i = 0; i < NODE_TYPE_END; i++) {
    Printf("%s nodes: %u\n", node_desc[i], node_counts[i]);
    num_nodes += node_counts[i];
  }
  Printf("Total number of nodes: %u\n", num_nodes);
  Printf("Max depth: %d\n", max_depth);
  Printf("=============================================================\n");
}

} // extern C
} // namespace __tsan