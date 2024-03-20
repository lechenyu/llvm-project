#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <map>

#include <nlohmann/json.hpp>
#include "omp-tools.h"
#include "dlfcn.h"
#include "taskdep_predefined.h"

#define GRAPH_MACRO
#define DPST_MACRO
// #define STACK_INFO
// #define DEBUG_INFO

#ifdef GRAPH_MACRO
  #include "graph-predefined.h"

  using json = nlohmann::json;

  json get_datamove_json();

  std::mutex race_map_mutex;

  #ifdef DPST_MACRO
  std::atomic_uint step_id_counter(1);
  #endif
#endif

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

enum class Sid : uint8_t {};

enum class Epoch : uint16_t {};
typedef struct TreeNode {
 public:
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
} TreeNode;

// static constexpr TreeNode *kNotOpenMPTask = nullptr;
static constexpr int kNullStepId = -1;
static constexpr size_t kDefaultStackSize = 1024;
static TreeNode *root = nullptr;
static TreeNode kNullTaskWait = {kNullStepId, TASKWAIT};

#ifdef GRAPH_MACRO
  static unsigned int target_begin_node = 0;
  static unsigned int target_end_node = 0;
  std::vector<targetRegion> *trm;
  std::vector<DataMove> *temp_dmv;
  std::map<std::pair<vertex_t, vertex_t>, race_info> *race_map;
#endif

// Data structure to store additional information for task dependency.
// each variable has a dependencyData
// For example, depend(out:i) depend(in:j)
// We will have one DependencyData for i, one for j
struct DependencyData final : DataPoolEntry<DependencyData> {
  std::vector<TreeNode*> in = std::vector<TreeNode*>();
  TreeNode* out = nullptr;
  TreeNode* inoutset = nullptr;

  std::mutex mutex_;

  void Reset() {}

  static DependencyData *New() { 
    return DataPoolEntry<DependencyData>::New();
  }

  DependencyData(DataPool<DependencyData> *dp)
      : DataPoolEntry<DependencyData>(dp) {}
};

class task_t;
class finish_t {
public:
  std::atomic<TreeNode *> node_in_dpst;
  finish_t *parent;
  task_t *belonging_task;
  void *corresponding_pr; 
public:
  finish_t(TreeNode *node, finish_t *parent, task_t *task)
      : node_in_dpst(node), parent(parent), belonging_task(task), corresponding_pr(nullptr) {}
};

class task_t {
public:
  TreeNode *node_in_dpst;
  finish_t *current_finish;
  finish_t *enclosed_finish;
  TreeNode *current_taskwait;
  TreeNode *previous_taskwait;
  int current_step_id;
  bool initialized;
  unsigned int index;
  void *parallel_region;
  bool added_to_others_join;

  std::vector<TreeNode*> depending_task_nodes;

  task_t* parent_task;
  unsigned int execution;
  // Dependency information for this task.
  // TaskDependency *Dependencies{nullptr};

  // Number of dependency entries.
  unsigned DependencyCount{0};

  // We expect a rare need for the dependency-map, so alloc on demand
  std::unordered_map<void *, DependencyData *> *DependencyMap{nullptr};

  bool IsOnTarget{false};
public:
  task_t(TreeNode *node, finish_t *enclosed_finish, bool initialized, unsigned int index, void *pr, task_t* parent)
    : node_in_dpst(node), current_finish(nullptr),
      enclosed_finish(enclosed_finish), current_taskwait(&kNullTaskWait), previous_taskwait(&kNullTaskWait),
      current_step_id(kNullStepId), initialized(initialized),
      index(index), parallel_region(pr), added_to_others_join(false), 
      parent_task(parent), execution(0) {
        if(parent != nullptr){
          this->IsOnTarget = parent->IsOnTarget;
        }
      }
};

static void drawDependEdges(task_t *task, unsigned int current_step){
#ifdef GRAPH_MACRO
  for(TreeNode* previous_task_node : task->depending_task_nodes){
    if(previous_task_node == nullptr){
      continue;
    }

    // printf("%d ", previous_task_node->children_list_tail->corresponding_id);
    vertex_t join_parent = (unsigned int) previous_task_node->children_list_tail->corresponding_id;
    // TODO: check if the edge already exists before addEdge
    addEdge(join_parent, current_step, edge_type::JOIN_E);
    
  }
#endif
}

static void AcquireAndReleaseDependencies(task_t *task, DependencyData* dd, ompt_dependence_type_t type) {
  // TODO: this lock will suppress race report
  // std::lock_guard<std::mutex> lock(dd->mutex_);

  bool has_out = (dd->out != nullptr);
  bool has_inoutset = (dd->inoutset != nullptr);
  std::vector<TreeNode*> &task_depending_nodes = task->depending_task_nodes;

  if (type == ompt_dependence_type_out || type == ompt_dependence_type_inout || type == ompt_dependence_type_mutexinoutset) {
    //acquire
    for(TreeNode* node: dd->in){
      task_depending_nodes.push_back(node);
    }

    if(has_out) task_depending_nodes.push_back(dd->out);
    if(has_inoutset) task_depending_nodes.push_back(dd->inoutset);

    //release
    dd->out = task->node_in_dpst;
    dd->in.clear();
  } 
  else if (type == ompt_dependence_type_in) {
    //acquire
    if(has_out) task_depending_nodes.push_back(dd->out);
    if(has_inoutset) task_depending_nodes.push_back(dd->inoutset);

    //release
    dd->in.push_back(task->node_in_dpst);
  } 
  else if (type == ompt_dependence_type_inoutset) {
    //acquire
    for(TreeNode* node: dd->in){
      task_depending_nodes.push_back(node);
    }
    if(has_out) task_depending_nodes.push_back(dd->out);

    //release
    dd->inoutset = task->node_in_dpst;
    dd->in.clear();
  }
}

class parallel_t {
public:
  unsigned int parallelism;
  std::atomic<int> remaining_task;
  task_t *encounter_task;
  bool IsOnTarget{false};
  bool isTeams{false};

#ifdef GRAPH_MACRO
  vertex_t start_cg_node;
  std::vector<vertex_t> end_nodes_to_join;
  std::mutex pr_mutex;
#endif
public:
  parallel_t(int parallelism, task_t *task)
      : parallelism(parallelism), remaining_task(parallelism),
        encounter_task(task) {}

#ifdef GRAPH_MACRO
  parallel_t(int parallelism, task_t *task, vertex_t node)
    : parallelism(parallelism), remaining_task(parallelism),
      encounter_task(task), start_cg_node(node) {
        this->end_nodes_to_join = std::vector<vertex_t>(parallelism, 0);
        this->IsOnTarget = task->IsOnTarget;
      }
#endif

