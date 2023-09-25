#ifndef   DATA_STRUCTURE_H
#define   DATA_STRUCTURE_H
#include "tsan_defs.h"
namespace __tsan {
enum NodeType {
  ROOT,
  FINISH,
  ASYNC_I,
  ASYNC_E,
  FUTURE,
  STEP,
  TASKWAIT,
  PARALLEL,
  NODE_TYPE_END
};

typedef struct TreeNode {
  int corresponding_id;
  NodeType node_type;
  int depth;
  int number_of_child;
  int is_parent_nth_child;
  int preceeding_taskwait;
  Sid sid;
  Epoch ev;
  TreeNode *parent;
  TreeNode *children_list_head;
  TreeNode *children_list_tail;
  TreeNode *next_sibling;
  // TreeNode() = default;
  // TreeNode(int task_id, int step_index, NodeType type, int depth, int nth_child,
  //          int preceeding_taskwait, TreeNode *parent)
  //     : corresponding_task_id(task_id),
  //       corresponding_step_index(step_index),
  //       this_node_type(type),
  //       depth(depth),
  //       number_of_child(0),
  //       is_parent_nth_child(nth_child),
  //       preceeding_taskwait(preceeding_taskwait),
  //       parent(parent),
  //       children_list_head(),
  //       children_list_tail(),
  //       next_sibling(),
  //       current_finish_node() {}
} TreeNode;


// class DPST{
//  public:
//   TreeNode *root;
//   TreeNode *current_step_node;
//   DPST() {
//     this->root = nullptr;
//     this->current_step_node = nullptr;
//   }
// };

// extern struct dpst DPST;

// typedef struct finish_t{
//     tree_node* node_in_dpst;
//     finish_t* parent;
// } finish_t;

// typedef struct task_t{
//   int id;
//   int parent_id;
//   tree_node* node_in_dpst;
//   finish_t* belong_to_finish;
//   finish_t* current_finish;
// } task_t;

} // namespace __tsan
#endif