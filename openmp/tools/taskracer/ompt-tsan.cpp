#include <iostream>
#include <cstdint>
#include <cassert>
#include "omp-tools.h"
#include "dlfcn.h"

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graphml.hpp>
using namespace boost;

void print_graph();

struct Vertex {
  unsigned int id;
};

struct Edge{
  std::string type;
};

const std::string contedge = "CONT";
const std::string forkedge = "FORK";
const std::string joinedge = "JOIN";

//Define the graph using those classes
typedef adjacency_list<listS, vecS, directedS, Vertex, Edge > Graph;
//Some typedefs for simplicity
typedef graph_traits<Graph>::vertex_descriptor vertex_t; // vertex_descriptor, in this case, is an index for the listS used to save all vertex. 
typedef graph_traits<Graph>::edge_descriptor edge_t;

//Instanciate a graph
Graph g(500);
dynamic_properties dp;

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

class task_t;
class finish_t {
public:
  std::atomic<TreeNode *> node_in_dpst;
  finish_t *parent;
  task_t *belonging_task;
public:
  finish_t(TreeNode *node, finish_t *parent, task_t *task)
      : node_in_dpst(node), parent(parent), belonging_task(task) {}
};

class task_t {
public:
  TreeNode *node_in_dpst;
  finish_t *current_finish;
  finish_t *enclosed_finish;
  TreeNode *current_taskwait;
  int current_step_id;
  bool initialized;
  unsigned int index;
public:
  task_t(TreeNode *node, finish_t *enclosed_finish, bool initialized, unsigned int index)
      : node_in_dpst(node), current_finish(nullptr),
        enclosed_finish(enclosed_finish), current_taskwait(&kNullTaskWait),
        current_step_id(kNullStepId), initialized(initialized), index(index) {}
};

class parallel_t {
public:
  int parallelism;
  std::atomic<int> remaining_task;
  task_t *encounter_task;
  vertex_t start_cg_node;
  std::vector<vertex_t> end_nodes_to_join;
public:
  parallel_t(int parallelism, task_t *task)
      : parallelism(parallelism), remaining_task(parallelism),
        encounter_task(task), start_cg_node(-1) {
        }

  parallel_t(int parallelism, task_t *task, vertex_t node)
    : parallelism(parallelism), remaining_task(parallelism),
      encounter_task(task), start_cg_node(node) {
        this->end_nodes_to_join = std::vector<vertex_t>(parallelism, 0);
      }

  int count_down_on_barrier() {
    int val = remaining_task.fetch_sub(1, std::memory_order_acq_rel);
    return val;
  }
  void reset_remaining_task() {
    remaining_task.store(parallelism, std::memory_order_release);
  }
};