  int count_down_on_barrier() {
    int val = remaining_task.fetch_sub(1, std::memory_order_acq_rel);
    return val;
  }
  void reset_remaining_task() {
    remaining_task.store(parallelism, std::memory_order_release);
  }
};

#ifdef DPST_MACRO
TreeNode dpst_root;
#endif

extern "C" {

#ifdef DPST_MACRO
void __tsan_reset_step_in_tls(){
  return;
}

void __tsan_set_step_in_tls(int step_id){
  return;
}

TreeNode *__tsan_init_DPST() {
  // ThreadState *thr = cur_thread();
  dpst_root.corresponding_id = 0;
  dpst_root.node_type = ASYNC_I;
  dpst_root.depth = 0;
  dpst_root.number_of_child = 0;
  dpst_root.is_parent_nth_child = 0;
  dpst_root.preceeding_taskwait = kNullStepId;
  // dpst_root.sid = nullptr;
  // dpst_root.ev = nullptr;
  dpst_root.parent = nullptr;
  dpst_root.children_list_head = nullptr;
  dpst_root.children_list_tail = nullptr;
  dpst_root.next_sibling = nullptr;
  return &dpst_root;
}


TreeNode *__tsan_alloc_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait)
{
  // Allocate memory for new node
  // tree_node* node = new tree_node();
  // TreeNode *node = (TreeNode *)InternalAlloc(sizeof(TreeNode));
  TreeNode *node = new TreeNode();
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

TreeNode *__tsan_alloc_insert_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait) {
  TreeNode *node = __tsan_alloc_internal_node(internal_node_id, node_type, parent, preceeding_taskwait);
  __tsan_insert_internal_node(node, parent);
  return node;
}


TreeNode *__tsan_insert_leaf(TreeNode *parent, int preceeding_taskwait) {
  // ThreadState *thr = cur_thread();
  // TreeNode &n = step_nodes->EmplaceBack(
  //     STEP, preceeding_taskwait,
  //     thr->fast_state.sid(), thr->fast_state.epoch(), parent);

  unsigned int step_index = step_id_counter.fetch_add(1, std::memory_order_relaxed);
  TreeNode *new_step = new TreeNode();
  new_step->corresponding_id = step_index;
  new_step->node_type = STEP;
  new_step->depth = parent->depth + 1;
  new_step->number_of_child = 0;
  new_step->is_parent_nth_child = parent->number_of_child;
  new_step->preceeding_taskwait = preceeding_taskwait;
  new_step->parent = parent;
  new_step->children_list_head = nullptr;
  new_step->children_list_tail = nullptr;
  new_step->next_sibling = nullptr;

  parent->number_of_child += 1;

  if (parent->children_list_head == nullptr) {
    parent->children_list_head = new_step;
    parent->children_list_tail = new_step;
  } else {
    parent->children_list_tail->next_sibling = new_step;
    parent->children_list_tail = new_step;
  }

  return new_step;
}
#else // DPST_MACRO


void __attribute__((weak)) __tsan_print();

TreeNode __attribute__((weak)) *__tsan_init_DPST();

TreeNode *__attribute__((weak))
__tsan_insert_internal_node(TreeNode *node, TreeNode *parent);

TreeNode *__attribute__((weak))
__tsan_alloc_insert_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

TreeNode *__attribute__((weak)) __tsan_insert_leaf(TreeNode *parent, int preceeding_taskwait);

void __attribute__((weak)) __tsan_print_DPST_info(bool print_dpst);

void __attribute__((weak)) __tsan_reset_step_in_tls();

void __attribute__((weak)) __tsan_set_step_in_tls(int step_id);

void __attribute__((weak)) AnnotateNewMemory(const char *f, int l, const volatile void *mem, size_t size);

TreeNode *__attribute__((weak)) __tsan_get_step_in_tls();

void __attribute__((weak)) __tsan_set_ompt_print_function(void (*f)());

void __attribute__((weak)) __tsan_set_report_race_steps_function(void (*f)(unsigned int, unsigned int));

void __attribute__((weak)) __tsan_set_report_race_stack_function(void (*f)(char*, bool, unsigned int, unsigned int));

void __attribute__((weak)) __tsan_set_report_current_racy_stack_function(void (*f)(char*, unsigned int, unsigned int, unsigned int));

char* __attribute__((weak)) __tsan_symbolize_pc(void* codeptr_ra, unsigned int &line, unsigned int &col);

#endif // DPST_MACRO  
} // extern "C"