extern "C" {
void __attribute__((weak)) __tsan_print();

TreeNode __attribute__((weak)) *__tsan_init_DPST();

// TreeNode *__attribute__((weak))
// __tsan_alloc_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

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

void boost_test();
static std::atomic<int> task_id_counter(1);
static std::atomic<int> sync_id_counter(1);
static std::atomic<int> thread_id_counter(0);

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
  printf("[PARALLEL_BEGIN] \n");
  // insert a FINISH node because of the implicit barrier
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode *parent = find_parent(current_task);
  // 1. Update DPST
  TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
      sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
      parent, current_task->current_taskwait->corresponding_id);

  vertex_t cg_parent = current_task->current_step_id;

  // 2. Set current task's current_finish to this finish
  finish_t *finish = new finish_t{
      new_finish_node, current_task->current_finish, current_task};
  current_task->current_finish = finish;
  
  parallel_data->ptr = new parallel_t(requested_parallelism, current_task, cg_parent);
  int step_id = insert_leaf(new_finish_node, current_task);

  // DONE: 
  // insert a node in G as continuation node, and a continuation edge
  g[step_id].id = step_id;

  edge_t e; bool b;
  boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
  g[e].type = contedge;
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

  vertex_t cg_parent = current_task->current_step_id;

  int step_id = insert_leaf(parent, current_task);
  
  // DONE: 
  // 1. insert a node in G as continuation node, and a continuation edge
  g[step_id].id = step_id;

  edge_t e; bool b;
  boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
  g[e].type = contedge;
  // printf("[PARALLEL_END] new step id is %d, current step id %d \n", step_id, cg_parent);

  // 2. insert join edges from last step nodes in implicit tasks to the continuation node
  for(int joining_node : parallel->end_nodes_to_join){
    // printf("vertex_t to join %d \n", joining_node);
    edge_t ee; bool bb;
    boost::tie(ee,bb) = boost::add_edge(joining_node,step_id,g);
    g[ee].type = joinedge;
  }

  print_graph();

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
      printf("OMPT! initial task begins, should only appear once !! \n");
      if (!root) {
        root = __tsan_init_DPST();
      }
      TreeNode *initial = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
          root, kNullStepId);
      finish_t *implicit_parallel = new finish_t{initial, nullptr, nullptr};
      task_t* main_ti = new task_t{root, nullptr, true, index};
      main_ti->current_finish = implicit_parallel;
      implicit_parallel->belonging_task = main_ti;
      task_data->ptr = main_ti;
      int step_id = insert_leaf(initial, main_ti);

      // DONE: this should be the first node in G, with step_id = 1
      g[step_id].id = step_id;
    } else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
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
      task_t *ti = new task_t{new_task_node, enclosed_finish, true, index};
      task_data->ptr = ti;
      int step_id = insert_leaf(new_task_node, ti);
      // printf("implicit task first node step_id is %d \n", step_id);

      // DONE: insert first step node for the implicit task
      // Question: how to get parent node? We have parent task, but the current step node
      // for parent task is already the continuation node.
      //                 A
      //               / |
      //              /  |
      //             /   |
      //            /    |
      //           B     C <- (current step node for parent task)
      vertex_t cg_parent = parallel->start_cg_node;
      // vertex_t node = boost::add_vertex(g);

      g[step_id].id = step_id;

      edge_t e; bool b;
      boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
      g[e].type = forkedge;

    } else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
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
      insert_leaf(new_finish_node, current_task);

      // TODO: taskgroup step node in computation graph
    } else if (endpoint == ompt_scope_end) {
      finish_t *finish = current_task->current_finish;
      TreeNode *parent = find_parent(finish);
      current_task->current_finish = finish->parent;
      delete finish;
      insert_leaf(parent, current_task);

      // TODO: taskgroup end 
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
      current_task->current_taskwait = new_taskwait_node;
    } else if (endpoint == ompt_scope_end) {
      TreeNode *parent = current_task->current_taskwait->parent;
      insert_leaf(parent, current_task);

      // TODO: taskwait
    }
  } else if (kind == ompt_sync_region_reduction) {
    return;
  } else {
    // For all kinds of barriers, split parallel region
    // Assume barriers are only used in implicit tasks
    if (endpoint == ompt_scope_begin) {
      task_t *current_task = (task_t *)task_data->ptr;
      TreeNode *current_task_node = current_task->node_in_dpst;
      if (current_task_node->node_type != ASYNC_I) {
        printf("WARNING: barrier in explicit task\n");
        return;
      }
      finish_t *enclosed_finish = current_task->enclosed_finish;
      TreeNode *finish_parent = find_parent(enclosed_finish);

      TreeNode *new_parallel;
      parallel_t *parallel = (parallel_t *)parallel_data->ptr;
      int remaining_task = parallel->count_down_on_barrier();
      if (remaining_task == 1) {
        new_parallel = __tsan_alloc_insert_internal_node(
            sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
            finish_parent, enclosed_finish->node_in_dpst.load(std::memory_order_acquire)->preceeding_taskwait);
        enclosed_finish->node_in_dpst.store(new_parallel, std::memory_order_release);
        parallel->reset_remaining_task();
      } else {
        do {
          new_parallel = enclosed_finish->node_in_dpst.load(std::memory_order_acquire);
        } while (new_parallel == current_task_node->parent);
      }

      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
             task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
             new_parallel, current_task_node->preceeding_taskwait);
      task_t* ti = new task_t{new_task_node, enclosed_finish, true, current_task->index};
      task_data->ptr = ti;
      // DONE: save the new step node in parallel_t so the parallel_t knows who to join
      int step_id = insert_leaf(new_task_node, ti);
      unsigned int team_index = ti->index;
      parallel->end_nodes_to_join[team_index] = step_id;

      // DONE: continuation edge and continuation node
      g[step_id].id = step_id;
      edge_t e;
      bool b;
      boost::tie(e,b) = boost::add_edge(current_task->current_step_id, step_id, g);
      g[e].type = contedge;

      // printf("[SYNC:scope_begin] new step id is %d, current step id is %d \n", step_id, current_task->current_step_id);

      delete current_task;
    }
  }
  
}


static void ompt_ta_task_create(ompt_data_t *encountering_task_data,
                                const ompt_frame_t *encountering_task_frame,
                                ompt_data_t *new_task_data, int flags,
                                int has_dependences, const void *codeptr_ra) 
{
  printf("[TASK_CREATE] \n");
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode* parent = find_parent(current_task);
  TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
      task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_E, parent,
      current_task->current_taskwait->corresponding_id);
  finish_t *enclosed_finish = current_task->current_finish
                                  ? current_task->current_finish
                                  : current_task->enclosed_finish;
  task_t *ti = new task_t{new_task_node, enclosed_finish, false, 0};
  new_task_data->ptr = ti;
  insert_leaf(parent, current_task);

  // TODO: task create, same as traditional fork 
}



static void ompt_ta_task_schedule(
  ompt_data_t *prior_task_data,
  ompt_task_status_t prior_task_status,
  ompt_data_t *next_task_data
){
  // printf("OMPT! task_schedule, put task node in current thread \n");
  // printf("task %d, %p scheduled\n", next_task_node->corresponding_task_id, next_task_node);
  // printf("thread %lu, task %lu\n", ompt_get_thread_data()->value, (uintptr_t)next_task_node & 0xFFFFUL);
  // char *stack = static_cast<char *>(__builtin_frame_address(0));
  // TreeNode *prior_task_node = ((task_t *)prior_task_data->ptr)->node_in_dpst;
  //printf("task %lu, stack range [%p, %p]\n", (uintptr_t)next_task_node & 0xFFFFUL, stack - kDefaultStackSize, stack);
  // printf("tid = %lu, prior task %lu, next task %lu, %u\n",
  //        ompt_get_thread_data()->value, (uintptr_t)prior_task_node & 0xFFFFUL,
  //        (uintptr_t)next_task_node & 0xFFFFUL, prior_task_status);
  TsanNewMemory(static_cast<char *>(__builtin_frame_address(0)) -
                    kDefaultStackSize,
                kDefaultStackSize);
  if (prior_task_status == ompt_task_complete ||
      prior_task_status == ompt_task_late_fulfill) {
    if (ompt_get_task_memory) {
      void *addr;
      size_t size;
      int ret_task_memory = 1, block = 0;
      while (ret_task_memory) {
        ret_task_memory = ompt_get_task_memory(&addr, &size, block);
        if (size > 0) {
          TsanNewMemory(((void**)addr), size+8);
        }
        block++;
      }
    }
    if (prior_task_status == ompt_task_complete && prior_task_data) {
      task_t *prior_ti = (task_t *)prior_task_data->ptr;
      delete prior_ti;
    }
  }
  
  assert(next_task_data->ptr);
  task_t* next_task = (task_t*) next_task_data->ptr;
  TreeNode* next_task_node = next_task->node_in_dpst;
  if (!next_task->initialized) {
    insert_leaf(next_task_node, next_task);
    next_task->initialized = true;

    // TODO: task schedule, update the computation graph if the new task is initizalied here
  } else {
    __tsan_set_step_in_tls(next_task->current_step_id);
  }
}

static void ompt_ta_thread_begin(ompt_thread_t thread_type, ompt_data_t *thread_data) {
  thread_data->value = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
}

static int ompt_tsan_initialize(ompt_function_lookup_t lookup, int device_num,
                                ompt_data_t *tool_data) {
  // __tsan_print();
  // ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  // if (ompt_set_callback == NULL) {
  //   std::cerr << "Could not set callback, exiting..." << std::endl;
  //   std::exit(1);
  // }

  // ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  // if(ompt_get_task_info == NULL){
  //   std::cerr << "Could not get task info, exiting..." << std::endl;
  //   std::exit(1);
  // }
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

  return 1; // success
}

static void ompt_tsan_finalize(ompt_data_t *tool_data) {
  //__tsan_print_DPST_info(true);
}

static bool scan_tsan_runtime() {
  void *func_ptr = dlsym(RTLD_DEFAULT, "__tsan_read1");
  return func_ptr != nullptr;
}