#define SET_OPTIONAL_CALLBACK_T(event, type, result, level)                    \
  do {                                                                         \
    ompt_callback_##type##_t ta_##event = &ompt_ta_##event;                    \
    result = ompt_set_callback(ompt_callback_##event,                          \
                               (ompt_callback_t)ta_##event);                   \
    if (result < level)                                                        \
      printf("Registered callback '" #event "' is not supported at " #level    \
             " (%i)\n",                                                        \
             result);                                                          \
  } while (0)

#define SET_CALLBACK_T(event, type)                                            \
  do {                                                                         \
    int res;                                                                   \
    SET_OPTIONAL_CALLBACK_T(event, type, res, ompt_set_always);                \
  } while (0)

#define SET_CALLBACK(event) SET_CALLBACK_T(event, event)

#define GET_ENTRY_POINT(name)                                                  \
  do {                                                                         \
    ompt_##name = (ompt_##name##_t) lookup("ompt_"#name);                      \
    if (ompt_##name == NULL) {                                                 \
      std::cerr << "Could not find 'ompt_" #name "', exiting..." << std::endl; \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define TsanNewMemory(addr, size)                                              \
  AnnotateNewMemory(__FILE__, __LINE__, addr, size)

ompt_get_task_info_t ompt_get_task_info;
ompt_set_callback_t ompt_set_callback;
ompt_get_thread_data_t ompt_get_thread_data;
ompt_get_task_memory_t ompt_get_task_memory;

static std::atomic<int> task_id_counter(1);
static std::atomic<int> sync_id_counter(1);
static std::atomic<int> thread_id_counter(0);

void ompt_print_func(){
  printf("print in ompt \n");
}

void ompt_report_race_steps_func(unsigned int cur, unsigned int prev){
#ifdef GRAPH_MACRO
  setRace(cur);
  setRace(prev);
  std::pair<vertex_t, vertex_t> key = std::make_pair(prev, cur);
  
  std::lock_guard<std::mutex> lock(race_map_mutex);
  if(race_map->find(key) == race_map->end()){
    race_info ri = {prev, cur, 0, "", ""};
    race_map->emplace(key, ri);
  }
#endif
}

void ompt_report_race_stack_func(char* s, bool first, unsigned int current_step, unsigned int prev_step){
#ifdef GRAPH_MACRO
  #ifdef DEBUG_INFO
    printf("race stack: %s \n", s);
    printf("first? %d, current_step %d, previous_step %d \n", first, current_step, prev_step);
  #endif
  std::pair<vertex_t, vertex_t> key = std::make_pair(prev_step, current_step);
  race_info& ri = race_map->find(key)->second;

  if(!first){
    ri.prev_stack = s;
  }
  else{
    ri.current_stack = s;
  }
#endif
}

void ompt_report_current_racy_stack_func(char* s, unsigned int current_step, unsigned int prev_step, unsigned int lineNumber){
#ifdef GRAPH_MACRO
  std::pair<vertex_t, vertex_t> key = std::make_pair(prev_step, current_step);
  auto it = race_map->find(key);
  race_info ri = it->second;

  if(ri.current_stack == ""){
    race_info new_ri = {ri.prev_step, ri.current_step, ri.lca, ri.prev_stack, "hello world"};
    new_ri.current_stack = s;
    new_ri.current_stack += ":" + std::to_string(lineNumber) + ":";
    it->second = new_ri;
  }
#endif
}

static inline int insert_leaf(TreeNode *internal_node, task_t *task) {
  TreeNode *new_step = __tsan_insert_leaf(internal_node, task->current_taskwait->corresponding_id);
  int step_id = new_step->corresponding_id;
  task->current_step_id = step_id;
  __tsan_set_step_in_tls(step_id);
  return step_id;
}

static inline TreeNode *find_parent(task_t *task) {
  if (task->current_finish) {
    return task->current_finish->node_in_dpst.load(std::memory_order_acquire);
  } else {
    return task->node_in_dpst;
  }
}

static inline TreeNode *find_parent(finish_t *finish) {
  if (finish->parent) {
    return finish->parent->node_in_dpst.load(std::memory_order_acquire);
  } else {
    return finish->belonging_task->node_in_dpst;
  }
}

static void ompt_ta_parallel_begin
(
  ompt_data_t *encountering_task_data,
  const ompt_frame_t *encountering_task_frame,
  ompt_data_t *parallel_data,
  unsigned int requested_parallelism,
  int flags,
  const void *codeptr_ra
)
{
#ifdef DEBUG_INFO
  printf("[parallel_begin] parallelism: %d  ", requested_parallelism);
  if(flags & ompt_parallel_league){
    printf("this is teams \n");
  }
  
  if(flags & ompt_parallel_team){
    printf("this is parallel \n");
  }
#endif
  
  // insert a FINISH node because of the implicit barrier
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode *parent = find_parent(current_task);
  // 1. Update DPST
  TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
      sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
      parent, current_task->current_taskwait->corresponding_id);

  // 2. Set current task's current_finish to this finish
  finish_t *finish = new finish_t{
      new_finish_node, current_task->current_finish, current_task};
  current_task->current_finish = finish;
  
#ifdef GRAPH_MACRO
  vertex_t cg_parent = current_task->current_step_id;
  addEvent(cg_parent, event_type::parallel_begin);

  parallel_data->ptr = new parallel_t(requested_parallelism, current_task, cg_parent);
  int step_id = insert_leaf(new_finish_node, current_task);

  // FJ: insert a node in G as continuation node, and a continuation edge
  addVertex(step_id);
  if (current_task->IsOnTarget){
    setOnTarget(step_id);
  }

  addEdge(cg_parent, step_id, edge_type::CONT);

  #ifdef STACK_INFO
  if (codeptr_ra != nullptr)
  {
    unsigned int line;
    unsigned int col;
    const char* pc = (const char*) codeptr_ra - 1;

    char* file = __tsan_symbolize_pc( (void*) pc, line, col);
    std::string stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
    addStack(cg_parent, stack);

    printf("[parallel_begin] %s \n", stack.c_str());
  }
  #endif
#else
  parallel_data->ptr = new parallel_t(requested_parallelism, current_task);
  insert_leaf(new_finish_node, current_task);
#endif

  parallel_t* pr = (parallel_t*) parallel_data->ptr;

  if(flags & ompt_parallel_league){
    pr->isTeams = true;
  }

  finish->corresponding_pr = (void*) pr;
}


static void ompt_ta_parallel_end
(
  ompt_data_t *parallel_data,
  ompt_data_t *encountering_task_data,
  int flags,
  const void *codeptr_ra
)
{
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  finish_t* finish = current_task->current_finish;
  current_task->current_finish = finish->parent;
  TreeNode *parent = find_parent(finish);
  delete finish;
  parallel_t *parallel = (parallel_t *)parallel_data->ptr;

#ifdef GRAPH_MACRO
  vertex_t cg_parent = current_task->current_step_id;
  addEvent(cg_parent, event_type::parallel_end);

  int step_id = insert_leaf(parent, current_task);
  
  // 1. insert a node in G as continuation node, and a continuation edge
  addVertex(step_id);
  if(parallel->IsOnTarget){
    setOnTarget(step_id);
  }

  addEdge(cg_parent, step_id, edge_type::CONT);

  // 2. insert join edges from last step nodes in implicit tasks to the continuation node
  // printf("[parallel_end] current node is %u, joining size is %lu, ", cg_parent, parallel->end_nodes_to_join.size());

  for(int joining_node : parallel->end_nodes_to_join){
    if(joining_node == 0){
      continue;
    }
    
    addEdge(joining_node, step_id, edge_type::JOIN);
  }

  #ifdef STACK_INFO
  if (codeptr_ra != nullptr)
  {
    unsigned int line;
    unsigned int col;
    const char* pc = (const char*) codeptr_ra - 1;
    char* file = __tsan_symbolize_pc( (void*) pc, line, col);

    std::string stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
    addStack(cg_parent, stack);

    // printf("[parallel_end] %s \n", stack.c_str());
  }
  #endif
#else
  insert_leaf(parent, current_task);
  printf("[parallel_end] \n");
#endif

  delete parallel;
  parallel_data->ptr = nullptr;

}

static void ompt_ta_implicit_task(
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  unsigned int actual_parallelism,
  unsigned int index,
  int flags
)
{
  if (flags & ompt_task_initial) {
    if (endpoint == ompt_scope_begin) {
      // printf("[implicit_task] initial task begin \n");

      if(root){
        // This condition means this initial task is a task on target,
        // or the initial task for a team

        // 1. parent task
        int dd = 1;
        ompt_data_t *spt = nullptr;
        ompt_get_task_info(dd, NULL, &spt, NULL, NULL, NULL);
        if(spt == nullptr || spt->ptr == nullptr){
          printf("ERROR: spt is nullptr, cannot get parent for this initial task \n");
          return;
        }

        task_t* parent_task = (task_t*) spt->ptr;
        TreeNode* parent = find_parent(parent_task);
        TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
            task_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL, parent,
            parent_task->current_taskwait->corresponding_id);

        // 2. creat task_t with info
        finish_t *enclosed_finish = parent_task->current_finish;
        void* pr = enclosed_finish->corresponding_pr;
        task_t *ti = new task_t{new_task_node, enclosed_finish, true, index, pr, parent_task};
        ti->IsOnTarget = parent_task->IsOnTarget;
        task_data->ptr = ti;

        #ifdef GRAPH_MACRO
          int step_id = insert_leaf(new_task_node, ti);
          addVertex(step_id);
          if(ti->IsOnTarget){
            setOnTarget(step_id);
          }
          vertex_t cg_parent = parent_task->current_step_id;

          addEdge(cg_parent, step_id, edge_type::FORK_I);
        #else
          insert_leaf(new_task_node, ti);
        #endif

        // 3. an implicit parallel region the initial task has
        finish_t *implicit_parallel = new finish_t{new_task_node, nullptr,ti};
        ti->current_finish = implicit_parallel;

        return;
      }

      root = __tsan_init_DPST();
      TreeNode *initial = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
          root, kNullStepId);
      finish_t *implicit_parallel = new finish_t{initial, nullptr, nullptr};
      task_t* main_ti = new task_t{root, nullptr, true, index, nullptr, nullptr};
      main_ti->current_finish = implicit_parallel;
      implicit_parallel->belonging_task = main_ti;
      task_data->ptr = main_ti;
      
      parallel_t* pr;
      #ifdef GRAPH_MACRO
        // FJ: this should be the first node in G, with step_id = 1
        int step_id = insert_leaf(initial, main_ti);
        pr = new parallel_t(1, main_ti, step_id);

        addVertex(step_id);
      #else
        insert_leaf(initial, main_ti);
        pr = new parallel_t(1,main_ti);
      #endif

      implicit_parallel->corresponding_pr = (void*) pr;
    } 
    else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
      // printf("[implicit_task] initial task end, parallel region is %p, current step is %d \n", ti->parallel_region, ti->current_step_id);

      #ifdef GRAPH_MACRO
        if(ti->added_to_others_join == false && ti->parallel_region != nullptr){
          task_t *parent_task = ti->parent_task;

          if(parent_task != nullptr && parent_task->current_finish != nullptr){
              parallel_t *pr = (parallel_t*) ti->parallel_region;
              parallel_t *parent_current_pr = (parallel_t*) parent_task->current_finish->corresponding_pr;

              if(parent_current_pr->isTeams && parent_current_pr == pr){
                pr->end_nodes_to_join[ti->index] = ti->current_step_id;
              }
          }
        }
      #endif

      delete ti;
      __tsan_reset_step_in_tls();
    }
  } else if (flags & ompt_task_implicit) {
    if (endpoint == ompt_scope_begin) {
      assert(parallel_data->ptr != nullptr);
      parallel_t *parallel = (parallel_t *) parallel_data->ptr;
      task_t* parent_task = parallel->encounter_task;
      TreeNode *parent = find_parent(parent_task);
      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
          task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
          parent, parent_task->current_taskwait->corresponding_id);
      finish_t *enclosed_finish = parent_task->current_finish;
      task_t *ti = new task_t{new_task_node, enclosed_finish, true, index, (void*) parallel, parent_task};
      task_data->ptr = ti;
      
    #ifdef GRAPH_MACRO
      vertex_t step_id = insert_leaf(new_task_node, ti);
      addVertex(step_id);
      if(ti->IsOnTarget){
        setOnTarget(step_id);
      }
      vertex_t cg_parent = parallel->start_cg_node;

      EdgeFull edge = {edge_type::FORK_I, cg_parent, step_id};
      // Here we are using a concurrent vector savedEdges
      // because many implicit tasks for the same parallel region can be created simultanesouly
      savedEdges->push_back(edge);
    #else
      insert_leaf(new_task_node, ti);
    #endif
    } else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
      #ifdef DEBUG_INFO
        printf("[implicit_task] implicit task end, address is %p, current step is %d \n", ti, ti->current_step_id);
      #endif

      #ifdef GRAPH_MACRO
        if(ti->added_to_others_join == false){
          task_t *parent_task = ti->parent_task;

          if(parent_task != nullptr && parent_task->current_finish != nullptr){
              parallel_t *pr = (parallel_t*) ti->parallel_region;
              parallel_t *parent_pr = (parallel_t*) parent_task->parallel_region;
              parallel_t *parent_current_pr = (parallel_t*) parent_task->current_finish->corresponding_pr;

              if(pr != nullptr && parent_pr->isTeams && parent_current_pr == pr){
                pr->end_nodes_to_join[ti->index] = ti->current_step_id;
              }
          }
        }
      #endif

      delete ti;
      __tsan_reset_step_in_tls();
    }
  }
}


static void ompt_ta_sync_region(
  ompt_sync_region_t kind,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  if(kind == ompt_sync_region_taskgroup){
    assert(task_data->ptr != nullptr);
    task_t *current_task = (task_t *)task_data->ptr;
    if (endpoint == ompt_scope_begin) {
      TreeNode *parent = find_parent(current_task);
      TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), FINISH,
          parent, current_task->current_taskwait->corresponding_id);
      finish_t *new_finish =
          new finish_t{new_finish_node, current_task->current_finish, current_task};
      current_task->current_finish = new_finish;
      int current_step = current_task->current_step_id;
      int next_step = insert_leaf(new_finish_node, current_task);

      #ifdef GRAPH_MACRO
        parallel_t *pr = new parallel_t(0, current_task, current_step);
        new_finish->corresponding_pr = pr;
        pr->end_nodes_to_join.reserve(5);

        #ifdef STACK_INFO
        if (codeptr_ra != nullptr)
        {
          unsigned int line;
          unsigned int col;
          const char* pc = (const char*) codeptr_ra - 1;
          char* file = __tsan_symbolize_pc( (void*) pc, line, col);

          std::string stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
          addStack(current_step, stack);
        }
        #endif

        addVertex(next_step);
        addEvent(current_step, event_type::taskgroup_begin);
        addEdge(current_step, next_step, edge_type::CONT);
      #else
        parallel_t *pr = new parallel_t(0, current_task);
        new_finish->corresponding_pr = pr;
      #endif
    } else if (endpoint == ompt_scope_end) {
      finish_t *finish = current_task->current_finish;
      TreeNode *parent = find_parent(finish);
      current_task->current_finish = finish->parent;
      parallel_t *pr = (parallel_t*) finish->corresponding_pr;

      #ifdef GRAPH_MACRO
        vertex_t current_step = current_task->current_step_id;
        vertex_t next_step = insert_leaf(parent, current_task);

        addVertex(next_step);
        addEvent(current_step, event_type::taskgroup_end);
        addEdge(current_step, next_step, edge_type::CONT);

        for(vertex_t join_node : pr->end_nodes_to_join){
          addEdge(join_node, next_step, edge_type::JOIN);
        }
      #else
        insert_leaf(parent, current_task);
      #endif

      delete pr;
      delete finish;
    }
  } else if(kind == ompt_sync_region_taskwait){
    assert(task_data->ptr != nullptr);
    task_t *current_task = (task_t *)task_data->ptr;
    if (endpoint == ompt_scope_begin) {
      // insert a special node for taskwait, mark this as a taskwait step
      TreeNode *parent = find_parent(current_task);
      TreeNode *new_taskwait_node = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), TASKWAIT,
          parent, current_task->current_taskwait->corresponding_id);
      current_task->previous_taskwait = current_task->current_taskwait;
      current_task->current_taskwait = new_taskwait_node;
    } else if (endpoint == ompt_scope_end) {
      TreeNode *parent = current_task->current_taskwait->parent;
      int current_step = current_task->current_step_id;
      int new_step = insert_leaf(parent, current_task);

      #ifdef GRAPH_MACRO
        addEvent(current_step, event_type::taskwait_end);
        addVertex(new_step);
        addEdge(current_step, new_step, edge_type::CONT);

        TreeNode* left_node = current_task->previous_taskwait->corresponding_id != kNullStepId ? current_task->previous_taskwait : parent->children_list_head;
        while (left_node != nullptr && left_node->corresponding_id != new_step)
        {
          if(left_node->node_type == ASYNC_E || left_node->node_type == ASYNC_I){
            int end_node = left_node->children_list_tail->corresponding_id;
            addEdge(end_node, new_step, edge_type::JOIN);
          }
          left_node = left_node->next_sibling;
        }

        #ifdef STACK_INFO
        if (codeptr_ra != nullptr)
        {
          unsigned int line;
          unsigned int col;
          const char* pc = (const char*) codeptr_ra - 1;
          char* file = __tsan_symbolize_pc( (void*) pc, line, col);

          std::string stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
          addStack(current_step, stack);
        }
        #endif
      #endif
    }
  } else if (kind == ompt_sync_region_reduction) {
    task_t *current_task = (task_t *)task_data->ptr;
    // printf("[SYNC] task current step id is %d \n", current_task->current_step_id);
    return;
  } else {
    // For all kinds of barriers, split parallel region
    // Assume barriers are only used in implicit tasks
    if (endpoint == ompt_scope_begin) {
      task_t *current_task = (task_t *)task_data->ptr;
      TreeNode *current_task_node = current_task->node_in_dpst;
      // if (current_task_node->node_type != ASYNC_I) {
      //   printf("WARNING: barrier in explicit task\n");
      //   return;
      // }
      finish_t *enclosed_finish = current_task->enclosed_finish;
      TreeNode *finish_parent = find_parent(enclosed_finish);

      TreeNode *new_parallel_treeNode;
      parallel_t *parallel = (parallel_t *)parallel_data->ptr;
      int remaining_task = parallel->count_down_on_barrier();
      if (remaining_task == 1) {
        new_parallel_treeNode = __tsan_alloc_insert_internal_node(
            sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
            finish_parent, enclosed_finish->node_in_dpst.load(std::memory_order_acquire)->preceeding_taskwait);
        enclosed_finish->node_in_dpst.store(new_parallel_treeNode, std::memory_order_release);
        parallel->reset_remaining_task();
      } else {
        do {
          new_parallel_treeNode = enclosed_finish->node_in_dpst.load(std::memory_order_acquire);
        } while (new_parallel_treeNode == current_task_node->parent);
      }

      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
             task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
             new_parallel_treeNode, current_task_node->preceeding_taskwait);
      task_t* ti = new task_t{new_task_node, enclosed_finish, true, current_task->index, current_task->parallel_region, current_task};
      task_data->ptr = ti;
      
    #ifdef GRAPH_MACRO
      addEvent(current_task->current_step_id, event_type::sync_region_begin);
      #ifdef DEBUG_INFO
        printf("[sync_region_begin] current step id %d \n", current_task->current_step_id);
      #endif
      int step_id = insert_leaf(new_task_node, ti);

      if(current_task->added_to_others_join == false){
        unsigned int team_index = ti->index;
        if(team_index > parallel->parallelism){
          parallel->end_nodes_to_join.push_back(step_id);
        }
        else{
          parallel->end_nodes_to_join[team_index] = step_id;
        }
        current_task->added_to_others_join = true;
        ti->added_to_others_join = true;
      }

      // FJ: continuation edge and continuation node
      addVertex(step_id);
      addEvent(step_id, event_type::sync_region_end);
      if(ti->IsOnTarget){
        setOnTarget(step_id);
      }

      addEdge(current_task->current_step_id, step_id, edge_type::CONT);
    #else
      insert_leaf(new_task_node, ti);
    #endif

      delete current_task;
    }
    else if(endpoint == ompt_scope_end){
      if(parallel_data == nullptr){
        return;
      }

      #ifdef GRAPH_MACRO
        parallel_t* parallel = (parallel_t*) parallel_data->ptr;
        if(parallel == nullptr){
          return;
        }

        task_t *current_task = (task_t *)task_data->ptr;
        unsigned int team_index = current_task->index;
        int current_step = current_task->current_step_id;

        current_task->added_to_others_join = false;
        if(team_index != 0){
          return;
        }

        #ifdef DEBUG_INFO
          printf("[sync_region_end] joining size %lu \n", parallel->end_nodes_to_join.size()); 
        #endif
        for(vertex_t node : parallel->end_nodes_to_join){
          // if node == 0, we can directly break, because
          // we assume all threads hit the barrier or not
          if(node == 0){
            break;
          }

          #ifdef DEBUG_INFO
            printf("[sync_region_end] join edge from %u to %u \n", node, current_step);
          #endif
          if(node == current_step){
            continue;
          }

          addEdge(node, current_task->current_step_id, edge_type::BARRIER);
        }

        parallel->end_nodes_to_join.resize(parallel->parallelism);
      #endif
    }
  }
  
}