extern "C" ompt_start_tool_result_t *
ompt_start_tool(unsigned int omp_version, const char *runtime_version) {
  static ompt_start_tool_result_t ompt_start_tool_result = {
      &ompt_tsan_initialize, &ompt_tsan_finalize, {0}};
  if (!scan_tsan_runtime()) {
    std::cout << "Taskracer does not detect TSAN runtime, stopping operation" << std::endl;
    return nullptr;
  }
  std::cout << "Taskracer detects TSAN runtime, carrying out race detection using DPST" << std::endl;

  std::cout << "Computation Graph recording enabled" << std::endl;

  auto vertex_id_map = boost::get(&Vertex::id, g);
  auto edge_type_map = boost::get(&Edge::type, g);
  dp.property("vertex_id", vertex_id_map);
  dp.property("edge_type", edge_type_map);

  // Create two vertices in that graph
  // vertex_t u = boost::add_vertex(g);
  // vertex_t v = boost::add_vertex(g);

  // // Create an edge conecting those two vertices
  // edge_t e; bool b;
  // boost::tie(e,b) = boost::add_edge(u,v,g);

  // // Set the properties of a vertex and the edge
  // g[u].id = 666;
  // g[v].id = 888;
  // g[e].info = "Hello world";
  // g[e].type = "cont";

  // // how to use vertex_t and edge_t
  // std::cout << "u is " << u << std::endl;
  // std::cout << "v is " << v << std::endl;
  // std::cout << "e source: " << e.m_source << ", vertex id is: " << g[e.m_source].id << std::endl;
  // std::cout << "e target: " << e.m_target << ", vertex id is: " << g[e.m_target].id << std::endl;

  // property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);
  // auto vertex_id_map = boost::get(&Vertex::id, g);
  // auto edge_info_map = boost::get(&Edge::info, g);
  // auto edge_type_map = boost::get(&Edge::type, g);

  // dynamic_properties dp;
  // dp.property("vertex_id", vertex_id_map);
  // dp.property("edge_info", edge_info_map);
  // dp.property("edge_type", edge_type_map);

  // write_graphml(std::cout, g, dp, true);
  // boost_test();

  return &ompt_start_tool_result;
}

void print_graph(){
  typedef property_map<Graph, vertex_index_t>::type IndexMap;
  IndexMap index = get(vertex_index, g);

  printf("vertices(g) = ");

  typedef graph_traits<Graph>::vertex_iterator vertex_iter;
  std::pair<vertex_iter, vertex_iter> vp;
  for (vp = vertices(g); vp.first != vp.second; ++vp.first){
    if(g[index[*vp.first]].id != 0){
      std::cout <<  index[*vp.first] <<  " ";
    }
  }

  printf("\n");

  printf("edges(g) = ");

  graph_traits<Graph>::edge_iterator ei, ei_end;
  for (tie(ei, ei_end) = edges(g); ei != ei_end; ++ei){
      std::cout << "(" 
                << index[source(*ei, g)] 
                << "->" << index[target(*ei, g)]
                << ","  << g[*ei].type  
                << ") ";
      
  }

  printf("\n");
}

void boost_test(){
  // Initialize boost graph
  // create a typedef for the Graph type
  typedef adjacency_list<vecS, vecS, bidirectionalS> Graph;

  // Make convenient labels for the vertices
  enum { A, B, C, D, E, N };
  const int num_vertices = N;
  const char* name = "ABCDE";

  // writing out the edges in the graph
  typedef std::pair<int, int> Edge;
  Edge edge_array[] = 
  { Edge(A,B), Edge(A,D), Edge(C,A), Edge(D,C),
    Edge(C,E), Edge(B,D), Edge(D,E) };
  const int num_edges = sizeof(edge_array)/sizeof(edge_array[0]);

  // declare a graph object
  Graph g(num_vertices);

  // add the edges to the graph object
  for (int i = 0; i < num_edges; ++i){
    add_edge(edge_array[i].first, edge_array[i].second, g);
  }

  // get the property map for vertex indices
  typedef property_map<Graph, vertex_index_t>::type IndexMap;
  IndexMap index = get(vertex_index, g);

  printf("vertices(g) = ");

  typedef graph_traits<Graph>::vertex_iterator vertex_iter;
  std::pair<vertex_iter, vertex_iter> vp;
  for (vp = vertices(g); vp.first != vp.second; ++vp.first){
    std::cout << index[*vp.first] <<  " ";
  }
    
  printf("\n");

  // get the edge list
  printf("edges(g) = ");

  graph_traits<Graph>::edge_iterator ei, ei_end;
  for (tie(ei, ei_end) = edges(g); ei != ei_end; ++ei)
      std::cout << "(" << index[source(*ei, g)] 
                << "," << index[target(*ei, g)] << ") ";

  printf("\n");
}