static void ompt_ta_task_create(ompt_data_t *encountering_task_data,
                                const ompt_frame_t *encountering_task_frame,
                                ompt_data_t *new_task_data, int flags,
                                int has_dependences, const void *codeptr_ra) 
{
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode* parent = find_parent(current_task);
  TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
      task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_E, parent,
      current_task->current_taskwait->corresponding_id);
  finish_t *enclosed_finish = current_task->current_finish
                                  ? current_task->current_finish
                                  : current_task->enclosed_finish;
                                  
  parallel_t* pr = (parallel_t*) enclosed_finish->corresponding_pr;
  unsigned int parallelism = pr->parallelism;
  task_t *ti = new task_t{new_task_node, enclosed_finish, true, parallelism+1, pr, current_task};
  new_task_data->ptr = ti;

#ifdef GRAPH_MACRO
  // 1. add a new node for new task and a fork edge
  vertex_t cg_parent = current_task->current_step_id;
  addEvent(cg_parent, event_type::task_create);

  int fork_node = insert_leaf(new_task_node,ti);

  addVertex(fork_node);
  if(ti->IsOnTarget){
    setOnTarget(fork_node);
  }

  addEdge(cg_parent, fork_node, edge_type::FORK_E);

  int continuation_node = insert_leaf(parent, current_task);

  // 2. add a continuation node and continuation edge, notice insert_leaf will set the current_step_id of current_task to be the continuation node
  addVertex(continuation_node);
  if(current_task->IsOnTarget){
    setOnTarget(continuation_node);
  }

  addEdge(cg_parent, continuation_node, edge_type::CONT);

  #ifdef STACK_INFO
  if (codeptr_ra != nullptr)
  {
    unsigned int line;
    unsigned int col;
    const char* pc = (const char*) codeptr_ra - 1;
    char* file = __tsan_symbolize_pc( (void*) pc, line, col);

    std::string stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
    addStack(cg_parent, stack);

    #ifdef DEBUG_INFO
      printf("[task_create] %s \n", stack.c_str());
    #endif
  }
  #endif
#else
  insert_leaf(new_task_node,ti);
  insert_leaf(parent, current_task);
#endif
}


static void ompt_ta_dependences(ompt_data_t *task_data,
                                const ompt_dependence_t *deps,
                                int ndeps)
{
#ifdef GRAPH_MACRO
  if (ndeps <= 0){
    return;
  }

  task_t *current_task = (task_t*) task_data->ptr;
  if (!current_task->parent_task) {
    return;
  }

  if (!current_task->parent_task->DependencyMap)
    current_task->parent_task->DependencyMap = new std::unordered_map<void *, DependencyData *>();

  current_task->depending_task_nodes = std::vector<TreeNode*>();
  current_task->depending_task_nodes.reserve(ndeps);
  // Data->Dependencies = (TaskDependency *) malloc(sizeof(TaskDependency) * ndeps);
  current_task->DependencyCount += ndeps;

  for (int i = 0; i < ndeps; i++) {
    auto ret = current_task->parent_task->DependencyMap->insert(std::make_pair(deps[i].variable.ptr, nullptr));

    if (ret.second == true) {
      // this means no previous pair exists and we successfully insert a new pair into parent_task->DependencyMap
      ret.first->second = DependencyData::New();
      ret.first->second->in.reserve(5);
    }

    DependencyData* depend_data = ret.first->second;
    AcquireAndReleaseDependencies(current_task, depend_data, deps[i].dependence_type);

    // new ((void *)(Data->Dependencies + i)) TaskDependency(ret.first->second, deps[i].dependence_type);
  }

#endif
}


static void ompt_ta_task_schedule(
  ompt_data_t *prior_task_data,
  ompt_task_status_t prior_task_status,
  ompt_data_t *next_task_data
){
  #ifndef DPST_MACRO
  TsanNewMemory(static_cast<char *>(__builtin_frame_address(0)) - kDefaultStackSize, kDefaultStackSize);
  #endif

  if (prior_task_status == ompt_task_complete || prior_task_status == ompt_task_late_fulfill) {

    if (ompt_get_task_memory) {
      void *addr;
      size_t size;
      int ret_task_memory = 1, block = 0;
      while (ret_task_memory) {
        ret_task_memory = ompt_get_task_memory(&addr, &size, block);

        #ifndef DPST_MACRO
        if (size > 0) {
          TsanNewMemory(((void**)addr), size+8);
        }
        #endif

        block++;
      }
    }
    
    if (prior_task_status == ompt_task_complete && prior_task_data && prior_task_data->ptr) {
      task_t *prior_ti = (task_t *)prior_task_data->ptr;

    #ifdef GRAPH_MACRO
      if(prior_ti->added_to_others_join == false){
        parallel_t* previous_task_parallel_region = (parallel_t*) prior_ti->parallel_region;
        int team_index = prior_ti->index;
        vertex_t prior_ti_end_node = prior_ti->node_in_dpst->children_list_tail->corresponding_id;

        if(team_index > previous_task_parallel_region->parallelism){
          std::lock_guard<std::mutex> lock(previous_task_parallel_region->pr_mutex);
          previous_task_parallel_region->end_nodes_to_join.push_back(prior_ti_end_node);
        }
        else{
          previous_task_parallel_region->end_nodes_to_join[team_index] = prior_ti_end_node;
        }

        addEvent(prior_ti_end_node, event_type::task_schedule);
        
        #ifdef DEBUG_INFO
          printf("[task_schedule] previous task end node %u @@@ ", prior_ti_end_node);
        #endif
      }
    #endif
      delete prior_ti;
    }
  }
  
  if(next_task_data == nullptr){
    return;
  }

  assert(next_task_data->ptr);
  task_t* next_task = (task_t*) next_task_data->ptr;
  #ifdef DEBUG_INFO
    printf("[task_schedule] next task starting node %d \n", next_task->current_step_id);
  #endif

  __tsan_set_step_in_tls(next_task->current_step_id);
  if (next_task->execution == 0 && next_task->DependencyCount > 0) {
    unsigned int current_step = next_task->current_step_id;
    drawDependEdges(next_task, current_step);
  }
  next_task->execution++;

}


static void ompt_ta_thread_begin(ompt_thread_t thread_type, ompt_data_t *thread_data) {
  thread_data->value = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
  DependencyDataPool::ThreadDataPool = new DependencyDataPool;
}

static void ompt_ta_thread_end(ompt_data_t *thread_data){
  delete DependencyDataPool::ThreadDataPool;
}


static void ompt_ta_target(ompt_target_t kind, ompt_scope_endpoint_t endpoint,
                             int device_num, ompt_data_t *task_data,
                             ompt_id_t target_id, const void *codeptr_ra) 
{
  if(kind != ompt_target){
    return;
  }

  std::string stack;
  #ifdef STACK_INFO
  if (codeptr_ra != nullptr)
  {
    unsigned int line;
    unsigned int col;
    const char* pc = (const char*) codeptr_ra - 1;
    char* file = __tsan_symbolize_pc( (void*) pc, line, col);

    stack = "file: " + std::string(file) + ", line: " + std::to_string(line) + ", col: " + std::to_string(col);
  }
  #endif

  printf("[target] ");

  task_t* current_task = (task_t*) task_data->ptr;

  if (endpoint == ompt_scope_begin)
  {
    printf("scope begin \n");
    current_task->IsOnTarget = true;

    #ifdef GRAPH_MACRO
      int cg_parent = current_task->current_step_id;
      addEvent(cg_parent, event_type::target_begin);
      addStack(cg_parent, stack);

      TreeNode *parent = find_parent(current_task);
      int step_id = insert_leaf(parent, current_task);
      addVertex(step_id);
      setOnTarget(step_id);

      addEdge(cg_parent,step_id,edge_type::TARGET);

      targetRegion tr = targetRegion();
      tr.begin_node = cg_parent;
      
      if(temp_dmv->size() > 0){
        for(auto dm : *temp_dmv){
          tr.dmv.push_back(dm);
        }
        temp_dmv->clear();
      }

      trm->push_back(tr);
      target_begin_node = cg_parent;
    #endif
  }
  else if(endpoint == ompt_scope_end){
    printf("scope end \n");
    current_task->IsOnTarget = false;

    #ifdef GRAPH_MACRO
      int cg_parent = current_task->current_step_id;
      addEvent(cg_parent, event_type::target_end);
      addStack(cg_parent, stack);

      TreeNode *parent = find_parent(current_task);
      int step_id = insert_leaf(parent, current_task);
      addVertex(step_id);
      unsetOnTarget(step_id);

      addEdge(cg_parent,step_id,edge_type::CONT);

      targetRegion &tr = trm->back();
      tr.end_node = cg_parent;

      target_begin_node = 0;
      target_end_node = cg_parent;
    #endif
  }
  else{
    printf("\n");
  }
}


static void ompt_ta_device_mem(ompt_data_t *target_task_data,
                                ompt_data_t *target_data,
                                unsigned int device_mem_flag,
                                void *orig_base_addr, void *orig_addr,
                                int orig_device_num, void *dest_addr,
                                int dest_device_num, size_t bytes,
                                const void *codeptr_ra, char *var_name)
{
  printf("[device_mem] var name %s, original addr %p, destination address %p, size %lu, ", (var_name ? var_name : "unknown")
         , orig_addr, dest_addr, bytes);

  #ifdef GRAPH_MACRO
    DataMove dm(orig_addr, dest_addr, bytes, device_mem_flag);
    if(trm->size() == 0){
      temp_dmv->push_back(dm);
    }
    else{
      trm->back().dmv.push_back(dm);
    }
  #endif

  std::string kind = "";
  if (device_mem_flag & ompt_device_mem_flag_to){
    kind = kind + "to | ";
  }
  if (device_mem_flag & ompt_device_mem_flag_from){
    kind = kind + "from | ";
  }
  if (device_mem_flag & ompt_device_mem_flag_alloc){
    kind = kind + "alloc | ";
  }
  if (device_mem_flag & ompt_device_mem_flag_release){
    kind = kind + "release | ";
  }
  if (device_mem_flag & ompt_device_mem_flag_associate){
    kind = kind + "associate | ";
  }
  if (device_mem_flag & ompt_device_mem_flag_disassociate){
    kind = kind + "disassociate | ";
  }

  std::cout << kind << std::endl;
  
} 

static int ompt_tsan_initialize(ompt_function_lookup_t lookup, int device_num,
                                ompt_data_t *tool_data) {
  #ifndef DPST_MACRO
  // __tsan_print();
  // __tsan_set_ompt_print_function(&ompt_print_func);
  __tsan_set_report_race_steps_function(&ompt_report_race_steps_func);
  __tsan_set_report_race_stack_function(&ompt_report_race_stack_func);
  __tsan_set_report_current_racy_stack_function(&ompt_report_current_racy_stack_func);
  #endif

  GET_ENTRY_POINT(set_callback);
  GET_ENTRY_POINT(get_task_info);
  GET_ENTRY_POINT(get_thread_data);
  GET_ENTRY_POINT(get_task_memory);

  SET_CALLBACK(task_create);
  SET_CALLBACK(parallel_begin);
  SET_CALLBACK(implicit_task);
  SET_CALLBACK(sync_region);
  SET_CALLBACK(parallel_end);
  SET_CALLBACK(task_schedule);
  SET_CALLBACK(thread_begin);
  SET_CALLBACK(thread_end);
  SET_CALLBACK(dependences);

  SET_CALLBACK(target);
  SET_CALLBACK(device_mem);

  #ifdef GRAPH_MACRO
    savedEdges = new ConcurrentVector<EdgeFull>();
    savedEdges->reserve(10000000);

    savedVertex = new std::vector<Vertex_new>(vertex_size);

    trm = new std::vector<targetRegion>();
    trm->reserve(5);

    temp_dmv = new std::vector<DataMove>();
    temp_dmv->reserve(5);

    race_map = new std::map<std::pair<vertex_t, vertex_t>, race_info>();
  #endif
  return 1; // success
}

static void ompt_tsan_finalize(ompt_data_t *tool_data) {
  //__tsan_print_DPST_info(true);

  #ifdef GRAPH_MACRO
    print_graph_json();

    // unsigned int vertex_count = 0;
    // unsigned int edge_count = 0;
    // unsigned int fork_edge_count = 0;
    // unsigned int join_edge_count = 0;

    // for(int i=0; i < savedEdges->size(); i++){
    //   edge_count++;
    //   fork_edge_count ++;
    // }

    // for (int i = 1; i < savedVertex->size(); i++)
    // {
    //   Vertex_new v = (*savedVertex)[i];
    //   if (v.id == 0)
    //     break;

    //   vertex_count++;

    //   edge_count += v.index;

    //   for(int i=0; i < v.index; i++){
    //     if(v.out_edges[i].type == edge_type::JOIN || v.out_edges[i].type == edge_type::BARRIER || v.out_edges[i].type == edge_type::JOIN_E){
    //       join_edge_count++;
    //     }
    //     else if(v.out_edges[i].type == edge_type::FORK_E || v.out_edges[i].type == edge_type::FORK_I){
    //       fork_edge_count++;
    //     }
    //   }
    // }

    // printf("number of nodes: %u \n", vertex_count);
    // printf("number of edges: %u \n", edge_count);
    // printf("number of synchronized edges: %lu \n", savedEdges->size());
    // printf("number of fork edges: %u \n", fork_edge_count);
    // printf("number of join edges: %u \n", join_edge_count);


    delete savedEdges;
    delete savedVertex;

    delete trm;
    delete temp_dmv;
    delete race_map;
  #endif
}

static bool scan_tsan_runtime() {
  void *func_ptr = dlsym(RTLD_DEFAULT, "__tsan_read1");
  return func_ptr != nullptr;
}

extern "C" ompt_start_tool_result_t *
ompt_start_tool(unsigned int omp_version, const char *runtime_version) {
  static ompt_start_tool_result_t ompt_start_tool_result = {
      &ompt_tsan_initialize, &ompt_tsan_finalize, {0}};
  // if (!scan_tsan_runtime()) {
  //   std::cout << "Taskracer does not detect TSAN runtime, stopping operation" << std::endl;
  //   return nullptr;
  // }
  std::cout << "Taskracer detects TSAN runtime, carrying out race detection using DPST" << std::endl;

#ifdef GRAPH_MACRO
  std::cout << "Computation Graph recording enabled" << std::endl;
  pagesize = getpagesize();
#endif

  // return nullptr;
  return &ompt_start_tool_result;
}


void print_graph_json(){
#ifdef GRAPH_MACRO
  const char* env_p = std::getenv("PRINT_GRAPHML");
  if(!env_p || (strcmp(env_p, "TRUE") != 0)){
    return;
  }

  for(int i=0; i < savedEdges->size(); i++){
    EdgeFull edge = (*savedEdges)[i];
    addEdge(edge.source, edge.target, edge.type);
  }

  json j;
  json nodes = {};
  json edges = {};
  json files = {};
  std::set<std::string> fileSet;

  // 1. export nodes and edges into JSON
  for (int i = 1; i < savedVertex->size(); i++)
  {
    Vertex_new v = (*savedVertex)[i];
    if (v.id == 0)
      break;

    if(v.stack != ""){
      unsigned int lineNumber = 0;
      std::string filePath = extractInfo(v.stack, lineNumber, v.has_race);
      
      if(fileSet.find(filePath) == fileSet.end()){
        fileSet.insert(filePath);
        std::string fileString = get_file_string(filePath);
        files[filePath] = fileString;
      }
    }

    json node;
    node["id"] = v.id;
    node["end_event"] = v.end_event;
    node["has_race"] = v.has_race;
    node["ontarget"] = v.ontarget;
    node["stack"] = v.stack;
    node["active"] = 1;
    node["hidden"] = 0;
    nodes.push_back(node);

    vertex_t source = v.id;
    for (int j = 0; j < v.index; j++)
    {
      Edge_new e = v.out_edges[j];
      json edge;
      edge["source"] = source;
      edge["target"] = e.target;
      edge["edge_type"] = e.type;
      edge["hidden"] = 0;
      edges.push_back(edge);
    }
  }

  j["nodes"] = nodes;
  j["edges"] = edges;
  j["files"] = files;

  // 2. export races into JSON
  json races;
  for(auto it = race_map->begin(); it != race_map->end(); ++it){
    race_info ri = it->second;
    json race;
    race["prev"] = ri.prev_step;
    race["current"] = ri.current_step;
    race["lca"] = ri.lca;
    race["prev_stack"] = ri.prev_stack;
    race["current_stack"] = ri.current_stack;
    races.push_back(race);
  }

  // 3. export data moves into JSON
  json targets = get_datamove_json();

  j["races"] = races;
  j["targets"] = targets;

  // 4. write JSON to file
  std::ofstream outputFile("data/rawgraphml.json");
  if (outputFile.is_open()) {
    outputFile << std::setw(4) << j;
    outputFile.close();
    std::cout << "JSON data written to 'data/rawgraphml.json'" << std::endl;
  } 
  else 
  {
    std::cerr << "Unable to open file for writing" << std::endl;
  }
#endif
}


inline bool exists_test3 (const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

std::string get_file_string(std::string filePath){
  if(!exists_test3(filePath)){
    return "";
  }

  std::ifstream file(filePath);
  std::string str;
  std::string file_contents;
  while (std::getline(file, str))
  {
    file_contents += str;
    file_contents += "\n";
  }

  return file_contents;
}

#ifdef GRAPH_MACRO
json get_datamove_json(){
  json target;
  for (auto it = trm->begin(); it != trm->end(); ++it) {
    targetRegion &tr = *it;
    std::vector<DataMove> dmv = tr.dmv;
    json t;    
    json datamove;

    for(DataMove dm : dmv){
      std::stringstream ss;
      ss << std::hex << dm.orig_addr;
      std::string orig_address = ss.str();

      ss.str("");
      ss.clear();

      ss << std::hex << dm.dest_addr;
      std::string dest_address = ss.str();

      datamove.push_back({
        {"orig_address", orig_address},
        {"dest_address", dest_address},
        {"bytes", dm.bytes},
        {"flag", dm.device_mem_flag}
      });
    }

    t["begin_node"] = tr.begin_node;
    t["end_node"] = tr.end_node;
    t["datamove"] = datamove;
    target.push_back(t);
  }
  return target;
}
#endif


std::string extractInfo(const std::string& input, unsigned int& lineNumber, bool has_race) {
  // Define the regular expression pattern
  std::regex pattern(R"(/(.+):(\d+):\d+ \(.*\))");

  // Match the input against the pattern
  std::smatch match;
  std::string result = "Unknown";
  lineNumber = 0;

  // if(has_race){
  //   if (std::regex_search(input, match, pattern)) {
  //       // Extract the file path and line number from the matched groups
  //       result = "/" + match[1].str();
  //       lineNumber = std::stoi(match[2].str());
  //   }
  // }
  // else{
  //   // in this case, the stack starts with "file: "
  //   std::size_t first_comma_index = input.find(",");
  //   std::string starter = "file: ";
  //   result = input.substr(starter.length(), first_comma_index - starter.length());
  // }

  std::size_t first_comma_index = input.find(",");
  std::string starter = "file: ";
  result = input.substr(starter.length(), first_comma_index - starter.length());

  return result;
}